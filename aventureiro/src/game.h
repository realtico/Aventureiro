#ifndef AVENTUREIRO_GAME_H
#define AVENTUREIRO_GAME_H

#include "types.h"

/* Como uma partida termina - vitoria via teleporte ou morte (vida a
 * zero, seja por acidente no escuro ou contra-ataque de tripulante). */
typedef enum {
    JOGO_FIM_MORTE,
    JOGO_FIM_VITORIA
} FimDeJogo;

/*
 * Tela de titulo (linhas 10-20 do original): historia do jogo + lista dos
 * dez comandos. Bloqueia ate uma tecla ser pressionada.
 */
void game_tela_titulo(void);

/*
 * Loop principal (linha 500 do original): le um comando (0-9) e despacha
 * pro comando correspondente em combat.h, imprimindo as mensagens do
 * Resultado no log e redesenhando o HUD a cada rodada. Comandos que
 * exigem informacao extra da tecla so (mover: direcao N/S/L/O; trocar de
 * arma: indice; comunicar-se: postura S/I/A e valor da oferta; examinar
 * sala escura: E/L/D) pedem essa informacao antes de chamar combat.h.
 * Retorna assim que o jogador morre ou aciona o teleporte com sucesso.
 */
FimDeJogo game_loop(Jogador *jogador, Mapa *mapa, const BaseDeDados *bd, const Config *cfg);

/* Tela de morte (linhas 1610/6660/7070 do original): jogador perdeu tudo. */
void game_tela_morte(void);

/* Tela de vitoria (linha 5560 do original): jogador voltou via teleporte
 * mantendo o que coletou - mostra o saldo final de dinheiro e armas. */
void game_tela_vitoria(const Jogador *jogador, const BaseDeDados *bd);

#endif
