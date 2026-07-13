#ifndef AVENTUREIRO_PLAYER_H
#define AVENTUREIRO_PLAYER_H

#include <stdbool.h>

#include "types.h"

/*
 * Cria o jogador no inicio de uma partida: vida/energia/dinheiro de
 * config.json, posicionado na Sala de Teleporte do mapa (e' onde ele
 * nasce e para onde precisa voltar para "vencer", secao 2.4 do handover).
 * Comeca com a arma id 2 (Pistola Laser) no inventario, exatamente como o
 * original (linhas 330-370: T$="2", W$="PISTOLA LASER").
 */
Jogador jogador_iniciar(const Config *cfg, const Mapa *mapa);

/*
 * Adiciona 'id_arma' (indice em BaseDeDados::armas) ao inventario do
 * jogador. Retorna false sem alterar nada se a arma ja estiver no
 * inventario ou se o inventario estiver cheio (MAX_ARMAS_JOGADOR).
 */
bool jogador_adicionar_arma(Jogador *jogador, int id_arma);

/*
 * Troca a arma em uso para a de indice 'indice_no_inventario' dentro de
 * Jogador::armas_obtidas (nao e' o id da arma em BaseDeDados). Retorna
 * false sem alterar nada se o indice estiver fora do inventario atual.
 * Equivalente a linha 2502 do original.
 */
bool jogador_trocar_arma(Jogador *jogador, int indice_no_inventario);

/*
 * Usa um medicamento do jogador (se tiver algum e nao estiver com vida
 * cheia - linha 4010/4040 do original). Cura 7 + [1,3] pontos (linha 4070:
 * "BP=BP+7+INT(RND*3+1)"), nao a vida cheia de uma vez, respeitando o teto
 * 'vida_maxima' (linha 4080: "IF BP>20 THEN LET BP=20"). Consome um
 * medicamento do contador (linha 4095: "M=M-1"). Retorna true se de fato
 * curou.
 */
bool jogador_usar_medicamento(Jogador *jogador, int vida_maxima);

#endif
