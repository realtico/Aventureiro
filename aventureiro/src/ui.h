#ifndef AVENTUREIRO_UI_H
#define AVENTUREIRO_UI_H

#include "types.h"

/*
 * Unico modulo que sabe que ncurses existe (secao 6 do handover) - o resto
 * do jogo so troca Resultado/strings com este header, entao poderia em
 * teoria ser substituido por um front-end grafico sem tocar em
 * combat.c/game.c.
 */

/* Inicia o ncurses (raw mode, sem echo, cursor visivel so pro prompt de
 * comando). Chamar uma vez, antes de qualquer outra funcao deste modulo. */
void ui_iniciar(void);

/* Restaura o terminal ao estado normal (endwin). Chamar sempre antes de
 * main.c terminar, inclusive em caminhos de erro - senao o shell do
 * usuario fica com echo desligado depois do jogo fechar. */
void ui_encerrar(void);

/*
 * Adiciona uma linha ao log de mensagens rolante (narracao dos comandos,
 * vinda de Resultado::mensagens em combat.h) e redesenha a tela. Aceita
 * printf-style format string.
 */
void ui_log(const char *fmt, ...);

/* Limpa o log de mensagens acumulado (usado ao entrar em telas novas,
 * como titulo/morte/vitoria, para nao misturar narracao de contextos
 * diferentes). */
void ui_limpar_log(void);

/*
 * Desenha o HUD fixo no topo da tela (vida/energia/dinheiro/arma atual) a
 * partir do estado do jogador. 'bd' resolve o nome da arma atual (indice
 * em Jogador::armas_obtidas aponta para BaseDeDados::armas).
 */
void ui_desenhar_hud(const Jogador *jogador, const BaseDeDados *bd);

/*
 * Bloqueia ate o jogador apertar uma tecla numerica de 0 a 9 (os dez
 * comandos, secao 2.2 do handover) e retorna esse digito como int. Tambem
 * aceita 'h'/'H', retornando -1, como pseudo-comando de ajuda (Pacote 11)
 * que o chamador deve tratar sem consumir uma rodada. Teclas fora desse
 * conjunto sao ignoradas silenciosamente - o loop interno so retorna
 * quando le algo valido.
 */
int ui_ler_comando(void);

/* Pausa universal "pressione uma tecla" entre eventos (linha 8000 do
 * original) - aceita qualquer tecla, nao so 0-9. Retorna o codigo da tecla
 * lida (util para o chamador tratar teclas especiais, ex.: 'q' para sair
 * no smoke test isolado deste modulo). */
int ui_aguardar_tecla(void);

/*
 * Le uma linha digitada (com eco e cursor visivel, ao contrario do resto
 * do jogo) e devolve o inteiro correspondente via atoi. Usada so pelo
 * comando Comunicar-se/Subornar (linha 3140 do original: INPUT X), unico
 * ponto do jogo que pede um valor numerico livre em vez de uma tecla.
 */
int ui_ler_numero(void);

#endif
