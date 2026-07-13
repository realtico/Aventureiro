#include "player.h"

#include <string.h>

#include "util.h"

/* Cura ao usar medicamento, linha 4070 do original: "BP+7+INT(RND*3+1)" -
 * base fixa + 1 a 3 pontos, nunca a vida cheia de uma vez. */
#define CURA_MEDICAMENTO_BASE 7
#define CURA_MEDICAMENTO_VARIACAO_MINIMA 1
#define CURA_MEDICAMENTO_VARIACAO_MAXIMA 3

Jogador jogador_iniciar(const Config *cfg, const Mapa *mapa) {
    Jogador jogador;
    memset(&jogador, 0, sizeof(jogador));

    jogador.vida = cfg->vida_inicial;
    jogador.energia = cfg->energia_inicial;
    jogador.dinheiro = cfg->dinheiro_inicial;

    jogador.linha = mapa->teleporte_linha;
    jogador.coluna = mapa->teleporte_coluna;

    jogador.num_armas_obtidas = 0;
    jogador.arma_atual = 0;
    jogador_adicionar_arma(&jogador, 2); /* Pistola Laser - equivalente as linhas 330-370 do original */

    /* Linha 27 do original: "LET M=VAL "5"" - a variavel M (medicamentos)
     * comeca em 5 e nunca e' reatribuida antes do jogador nascer na Sala
     * de Teleporte, entao a partida de fato comeca com 5, nao 0. */
    jogador.num_medicamentos = cfg->medicamentos_iniciais;
    jogador.escudo_ligado = false;

    return jogador;
}

bool jogador_adicionar_arma(Jogador *jogador, int id_arma) {
    if (jogador->num_armas_obtidas >= MAX_ARMAS_JOGADOR) {
        return false;
    }
    for (int i = 0; i < jogador->num_armas_obtidas; i++) {
        if (jogador->armas_obtidas[i] == id_arma) {
            return false;
        }
    }
    jogador->armas_obtidas[jogador->num_armas_obtidas] = id_arma;
    jogador->num_armas_obtidas++;
    return true;
}

bool jogador_trocar_arma(Jogador *jogador, int indice_no_inventario) {
    if (indice_no_inventario < 0 || indice_no_inventario >= jogador->num_armas_obtidas) {
        return false;
    }
    jogador->arma_atual = indice_no_inventario;
    return true;
}

bool jogador_usar_medicamento(Jogador *jogador, int vida_maxima) {
    if (jogador->num_medicamentos <= 0 || jogador->vida >= vida_maxima) {
        return false;
    }
    jogador->vida += CURA_MEDICAMENTO_BASE + sorteio_intervalo(CURA_MEDICAMENTO_VARIACAO_MINIMA, CURA_MEDICAMENTO_VARIACAO_MAXIMA);
    if (jogador->vida > vida_maxima) {
        jogador->vida = vida_maxima;
    }
    jogador->num_medicamentos--;
    return true;
}
