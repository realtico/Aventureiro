#ifndef AVENTUREIRO_UI_H
#define AVENTUREIRO_UI_H

#include "types.h"

/*
 * Unico modulo que sabe que ncurses existe (secao 6 do handover) - o resto
 * do jogo so troca Resultado/strings com este header, entao poderia em
 * teoria ser substituido por um front-end grafico sem tocar em
 * combat.c/game.c.
 */

/*
 * Inicia o ncurses (raw mode, sem echo, cursor visivel so pro prompt de
 * comando). Chamar uma vez, antes de qualquer outra funcao deste modulo.
 *
 * 'tamanho_mapa' e' o lado do grid (Mapa::tamanho, ja conhecido em main.c
 * antes de ui_iniciar - o mapa e' gerado antes da UI) - usado pra calcular
 * se cabe um painel de mapa permanente (Pacote 17) ao lado do log, dado o
 * tamanho atual do terminal. Se nao couber, ui_desenhar_mapa() vira no-op
 * silencioso em vez de corromper a tela.
 */
void ui_iniciar(int tamanho_mapa);

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
 * aceita 'h'/'H' (retorna -1, pseudo-comando de ajuda, Pacote 11), que o
 * chamador deve tratar sem consumir uma rodada. Teclas fora desse conjunto
 * sao ignoradas silenciosamente - o loop interno so retorna quando le algo
 * valido.
 *
 * (O pseudo-comando de mapa 'm'/'M', Pacote 14, foi removido no Pacote 17
 * quando o mapa virou painel lateral sempre visivel, e voltou no Pacote 30 -
 * retorna -7 - como fallback pra terminal estreito onde o painel nao cabe
 * (cabe_painel falso, ver ui.c); disponivel sempre, igual 'h'/'H', mesmo em
 * terminal largo onde e' so' redundante com o painel. O chamador deve tratar
 * como pseudo-comando tambem, chamando ui_mostrar_mapa_tela_cheia().)
 *
 * As setas do teclado (Pacote 18) sao um atalho pro comando Mover direto
 * numa direcao, pulando o prompt "para que lado": KEY_UP retorna -3
 * (Norte), KEY_DOWN -4 (Sul), KEY_RIGHT -5 (Leste), KEY_LEFT -6 (Oeste). O
 * chamador trata isso chamando comando_mover() direto com a Direcao
 * correspondente, sem passar por comando_mover_interativo.
 */
int ui_ler_comando(void);

/* Pausa universal "pressione uma tecla" entre eventos (linha 8000 do
 * original) - aceita qualquer tecla, nao so 0-9. Retorna o codigo da tecla
 * lida (util para o chamador tratar teclas especiais, ex.: 'q' para sair
 * no smoke test isolado deste modulo). */
int ui_aguardar_tecla(void);

/*
 * Pausa dramatica de ~1s (Pacote 20) - chamada por narrar() (game.c) entre
 * mensagens marcadas com Resultado::pausa_apos, pra recriar as pausas do
 * original durante Examinar e a apresentacao de tripulante (ver
 * management/backlog/20-pausas-dramaticas.md). Desligada (vira no-op) se a
 * variavel de ambiente AVENTUREIRO_SEM_PAUSAS estiver definida com
 * qualquer valor - usado por tests/smoke_test.py pra nao estourar o
 * timeout do fuzzer com ~1-2s a mais por Examinar/apresentacao.
 */
void ui_pausar_dramatico(void);

/*
 * Le uma linha digitada (com eco e cursor visivel, ao contrario do resto
 * do jogo) e devolve o inteiro correspondente via atoi. Usada so pelo
 * comando Comunicar-se/Subornar (linha 3140 do original: INPUT X), unico
 * ponto do jogo que pede um valor numerico livre em vez de uma tecla.
 */
int ui_ler_numero(void);

/*
 * Redesenha o painel de mapa permanente (Pacote 17, evoluindo o mapa ASCII
 * do Pacote 14): grid_size x grid_size, marcando a posicao atual do
 * jogador ('@'), o pad da Sala de Teleporte ('o' - sempre visivel, e' onde
 * a partida comeca e termina), salas ja visitadas ('.') e as portas
 * descobertas entre salas onde pelo menos um dos dois lados ja foi
 * visitado - nunca revela tipo/conteudo de sala nao visitada, nem portas
 * de salas totalmente inexploradas (mesma regra de fidelidade do Pacote
 * 14). Chamar a cada mudanca de estado relevante (o chamador nao precisa
 * saber se 'visitada' mudou de fato - redesenhar e' barato e idempotente).
 * Desenha na janela dedicada do painel, nao no log - se o terminal for
 * pequeno demais pro painel caber (decidido em ui_iniciar), e' um no-op
 * silencioso.
 */
void ui_desenhar_mapa(const Mapa *mapa, const Jogador *jogador);

/*
 * Pacote 30: mostra o mesmo grid de ui_desenhar_mapa em tela cheia, como
 * overlay temporario - fallback pro pseudo-comando 'm'/'M' (ui_ler_comando,
 * retorna -7) quando o painel lateral permanente nao cabe no terminal
 * (cabe_painel falso em recriar_janelas). Bloqueia ate qualquer tecla ser
 * apertada, depois restaura as janelas permanentes na tela - o chamador nao
 * precisa fazer nada alem de chamar isto e continuar o loop (nao consome
 * rodada, mesmo espirito de mostrar_ajuda() em game.c).
 */
void ui_mostrar_mapa_tela_cheia(const Mapa *mapa, const Jogador *jogador);

#endif
