#include "ui.h"

#include <ncurses.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Altura fixa do HUD (linhas do topo). O log ocupa o resto da tela, numa
 * janela ncurses separada com scrollok ligado - assim redesenhar o HUD
 * nunca atropela a rolagem do log, e vice-versa.
 */
#define ALTURA_HUD 4

static WINDOW *janela_hud = NULL;
static WINDOW *janela_log = NULL;

void ui_iniciar(void) {
    initscr();
    cbreak();     /* le tecla sem esperar Enter, mas deixa Ctrl-C funcionando (raw() desligaria isso) */
    noecho();     /* nao ecoa a tecla digitada - o jogo controla o que aparece na tela */
    keypad(stdscr, TRUE);
    curs_set(0);  /* sem cursor piscando - nao ha campo de texto livre, so leitura de digito */

    janela_hud = newwin(ALTURA_HUD, COLS, 0, 0);
    janela_log = newwin(LINES - ALTURA_HUD, COLS, ALTURA_HUD, 0);
    scrollok(janela_log, TRUE);
    keypad(janela_log, TRUE);
}

void ui_encerrar(void) {
    if (janela_hud != NULL) {
        delwin(janela_hud);
        janela_hud = NULL;
    }
    if (janela_log != NULL) {
        delwin(janela_log);
        janela_log = NULL;
    }
    endwin();
}

void ui_log(const char *fmt, ...) {
    char linha[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(linha, sizeof(linha), fmt, args);
    va_end(args);

    waddstr(janela_log, linha);
    waddch(janela_log, '\n');
    wrefresh(janela_log);
}

void ui_limpar_log(void) {
    werase(janela_log);
    wrefresh(janela_log);
}

void ui_desenhar_hud(const Jogador *jogador, const BaseDeDados *bd) {
    werase(janela_hud);
    box(janela_hud, 0, 0);

    const char *nome_arma = "-";
    if (jogador->arma_atual >= 0 && jogador->arma_atual < jogador->num_armas_obtidas) {
        int id_arma = jogador->armas_obtidas[jogador->arma_atual];
        if (id_arma >= 0 && id_arma < bd->num_armas) {
            nome_arma = bd->armas[id_arma].nome;
        }
    }

    mvwprintw(janela_hud, 1, 2, "Vida: %-4d  Energia: %-4d  Dinheiro: %-6d",
              jogador->vida, jogador->energia, jogador->dinheiro);
    mvwprintw(janela_hud, 2, 2, "Arma: %-24s  Escudo: %s  Medicamentos: %d",
              nome_arma,
              jogador->escudo_ligado ? "ligado" : "desligado",
              jogador->num_medicamentos);

    wrefresh(janela_hud);
}

int ui_ler_comando(void) {
    int tecla;
    do {
        tecla = wgetch(janela_log);
        if (tecla == 'h' || tecla == 'H') {
            return -1; /* pseudo-comando de ajuda, Pacote 11 */
        }
    } while (tecla < '0' || tecla > '9');
    return tecla - '0';
}

int ui_aguardar_tecla(void) {
    return wgetch(janela_log);
}

int ui_ler_numero(void) {
    char buf[16] = {0};
    echo();
    curs_set(1);
    wgetnstr(janela_log, buf, (int)sizeof(buf) - 1);
    noecho();
    curs_set(0);
    return atoi(buf);
}
