#include "combat.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "player.h"
#include "util.h"

/* Custo de energia da lanterna ao examinar sala escura, linha 5030/5210 do
 * original (o limiar hardcoded de 20 e' o mesmo valor usado como custo). */
#define CUSTO_ENERGIA_LANTERNA 20

/* Drenagem de energia do escudo por sala percorrida, linha 3600 do original. */
#define CUSTO_ENERGIA_ESCUDO_POR_SALA 2

/* Chance (%) de tropecar e cair ao tentar fugir, linha 2040 do original
 * ("IF RND>.1 THEN GOTO 2048" - so 10% cai, os outros 90% fogem). */
#define CHANCE_FUGIR_SUCESSO 90

/* Chance (%) de o tripulante em panico escapar mesmo assim, linha 6801
 * ("IF RND>.33 THEN GOTO 6810" - 67% foge, 33% cai na tentativa). */
#define CHANCE_PANICO_ESCAPAR 67

/* Chance (%) de um tripulante agressivo decidir fugir so por sorte, mesmo
 * com energia suficiente pra arma, linha 6507 ("RND<.1"). A outra metade
 * da condicao (energia propria menor que o custo da arma) e' avaliada
 * direto em reacao_tripulante_apos_turno. */
#define CHANCE_FUGA_POR_SORTE 10

/* Chance (%) por passo (a partir do segundo) de perder o rastro ao
 * perseguir um tripulante fugido, linha 6839 ("RND<.1"). */
#define CHANCE_PERSEGUICAO_PERDER 10

/* Chance (%) de o tripulante de quem o jogador fugiu vir atras dele pra
 * sala de destino, linha 2120 ("RND<.5" - simetrico, 50/50). So' se aplica
 * se a sala de destino ainda nao tiver outro tripulante. */
#define CHANCE_TRIPULANTE_SEGUIR_FUGA_JOGADOR 50

/* Chance (%) de sortear um medicamento extra ao saquear o corpo de um
 * tripulante agressivo morto, linha 1830 do original ("IF RND>.2 THEN
 * GOTO 1860" - 20% ganha medicamento). */
#define CHANCE_LOOT_MEDICAMENTO 20

/* Chance (%) de sortear dinheiro extra ao saquear o corpo, linha 1860 do
 * original ("IF RND>.7 THEN GOTO 1900" - 70% ganha dinheiro, nao 30%). */
#define CHANCE_LOOT_DINHEIRO 70

/* Faixa de dinheiro (A$) saqueada do corpo, linha 1870 do original
 * ("INT(RND*200+50)" = 50 a 249). */
#define DINHEIRO_MINIMO_LOOT 50
#define DINHEIRO_MAXIMO_LOOT 249

/* Item encontrado ao examinar a sala (linha 5240-5390 do original) - ver
 * uso em comando_examinar_sala para o porque de cada limiar. */
#define ENERGIA_CARGA 100
#define ENERGIA_CARGA_LIMIAR_UMA 0.5
#define ENERGIA_CARGA_LIMIAR_DUAS 0.2
#define MEDICAMENTO_ITEM_LIMIAR_UM 0.4
#define MEDICAMENTO_ITEM_LIMIAR_DOIS 0.15
#define DINHEIRO_BASE_ITEM_SALA 50
#define DINHEIRO_FATOR_ITEM_SALA 2000

/* Limiar de suborno: aceita se a oferta superar um sorteio em [0,999],
 * linha 3190 do original ("IF X>RND*1E3 THEN GOTO 3220"). */
#define SUBORNO_LIMIAR_MAXIMO 999

/* Chance (%) de o tripulante irritado escorregar e ir embora, linha 3270
 * do original ("IF RND<.2 THEN GOTO 3300"). */
#define CHANCE_IRRITAR_SUCESSO 20

/* Chance (%) de o tripulante ficar amigavel por sorte (alem do caso de
 * vida baixa abaixo), linha 3330 do original ("IF RND<.09 OR MBP<2 THEN
 * GOTO 3360"). */
#define CHANCE_AMIGAVEL_SUCESSO 9
#define VIDA_AMIGAVEL_GARANTIDO 2

/* Faixa de dinheiro dado pelo tripulante amigavel, linha 3360 do original
 * ("INT(RND*250+1)" = 1 a 250). */
#define DINHEIRO_MINIMO_AMIGAVEL 1
#define DINHEIRO_MAXIMO_AMIGAVEL 250

/* Indexados por int (nao Direcao) de proposito - ver aviso do handover
 * (secao 6) sobre -Warray-bounds do GCC com enum usado como indice apos
 * inlining em -O2, mesmo padrao adotado em map.c. */
static const int DELTA_LINHA[NUM_DIRECOES] = { -1, 1, 0, 0 };
static const int DELTA_COLUNA[NUM_DIRECOES] = { 0, 0, 1, -1 };

static Resultado resultado_vazio(void) {
    Resultado r;
    memset(&r, 0, sizeof(r));
    r.sucesso = true;
    r.jogador_morreu = false;
    return r;
}

static void log_msg(Resultado *r, const char *formato, ...) {
    if (r->num_mensagens >= MAX_MENSAGENS_RESULTADO) {
        return;
    }
    va_list args;
    va_start(args, formato);
    vsnprintf(r->mensagens[r->num_mensagens], MAX_TAMANHO_MENSAGEM, formato, args);
    va_end(args);
    r->num_mensagens++;
}

/* Marca a ultima mensagem preenchida (Pacote 20) pra ter uma pausa
 * dramatica depois dela, antes da proxima - chamar logo apos o log_msg(...)
 * que deve pausar. No-op se ainda nao houver mensagem nenhuma (nunca
 * deveria acontecer na pratica, so' evita indice -1). */
static void marcar_pausa(Resultado *r) {
    if (r->num_mensagens > 0) {
        r->pausa_apos[r->num_mensagens - 1] = true;
    }
}

/* Acidente no escuro (linha 7000): chance de se machucar ao examinar uma
 * sala escura sem lanterna. Dano de 1 a 5, mensagem varia por gravidade. */
static void acidente_no_escuro(Jogador *jogador, Resultado *r) {
    int dano = sorteio_intervalo(1, 5);
    if (dano == 1) {
        log_msg(r, "No escuro, você bateu na parede.");
    } else if (dano <= 3) {
        log_msg(r, "Você caiu no escuro e quebrou seu pé.");
    } else {
        log_msg(r, "Você caiu no escuro e fraturou a perna.");
    }
    log_msg(r, "Você perdeu %d ponto%s de vida.", dano, dano > 1 ? "s" : "");

    jogador->vida -= dano;
    if (jogador->vida <= 0) {
        jogador->vida = 0;
        log_msg(r, "Que azar, você morreu.");
        r->jogador_morreu = true;
    }
}

/*
 * Descreve tipo de sala/saidas/tripulante da celula em (linha, coluna) -
 * conteudo compartilhado entre "entrar em sala nova" (abaixo) e o comando
 * Examinar interativo (Pacote 11, game.c), que repete a mesma narracao
 * antes do resultado da busca.
 */
static void narrar_sala(const Mapa *mapa, const BaseDeDados *bd, int linha, int coluna, Resultado *r) {
    const Celula *celula = &mapa->celulas[linha][coluna];
    const TipoSala *tipo = &bd->salas[celula->id_tipo_sala];
    log_msg(r, "Sala tipo: %s", tipo->nome);

    char saidas[MAX_TAMANHO_MENSAGEM] = "Saídas: ";
    bool alguma = false;
    for (int d = 0; d < NUM_DIRECOES; d++) {
        if (celula->conectada[d]) {
            if (alguma) {
                strncat(saidas, ", ", sizeof(saidas) - strlen(saidas) - 1);
            }
            strncat(saidas, direcao_nome((Direcao)d), sizeof(saidas) - strlen(saidas) - 1);
            alguma = true;
        }
    }
    if (!alguma) {
        strncat(saidas, "nenhuma", sizeof(saidas) - strlen(saidas) - 1);
    }
    log_msg(r, "%s", saidas);

    if (celula->tem_tripulante && celula->tripulante_vivo) {
        const Tripulante *tripulante = &bd->tripulantes[celula->id_tripulante];
        /* Linha 6270 do original: a arma do tripulante ja e' revelada na
         * apresentacao, antes de qualquer ataque ("...ARMADO COM [arma]..."). */
        const Arma *arma = &bd->armas[tripulante->id_arma];
        /* Pacote 20: as duas primeiras falas ("Ha alguem...", "Ha alguma
         * coisa aqui...") tem pausa dramatica entre elas, igual as linhas
         * 6150/6210 do original - constroem a tensao antes da revelacao.
         * Pacote 29: a revelacao em si (rotulo "TWIN reporta..." + nome/arma
         * + frase, linha 6270 do original, atribuida ao "TWIN", o
         * tradutor/conselheiro eletronico da tela de titulo) precisou virar
         * 3 mensagens em vez de uma só - juntas numa unica chamada de
         * log_msg, nome+arma+frase podiam passar dos 96 bytes de
         * MAX_TAMANHO_MENSAGEM (confirmado com os dados reais de
         * crew.json/weapons.json) e vsnprintf cortava o fim da mensagem
         * silenciosamente. Mas as 3 saem sem pausa entre si - a tensao ja
         * foi construida pelas duas falas anteriores, e mais pausa aqui so'
         * deixaria a revelacao lenta sem ganhar nada em ritmo. */
        log_msg(r, "Há alguém...");
        marcar_pausa(r);
        log_msg(r, "Há alguma coisa aqui...");
        marcar_pausa(r);
        log_msg(r, "TWIN reporta...");
        log_msg(r, "É um %s, armado com %s.", tripulante->nome, arma->nome);
        log_msg(r, "%s", tripulante->frase);
    } else {
        log_msg(r, "Não há ninguém aqui.");
    }
}

void combat_narrar_sala_atual(const Jogador *jogador, const Mapa *mapa, const BaseDeDados *bd, Resultado *r) {
    narrar_sala(mapa, bd, jogador->linha, jogador->coluna, r);
}

/* Entrar em sala nova (linha 6002): narra tipo de sala, saidas e presenca
 * de tripulante; drena energia do escudo se ligado (linha 6115 + 3600);
 * marca a sala como visitada (Pacote 14, so' usado pelo mapa ASCII). */
static void entrar_em_sala(Jogador *jogador, Mapa *mapa, const BaseDeDados *bd, Resultado *r) {
    log_msg(r, "Você entrou numa nova sala.");
    mapa->celulas[jogador->linha][jogador->coluna].visitada = true;
    narrar_sala(mapa, bd, jogador->linha, jogador->coluna, r);

    if (jogador->escudo_ligado) {
        jogador->energia -= CUSTO_ENERGIA_ESCUDO_POR_SALA;
        if (jogador->energia <= 0) {
            jogador->energia = 0;
            jogador->escudo_ligado = false;
            log_msg(r, "Escudo desativado: energia esgotada.");
        }
    }
}

/* Desliga o escudo se a energia zerou (linha 3620), chamada tanto ao
 * drenar energia por sala (acima) quanto antes de recusar um ataque por
 * falta de energia (linha 1508). */
static void desligar_escudo_se_sem_energia(Jogador *jogador, Resultado *r) {
    if (jogador->escudo_ligado && jogador->energia <= 0) {
        jogador->energia = 0;
        jogador->escudo_ligado = false;
        log_msg(r, "Escudo desativado: energia esgotada.");
    }
}

/*
 * Executa a fuga bem-sucedida de um tripulante (linhas 6820-6860 do
 * original, Pacote 13): sorteia um caminho aleatorio de celulas
 * conectadas a partir de (linha, coluna) ate achar uma sem outro
 * tripulante, e relocaliza o registro dele (vida/energia atuais) pra la -
 * ele nao "desaparece", continua existindo em outra sala do mapa, como no
 * original. A relocacao acontece independente do jogador escolher segui-lo
 * depois (o original tambem faz isso incondicionalmente, GOSUB 6859 nos
 * dois ramos S/N da pergunta). O caminho sorteado fica em 'r' pra quem
 * orquestra o jogo (game.c) oferecer a perseguicao.
 */
static void tripulante_fugir_para_outra_sala(Mapa *mapa, int linha, int coluna, Resultado *r) {
    Celula *origem = &mapa->celulas[linha][coluna];

    int trilha[MAX_TRILHA_FUGA];
    int tamanho = 0;
    int l = linha;
    int c = coluna;
    bool achou_destino = false;

    while (tamanho < MAX_TRILHA_FUGA) {
        int direcoes_disponiveis[NUM_DIRECOES];
        int n = 0;
        for (int d = 0; d < NUM_DIRECOES; d++) {
            if (mapa->celulas[l][c].conectada[d]) {
                direcoes_disponiveis[n++] = d;
            }
        }
        int d = direcoes_disponiveis[sorteio_intervalo(0, n - 1)];
        trilha[tamanho++] = d;
        l += DELTA_LINHA[d];
        c += DELTA_COLUNA[d];
        if (!mapa->celulas[l][c].tem_tripulante) {
            achou_destino = true;
            break;
        }
    }

    if (!achou_destino) {
        origem->tem_tripulante = false;
        return;
    }

    Celula *destino = &mapa->celulas[l][c];
    destino->tem_tripulante = true;
    destino->id_tripulante = origem->id_tripulante;
    destino->tripulante_vivo = true;
    destino->tripulante_vida_atual = origem->tripulante_vida_atual;
    destino->tripulante_energia_atual = origem->tripulante_energia_atual;
    origem->tem_tripulante = false;

    r->tripulante_fugiu = true;
    memcpy(r->trilha_fuga, trilha, sizeof(int) * (size_t)tamanho);
    r->trilha_fuga_tamanho = tamanho;
}

/*
 * Reacao do tripulante quando sobrevive ao turno do jogador (ataque que
 * errou ou nao matou, fuga que caiu, comunicacao que fracassou, ou uso de
 * medicamento bem-sucedido) - equivalente a sub-rotina compartilhada da
 * linha 6505 (GOTO 6500), chamada de varios pontos no original.
 *
 * Decisao de fugir vs contra-atacar, linha 6507: "IF (ME<MP OR RND<.1) AND
 * MI THEN GOTO 6800" - so' tripulantes agressivos (MI=1) avaliam fugir,
 * quando a propria energia (ME) e' menor que o custo da arma (MP) ou por
 * 10% de sorte; nao-agressivos (MI=0) sempre contra-atacam, ja que a
 * condicao "AND MI" nunca e' verdadeira pra eles. Isso e' o oposto do que
 * uma leitura ingenua do campo 'agressivo' sugere - a inversao foi
 * descoberta no Pacote 10 e corrigida aqui no Pacote 13, junto com a
 * perseguicao (linhas 6800-6860) que antes so fazia o tripulante
 * desaparecer.
 */
static void reacao_tripulante_apos_turno(Jogador *jogador, Mapa *mapa, const BaseDeDados *bd, Resultado *r) {
    Celula *celula = &mapa->celulas[jogador->linha][jogador->coluna];
    if (!celula->tem_tripulante || !celula->tripulante_vivo) {
        return;
    }
    const Tripulante *tripulante = &bd->tripulantes[celula->id_tripulante];
    const Arma *arma = &bd->armas[tripulante->id_arma];

    bool decide_fugir = tripulante->agressivo &&
        (celula->tripulante_energia_atual < arma->custo_energia || sorteio_chance(CHANCE_FUGA_POR_SORTE));

    if (decide_fugir) {
        log_msg(r, "O %s entra em pânico.", tripulante->nome);
        if (!sorteio_chance(CHANCE_PANICO_ESCAPAR)) {
            log_msg(r, "E na tentativa de fugir, cai.");
            return;
        }
        log_msg(r, "E foge.");
        tripulante_fugir_para_outra_sala(mapa, jogador->linha, jogador->coluna, r);
        return;
    }

    log_msg(r, "O %s ataca com %s.", tripulante->nome, arma->nome);
    celula->tripulante_energia_atual -= arma->custo_energia;

    if (!sorteio_chance(arma->chance_acerto_percentual)) {
        log_msg(r, "Quase te acerta, mas erra.");
        return;
    }

    int dano = sorteio_intervalo(1, arma->dano_maximo);
    if (jogador->escudo_ligado) {
        /* INT(X/3+0.7), linha 6590 do original: arredonda em vez de
         * truncar (dano/3 puro subestimaria o dano reduzido). Em inteiros,
         * INT(X/3+0.7) = INT((10X+21)/30). */
        dano = (dano * 10 + 21) / 30;
        jogador->energia -= dano * 5;
        desligar_escudo_se_sem_energia(jogador, r);
        if (dano == 0) {
            log_msg(r, "O escudo absorve o golpe.");
            return;
        }
    }

    log_msg(r, "Acertou em você. Você perdeu %d ponto%s de vida.", dano, dano > 1 ? "s" : "");
    jogador->vida -= dano;
    if (jogador->vida <= 0) {
        jogador->vida = 0;
        log_msg(r, "Você foi mortalmente ferido. O %s roubou-lhe tudo.", tripulante->nome);
        r->jogador_morreu = true;
    }
}

Resultado combate_seguir_tripulante_fugido(Jogador *jogador, Mapa *mapa, const BaseDeDados *bd, const int *trilha_fuga, int trilha_fuga_tamanho) {
    Resultado r = resultado_vazio();

    for (int i = 0; i < trilha_fuga_tamanho; i++) {
        if (i > 0 && sorteio_chance(CHANCE_PERSEGUICAO_PERDER)) {
            log_msg(&r, "Você o perdeu neste ponto.");
            break;
        }
        int d = trilha_fuga[i];
        log_msg(&r, "Você o segue pelo lado %s.", direcao_nome((Direcao)d));
        jogador->linha += DELTA_LINHA[d];
        jogador->coluna += DELTA_COLUNA[d];
    }

    /* Linha 6852 do original: "GOTO 6000" - chega (ou para no meio do
     * caminho, se perdeu o rastro) e a sala e' narrada como uma entrada
     * normal, drenando escudo se ligado. */
    entrar_em_sala(jogador, mapa, bd, &r);
    return r;
}

Resultado comando_mover(Jogador *jogador, Mapa *mapa, const BaseDeDados *bd, Direcao direcao) {
    Resultado r = resultado_vazio();

    const Celula *atual = &mapa->celulas[jogador->linha][jogador->coluna];
    if (atual->tem_tripulante && atual->tripulante_vivo) {
        const Tripulante *tripulante = &bd->tripulantes[atual->id_tripulante];
        log_msg(&r, "Não se arrisque dando as costas ao %s.", tripulante->nome);
        r.sucesso = false;
        return r;
    }

    if (!atual->conectada[direcao]) {
        log_msg(&r, "Não há saída pelo %s.", direcao_nome(direcao));
        r.sucesso = false;
        return r;
    }

    jogador->linha += DELTA_LINHA[direcao];
    jogador->coluna += DELTA_COLUNA[direcao];

    entrar_em_sala(jogador, mapa, bd, &r);
    return r;
}

Resultado comando_trocar_arma(Jogador *jogador, const BaseDeDados *bd, int indice_no_inventario) {
    Resultado r = resultado_vazio();

    if (jogador->num_armas_obtidas <= 1) {
        log_msg(&r, "Você só tem uma arma.");
        r.sucesso = false;
        return r;
    }

    if (!jogador_trocar_arma(jogador, indice_no_inventario)) {
        log_msg(&r, "Índice de arma inválido.");
        r.sucesso = false;
        return r;
    }

    const Arma *arma = &bd->armas[jogador->armas_obtidas[jogador->arma_atual]];
    log_msg(&r, "Sua arma agora é: %s.", arma->nome);
    return r;
}

Resultado comando_escudo(Jogador *jogador) {
    Resultado r = resultado_vazio();

    if (jogador->energia <= 0) {
        log_msg(&r, "Energia insuficiente.");
        r.sucesso = false;
        return r;
    }

    jogador->escudo_ligado = !jogador->escudo_ligado;
    log_msg(&r, "Agora seu escudo está %s.", jogador->escudo_ligado ? "ligado" : "desligado");
    return r;
}

Resultado comando_usar_medicamento(Jogador *jogador, Mapa *mapa, const BaseDeDados *bd, const Config *cfg) {
    Resultado r = resultado_vazio();
    bool tinha_medicamento = jogador->num_medicamentos > 0;

    if (jogador_usar_medicamento(jogador, cfg->vida_inicial)) {
        log_msg(&r, "Sente-se melhor? Você agora tem %d pontos de vida.", jogador->vida);
        reacao_tripulante_apos_turno(jogador, mapa, bd, &r);
        return r;
    }

    if (tinha_medicamento) {
        log_msg(&r, "Não seja hipocondríaco... você está bem.");
    } else {
        log_msg(&r, "Você não tem mais medicamentos.");
    }
    r.sucesso = false;
    return r;
}

Resultado comando_situacao(const Jogador *jogador, const BaseDeDados *bd) {
    Resultado r = resultado_vazio();

    log_msg(&r, "Situação");
    log_msg(&r, "Vida: %d", jogador->vida);
    log_msg(&r, "Medicamentos: %d", jogador->num_medicamentos);
    log_msg(&r, "Energia: %d", jogador->energia);
    log_msg(&r, "Dinheiro: %d", jogador->dinheiro);
    log_msg(&r, "Armas: %d", jogador->num_armas_obtidas);

    if (jogador->vida > 0 && jogador->num_armas_obtidas > 0) {
        const Arma *arma = &bd->armas[jogador->armas_obtidas[jogador->arma_atual]];
        log_msg(&r, "Arma atual: %s", arma->nome);
    }

    return r;
}

Resultado comando_examinar_sala(Jogador *jogador, Mapa *mapa, const Config *cfg, bool usar_lanterna) {
    Resultado r = resultado_vazio();

    Celula *celula = &mapa->celulas[jogador->linha][jogador->coluna];
    if (celula->tem_tripulante && celula->tripulante_vivo) {
        log_msg(&r, "Não pode examinar a sala com alguém nela.");
        r.sucesso = false;
        return r;
    }

    if (celula->escura && usar_lanterna && jogador->energia < CUSTO_ENERGIA_LANTERNA) {
        log_msg(&r, "Energia insuficiente para usar a lanterna.");
        r.sucesso = false;
        return r;
    }

    /* Pacote 33: a partir daqui o exame de fato acontece (achando item ou
     * não, sala clara ou escura) - marca a sala como examinada pro mapa
     * ASCII (ui.c), distinto de item_coletado (que só fica true se havia
     * item a coletar). Fica antes do bloco de escura/lanterna abaixo de
     * propósito: mesmo o acidente no escuro (que pode matar o jogador) só
     * acontece depois de já ter se comprometido a vasculhar a sala. */
    celula->examinada = true;

    if (celula->escura) {
        if (usar_lanterna) {
            jogador->energia -= CUSTO_ENERGIA_LANTERNA;
            log_msg(&r, "Usando a lanterna, você examina a sala.");
        } else {
            log_msg(&r, "Está muito escuro aqui.");
            if (sorteio_chance(cfg->chance_acidente_no_escuro)) {
                acidente_no_escuro(jogador, &r);
                if (r.jogador_morreu) {
                    return r;
                }
            }
        }
    } else {
        log_msg(&r, "Que sorte. Existe luz suficiente para examinar a sala sem usar a lanterna.");
    }

    /* Pacote 20: linhas 5170-5180 do original - FOR/NEXT vazio de 1 a 8
     * antes de "VOCE ENCONTROU...", unico ponto deste pacote com lastro
     * direto de delay explicito no BASIC (ver management/backlog/
     * 20-pausas-dramaticas.md). */
    marcar_pausa(&r);
    log_msg(&r, "Você encontrou...");
    /* Pausa entre o anuncio e a revelacao em si (Nada/item) - reconstrucao
     * de fidelidade a experiencia, sem FOR/NEXT explicito correspondente
     * no original nesse ponto especifico. */
    marcar_pausa(&r);
    if (!celula->tem_item || celula->item_coletado) {
        log_msg(&r, "Nada.");
        return r;
    }

    /* Linhas 5220-5230 do original: "LET X$=R$(D,A,6)" / "LET R$(D,A,6)="
     * " " - o tipo de item da sala e' lido e IMEDIATAMENTE apagado do
     * array, antes ate' de decidir a recompensa - reexaminar a mesma sala
     * depois disso ve R$(D,A,6)=" ", nao bate com nenhum dos tipos P/M/E
     * e cai direto em "NADA." (linha 5410). Achado/corrigido no Pacote 24:
     * a porta ja tinha o campo Celula::item_coletado pra isso, mas nada
     * setava ele pra true - a sala sorteava um item novo toda vez que era
     * reexaminada, ao inves de uma unica vez. */
    celula->item_coletado = true;

    /*
     * O original (linha 5220 em diante) guarda o tipo de item por sala
     * desde a geracao do mapa (um char P/M/E fixo por sala). O mapa
     * (Pacote 4) ja simplifica presenca de item para um bool sem tipo
     * pre-definido (ver combat.h) - aqui sorteamos o tipo com peso
     * uniforme (1/3 cada), mas as quantidades/valores dentro de cada tipo
     * abaixo replicam o viés exato do original (Pacote 12).
     */
    int tipo = sorteio_intervalo(0, 2);
    if (tipo == 0) {
        /* Cargas de energia, linha 5260-5270: "X=RND"; 1 carga (50%,
         * X>.5), 2 cargas (30%, .2<=X<=.5) ou 3 cargas (20%, X<.2), cada
         * uma valendo 100. */
        double x = sorteio_uniforme();
        int cargas = (x > ENERGIA_CARGA_LIMIAR_UMA) ? 1 : (x >= ENERGIA_CARGA_LIMIAR_DUAS) ? 2 : 3;
        int energia = cargas * ENERGIA_CARGA;
        jogador->energia += energia;
        log_msg(&r, "Cargas de energia: +%d.", energia);
    } else if (tipo == 1) {
        /* Medicamentos, linha 5330-5340: "X=RND"; 1 (60%, X>.4), 2 (25%,
         * .15<X<=.4) ou 3 (15%, X<=.15) de uma vez. */
        double x = sorteio_uniforme();
        int qtd = (x > MEDICAMENTO_ITEM_LIMIAR_UM) ? 1 : (x > MEDICAMENTO_ITEM_LIMIAR_DOIS) ? 2 : 3;
        jogador->num_medicamentos += qtd;
        log_msg(&r, "Medicamentos: +%d.", qtd);
    } else {
        /* Dinheiro achado na sala, linha 5370: "INT(50+RND*RND*RND*2E3)" -
         * cubo de RND enviesa fortemente para valores baixos dentro da
         * faixa nominal 50 a 2050. */
        double x = sorteio_uniforme();
        int dinheiro = (int)(DINHEIRO_BASE_ITEM_SALA + x * x * x * DINHEIRO_FATOR_ITEM_SALA);
        jogador->dinheiro += dinheiro;
        log_msg(&r, "A$ %d.00", dinheiro);
    }

    return r;
}

Resultado comando_acionar_teleporte(Jogador *jogador, const Mapa *mapa) {
    Resultado r = resultado_vazio();

    if (jogador->linha != mapa->teleporte_linha || jogador->coluna != mapa->teleporte_coluna) {
        log_msg(&r, "Você não se encontra na Sala de Teleporte.");
        r.sucesso = false;
        return r;
    }

    const Celula *celula = &mapa->celulas[jogador->linha][jogador->coluna];
    if (celula->tem_tripulante && celula->tripulante_vivo) {
        log_msg(&r, "Não pode usar o teleporte com alguém na sala.");
        r.sucesso = false;
        return r;
    }

    log_msg(&r, "Você voltou via teleporte. Partida encerrada com sucesso.");
    return r;
}

/* Energia ganha ao saquear o corpo de um tripulante morto, linha 1750 do
 * original ("INT(RND*120+30)" = 30 a 149). */
#define ENERGIA_MINIMA_LOOT 30
#define ENERGIA_MAXIMA_LOOT 149

Resultado comando_atacar(Jogador *jogador, Mapa *mapa, const BaseDeDados *bd) {
    Resultado r = resultado_vazio();
    Celula *celula = &mapa->celulas[jogador->linha][jogador->coluna];

    if (!celula->tem_tripulante || !celula->tripulante_vivo) {
        log_msg(&r, "Atacar quem? Não há ninguém aqui.");
        r.sucesso = false;
        return r;
    }

    const Tripulante *tripulante = &bd->tripulantes[celula->id_tripulante];
    const Arma *arma = &bd->armas[jogador->armas_obtidas[jogador->arma_atual]];

    if (arma->custo_energia > jogador->energia) {
        log_msg(&r, "Você não tem energia suficiente para atacar.");
        desligar_escudo_se_sem_energia(jogador, &r);
        r.sucesso = false;
        return r;
    }

    log_msg(&r, "Você ataca o %s.", tripulante->nome);
    jogador->energia -= arma->custo_energia;

    if (!sorteio_chance(arma->chance_acerto_percentual)) {
        log_msg(&r, "Errou. Má sorte.");
        reacao_tripulante_apos_turno(jogador, mapa, bd, &r);
        return r;
    }

    int dano = sorteio_intervalo(1, arma->dano_maximo);
    log_msg(&r, "Acertou nele, reduzindo %d ponto%s de vida.", dano, dano > 1 ? "s" : "");
    celula->tripulante_vida_atual -= dano;

    if (celula->tripulante_vida_atual > 0) {
        reacao_tripulante_apos_turno(jogador, mapa, bd, &r);
        return r;
    }

    log_msg(&r, "O %s morreu.", tripulante->nome);
    celula->tripulante_vivo = false;
    celula->tem_tripulante = false;

    if (jogador_adicionar_arma(jogador, tripulante->id_arma)) {
        log_msg(&r, "Você recolhe a arma dele: %s.", bd->armas[tripulante->id_arma].nome);
    }

    int energia_ganha = sorteio_intervalo(ENERGIA_MINIMA_LOOT, ENERGIA_MAXIMA_LOOT);
    jogador->energia += energia_ganha;
    log_msg(&r, "Ele tinha uma carga de energia: +%d.", energia_ganha);

    /* Loot extra so para tripulantes agressivos (linha 1800: MI=1) -
     * os nao-agressivos (robots/serventes) nao carregam mais nada. */
    if (tripulante->agressivo) {
        if (sorteio_chance(CHANCE_LOOT_MEDICAMENTO)) {
            jogador->num_medicamentos++; /* linha 1850: "LET M=M+1" */
            log_msg(&r, "Também encontrou medicamentos.");
        }
        if (sorteio_chance(CHANCE_LOOT_DINHEIRO)) {
            int dinheiro = sorteio_intervalo(DINHEIRO_MINIMO_LOOT, DINHEIRO_MAXIMO_LOOT);
            jogador->dinheiro += dinheiro;
            log_msg(&r, "E A$ %d.00.", dinheiro);
        }
    }

    return r;
}

Resultado comando_fugir(Jogador *jogador, Mapa *mapa, const BaseDeDados *bd) {
    Resultado r = resultado_vazio();
    Celula *celula = &mapa->celulas[jogador->linha][jogador->coluna];

    if (!celula->tem_tripulante || !celula->tripulante_vivo) {
        log_msg(&r, "Fugir de quem, covarde?");
        r.sucesso = false;
        return r;
    }

    if (!sorteio_chance(CHANCE_FUGIR_SUCESSO)) {
        log_msg(&r, "Você tenta fugir, mas cai.");
        reacao_tripulante_apos_turno(jogador, mapa, bd, &r);
        r.sucesso = false;
        return r;
    }

    int direcoes_disponiveis[NUM_DIRECOES];
    int n_disponiveis = 0;
    for (int d = 0; d < NUM_DIRECOES; d++) {
        if (celula->conectada[d]) {
            direcoes_disponiveis[n_disponiveis++] = d;
        }
    }
    if (n_disponiveis == 0) {
        log_msg(&r, "Não há para onde fugir.");
        r.sucesso = false;
        return r;
    }

    /* Guardado antes de mover, pra poder trazer o mesmo tripulante atras
     * do jogador (linha 2120-2150) se a sala de destino estiver vazia e a
     * sorte decidir. */
    int id_tripulante_fugido = celula->id_tripulante;
    int vida_tripulante_fugido = celula->tripulante_vida_atual;
    int energia_tripulante_fugido = celula->tripulante_energia_atual;

    int direcao = direcoes_disponiveis[sorteio_intervalo(0, n_disponiveis - 1)];
    log_msg(&r, "Você fugiu pelo lado %s.", direcao_nome((Direcao)direcao));

    jogador->linha += DELTA_LINHA[direcao];
    jogador->coluna += DELTA_COLUNA[direcao];

    /*
     * Perseguicao ao fugir, linha 2120 do original: "IF R$(X,Y,7)<>" " OR
     * RND<.5 THEN GOTO 2200" - so' NAO te segue se a sala de destino ja
     * tiver outro tripulante, ou por 50% de sorte; senao ele vem atras
     * (Pacote 13). Diferente do tripulante fugindo DE voce em combate
     * (reacao_tripulante_apos_turno / tripulante_fugir_para_outra_sala,
     * linhas 6800-6870, onde a sala de origem e' sempre limpa
     * independente do jogador seguir ou nao): aqui a sub-rotina que limpa
     * a sala de origem (GOSUB 6860, linha 2150) so' roda dentro do ramo
     * "ele te seguiu" - se ele NAO seguir, R$(D,A,7) nunca e' apagado, ou
     * seja, o tripulante continua na sala original, vivo (a linha 2090,
     * GOSUB 6900, ja salva a vida/energia atuais de volta la' antes de
     * saber se ele vai seguir - so' faz sentido persistir isso se ele
     * puder continuar ali). Corrigido no Pacote 23: antes `celula->
     * tem_tripulante` era zerado incondicionalmente aqui, esvaziando a
     * sala de origem mesmo quando o tripulante nao seguia.
     */
    Celula *destino = &mapa->celulas[jogador->linha][jogador->coluna];
    if (!destino->tem_tripulante && sorteio_chance(CHANCE_TRIPULANTE_SEGUIR_FUGA_JOGADOR)) {
        celula->tem_tripulante = false;
        destino->tem_tripulante = true;
        destino->id_tripulante = id_tripulante_fugido;
        destino->tripulante_vivo = true;
        destino->tripulante_vida_atual = vida_tripulante_fugido;
        destino->tripulante_energia_atual = energia_tripulante_fugido;
        log_msg(&r, "Infelizmente o %s veio atrás de você.", bd->tripulantes[id_tripulante_fugido].nome);
    }

    entrar_em_sala(jogador, mapa, bd, &r);
    return r;
}

Resultado comando_comunicar(Jogador *jogador, Mapa *mapa, const BaseDeDados *bd, AcaoComunicar acao, int valor_oferecido) {
    Resultado r = resultado_vazio();
    Celula *celula = &mapa->celulas[jogador->linha][jogador->coluna];

    if (!celula->tem_tripulante || !celula->tripulante_vivo) {
        log_msg(&r, "Falando consigo mesmo... é o primeiro sinal de loucura.");
        r.sucesso = false;
        return r;
    }

    const Tripulante *tripulante = &bd->tripulantes[celula->id_tripulante];
    if (!tripulante->agressivo) {
        log_msg(&r, "O %s é pouco inteligente para comunicar-se.", tripulante->nome);
        r.sucesso = false;
        return r;
    }

    switch (acao) {
        case COMUNICAR_SUBORNAR:
            if (valor_oferecido > jogador->dinheiro) {
                log_msg(&r, "Você não tem todo esse dinheiro.");
                r.sucesso = false;
                return r;
            }
            log_msg(&r, "Está pensando...");
            if (valor_oferecido > sorteio_intervalo(0, SUBORNO_LIMIAR_MAXIMO)) {
                jogador->dinheiro -= valor_oferecido;
                log_msg(&r, "Aceita, deixando-o em paz.");
                celula->tem_tripulante = false;
                return r;
            }
            log_msg(&r, "E decide não aceitar.");
            r.sucesso = false;
            reacao_tripulante_apos_turno(jogador, mapa, bd, &r);
            return r;

        case COMUNICAR_IRRITAR:
            if (sorteio_chance(CHANCE_IRRITAR_SUCESSO)) {
                log_msg(&r, "O %s escorrega e vai longe.", tripulante->nome);
                celula->tem_tripulante = false;
                return r;
            }
            log_msg(&r, "Não gostou da sua atitude agressiva.");
            r.sucesso = false;
            reacao_tripulante_apos_turno(jogador, mapa, bd, &r);
            return r;

        case COMUNICAR_AMIGAVEL:
            if (sorteio_chance(CHANCE_AMIGAVEL_SUCESSO) || celula->tripulante_vida_atual < VIDA_AMIGAVEL_GARANTIDO) {
                int dinheiro = sorteio_intervalo(DINHEIRO_MINIMO_AMIGAVEL, DINHEIRO_MAXIMO_AMIGAVEL);
                jogador->dinheiro += dinheiro;
                log_msg(&r, "O %s achou você um cara legal, deu-lhe A$ %d.00 e foi embora.", tripulante->nome, dinheiro);
                celula->tem_tripulante = false;
                return r;
            }
            log_msg(&r, "Não gosta do seu sorriso.");
            r.sucesso = false;
            reacao_tripulante_apos_turno(jogador, mapa, bd, &r);
            return r;
    }

    return r;
}
