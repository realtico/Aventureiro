#include "player.h"

#include <string.h>

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

    jogador.tem_medicamento = false;
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
    if (!jogador->tem_medicamento || jogador->vida >= vida_maxima) {
        return false;
    }
    jogador->vida = vida_maxima;
    jogador->tem_medicamento = false;
    return true;
}
