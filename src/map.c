#include "map.h"

#include <string.h>

#include "util.h"

typedef struct {
    int linha;
    int coluna;
} Posicao;

/* Indexados por int (0=NORTE, 1=SUL, 2=LESTE, 3=OESTE) em vez de Direcao de
 * proposito - ver aviso do handover (secao 6) sobre -Warray-bounds do GCC
 * com enum usado como indice de array apos inlining em -O2. */
static const int DELTA_LINHA[NUM_DIRECOES] = { -1, 1, 0, 0 };
static const int DELTA_COLUNA[NUM_DIRECOES] = { 0, 0, 1, -1 };
static const int OPOSTO[NUM_DIRECOES] = { SUL, NORTE, OESTE, LESTE };

static bool dentro_do_grid(const Mapa *mapa, int linha, int coluna) {
    return linha >= 0 && linha < mapa->tamanho && coluna >= 0 && coluna < mapa->tamanho;
}

/*
 * Randomized DFS / recursive backtracker a partir da Sala de Teleporte:
 * gera uma arvore geradora do grid, o que garante conectividade total por
 * construcao (toda celula visitada foi alcancada por uma porta valida).
 * Implementado com pilha explicita (em vez de recursao) para nao depender
 * de profundidade de call stack em grids grandes.
 */
static void esculpir_labirinto(Mapa *mapa) {
    bool visitada[MAX_SALAS][MAX_SALAS];
    memset(visitada, 0, sizeof(visitada));

    Posicao pilha[MAX_SALAS * MAX_SALAS];
    int topo = 0;

    pilha[topo].linha = mapa->teleporte_linha;
    pilha[topo].coluna = mapa->teleporte_coluna;
    topo++;
    visitada[mapa->teleporte_linha][mapa->teleporte_coluna] = true;

    while (topo > 0) {
        Posicao atual = pilha[topo - 1];

        int direcoes_disponiveis[NUM_DIRECOES];
        int n_disponiveis = 0;
        for (int d = 0; d < NUM_DIRECOES; d++) {
            int vl = atual.linha + DELTA_LINHA[d];
            int vc = atual.coluna + DELTA_COLUNA[d];
            if (dentro_do_grid(mapa, vl, vc) && !visitada[vl][vc]) {
                direcoes_disponiveis[n_disponiveis++] = d;
            }
        }

        if (n_disponiveis == 0) {
            topo--; /* beco sem saida: retrocede na pilha */
            continue;
        }

        int d = direcoes_disponiveis[sorteio_intervalo(0, n_disponiveis - 1)];
        int vl = atual.linha + DELTA_LINHA[d];
        int vc = atual.coluna + DELTA_COLUNA[d];

        mapa->celulas[atual.linha][atual.coluna].conectada[d] = true;
        mapa->celulas[vl][vc].conectada[OPOSTO[d]] = true;
        visitada[vl][vc] = true;

        pilha[topo].linha = vl;
        pilha[topo].coluna = vc;
        topo++;
    }
}

/*
 * Adiciona ciclos/atalhos na arvore geradora: para cada par de celulas
 * vizinhas ainda nao conectado, sorteia se abre uma porta extra. So testa
 * SUL e LESTE por celula para visitar cada par vizinho uma unica vez (o
 * par contrario, ex. NORTE de quem esta abaixo, e o mesmo par).
 */
static void adicionar_portas_extras(Mapa *mapa, const Config *cfg) {
    static const int DIRECOES_SEM_REPETICAO[2] = { SUL, LESTE };

    for (int linha = 0; linha < mapa->tamanho; linha++) {
        for (int coluna = 0; coluna < mapa->tamanho; coluna++) {
            for (int i = 0; i < 2; i++) {
                int d = DIRECOES_SEM_REPETICAO[i];
                int vl = linha + DELTA_LINHA[d];
                int vc = coluna + DELTA_COLUNA[d];
                if (!dentro_do_grid(mapa, vl, vc)) {
                    continue;
                }
                if (mapa->celulas[linha][coluna].conectada[d]) {
                    continue; /* ja conectado pela arvore geradora */
                }
                if (sorteio_chance(cfg->chance_porta_extra_labirinto)) {
                    mapa->celulas[linha][coluna].conectada[d] = true;
                    mapa->celulas[vl][vc].conectada[OPOSTO[d]] = true;
                }
            }
        }
    }
}

/*
 * Preenche tipo de sala, tripulante e item de cada celula. A Sala de
 * Teleporte e sempre "segura" (sem tripulante, item ou escuridao) - e o
 * ponto de partida/chegada do jogador (secao 2.4 do handover).
 */
static void povoar_celulas(Mapa *mapa, const Config *cfg, const BaseDeDados *bd) {
    int indice_tipo_teleporte = 0;
    for (int i = 0; i < bd->num_salas; i++) {
        if (bd->salas[i].teleporte) {
            indice_tipo_teleporte = i;
            break;
        }
    }

    int tipos_normais[MAX_SALAS];
    int n_normais = 0;
    for (int i = 0; i < bd->num_salas; i++) {
        if (!bd->salas[i].teleporte) {
            tipos_normais[n_normais++] = i;
        }
    }

    for (int linha = 0; linha < mapa->tamanho; linha++) {
        for (int coluna = 0; coluna < mapa->tamanho; coluna++) {
            Celula *celula = &mapa->celulas[linha][coluna];
            bool eh_teleporte = (linha == mapa->teleporte_linha && coluna == mapa->teleporte_coluna);

            if (eh_teleporte) {
                celula->id_tipo_sala = indice_tipo_teleporte;
                continue;
            }

            celula->id_tipo_sala = tipos_normais[sorteio_intervalo(0, n_normais - 1)];
            celula->escura = sorteio_chance(cfg->chance_sala_escura);
            celula->tem_item = sorteio_chance(cfg->chance_item_na_sala);

            celula->tem_tripulante = bd->num_tripulantes > 0 && sorteio_chance(cfg->chance_tripulante_na_sala);
            if (celula->tem_tripulante) {
                celula->id_tripulante = sorteio_intervalo(0, bd->num_tripulantes - 1);
                celula->tripulante_vivo = true;
                celula->tripulante_vida_atual = bd->tripulantes[celula->id_tripulante].vida;
                /* linha 6002 do original: "ME=INT(RND*150+100)" - energia
                 * do tripulante, usada em combat.c pra decidir se ele foge
                 * ou contra-ataca (Pacote 13). */
                celula->tripulante_energia_atual = sorteio_intervalo(100, 249);
            }
        }
    }
}

Mapa gerar_mapa(const Config *cfg, const BaseDeDados *bd) {
    Mapa mapa;
    memset(&mapa, 0, sizeof(mapa));

    mapa.tamanho = cfg->grid_size;
    mapa.teleporte_linha = sorteio_intervalo(0, mapa.tamanho - 1);
    mapa.teleporte_coluna = sorteio_intervalo(0, mapa.tamanho - 1);

    esculpir_labirinto(&mapa);
    adicionar_portas_extras(&mapa, cfg);
    povoar_celulas(&mapa, cfg, bd);

    return mapa;
}

bool mapa_totalmente_conectado(const Mapa *mapa) {
    bool visitada[MAX_SALAS][MAX_SALAS];
    memset(visitada, 0, sizeof(visitada));

    Posicao fila[MAX_SALAS * MAX_SALAS];
    int inicio = 0;
    int fim = 0;

    fila[fim].linha = mapa->teleporte_linha;
    fila[fim].coluna = mapa->teleporte_coluna;
    fim++;
    visitada[mapa->teleporte_linha][mapa->teleporte_coluna] = true;
    int contagem = 1;

    while (inicio < fim) {
        Posicao atual = fila[inicio];
        inicio++;

        const Celula *celula = &mapa->celulas[atual.linha][atual.coluna];
        for (int d = 0; d < NUM_DIRECOES; d++) {
            if (!celula->conectada[d]) {
                continue;
            }
            int vl = atual.linha + DELTA_LINHA[d];
            int vc = atual.coluna + DELTA_COLUNA[d];
            if (!dentro_do_grid(mapa, vl, vc) || visitada[vl][vc]) {
                continue;
            }
            visitada[vl][vc] = true;
            contagem++;
            fila[fim].linha = vl;
            fila[fim].coluna = vc;
            fim++;
        }
    }

    return contagem == mapa->tamanho * mapa->tamanho;
}
