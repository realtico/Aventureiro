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
 * Usa o medicamento do jogador, recuperando vida ate 'vida_maxima'.
 * Equivalente a linha 4010 do original: so tem efeito (e so consome o
 * medicamento) se o jogador tiver um E nao estiver com vida cheia.
 * Retorna true se de fato curou.
 */
bool jogador_usar_medicamento(Jogador *jogador, int vida_maxima);

#endif
