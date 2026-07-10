#include "ui.h"

#include <langinfo.h>
#include <locale.h>
#include <ncurses.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/*
 * Altura fixa do HUD (linhas do topo). O log ocupa o resto da tela, numa
 * janela ncurses separada com scrollok ligado - assim redesenhar o HUD
 * nunca atropela a rolagem do log, e vice-versa.
 */
#define ALTURA_HUD 4

/*
 * Largura minima reservada pro log (Pacote 17) - abaixo disso o painel de
 * mapa nao vale a pena mesmo que caiba fisicamente, o log ficaria estreito
 * demais pra ler a narracao. Puramente uma escolha de legibilidade, nao um
 * limite tecnico do ncurses.
 */
#define LARGURA_MINIMA_LOG 40

/* Altura fixa da barra de comandos permanente (Pacote 26) - uma linha simples
 * na base da tela, sem moldura (desenhar_moldura precisa de pelo menos 2
 * linhas pra ter borda superior/inferior). */
#define ALTURA_BARRA 1

static WINDOW *janela_hud = NULL;
static WINDOW *janela_log = NULL;
/* Painel de mapa permanente (Pacote 17). NULL se o terminal for pequeno
 * demais pra caber (ver ui_iniciar) - ui_desenhar_mapa() checa isso e vira
 * no-op nesse caso, o resto do jogo funciona normalmente sem o painel. */
static WINDOW *janela_mapa = NULL;
/* Barra de comandos permanente (Pacote 26). NULL se o terminal for pequeno
 * demais pra caber nenhuma das 3 variantes de texto (ver escolher_texto_barra
 * e ui_iniciar) - mesma filosofia do painel de mapa, vira no-op silencioso. */
static WINDOW *janela_barra = NULL;

/* Pacote 28: tamanho do mapa guardado pra poder recriar as janelas depois de
 * um resize (verificar_e_aplicar_resize), sem precisar mudar a assinatura de
 * ui_iniciar. ultima_altura/ultima_largura comecam em -1 (nunca visto ainda)
 * so' pra garantir que o primeiro ioctl encontre "mudou", mas na pratica
 * ui_iniciar ja os inicializa com o tamanho real antes de qualquer redraw. */
static int mapa_tamanho_atual = 0;
static int ultima_altura = -1;
static int ultima_largura = -1;

/*
 * 3 niveis de detalhe pra barra de comandos, do mais legivel pro mais
 * compacto - escolhido conforme a largura do terminal (ui_iniciar), mesmo
 * espirito do painel de mapa (Pacote 17) que soma/some conforme o espaco.
 * BARRA_COMPLETA tem 90 colunas visiveis - mais que os 80 classicos, entao
 * cai pra BARRA_ABREVIADA (49 colunas) em terminais mais estreitos.
 */
static const char *BARRA_COMPLETA =
    "0:Mover 1:Atacar 2:Fugir 3:Arma 4:Falar 5:Escudo 6:Remédio 7:Status 8:Examinar 9:Teleporte";
static const char *BARRA_ABREVIADA =
    "0-Mv 1-At 2-Fg 3-Ar 4-Fl 5-Es 6-Rm 7-St 8-Ex 9-Tp (H=ajuda)";
static const char *BARRA_MINIMA = "0123456789 (H=ajuda)";

/* Conta colunas visiveis, nao bytes - acentos UTF-8 (ex. "Remédio") ocupam 2
 * bytes mas 1 coluna; contar bytes superestimaria a largura e rejeitaria uma
 * variante que caberia perfeitamente na tela. */
static int largura_visivel_utf8(const char *s) {
    int n = 0;
    for (const unsigned char *p = (const unsigned char *)s; *p != '\0'; p++) {
        if ((*p & 0xC0) != 0x80) {
            n++;
        }
    }
    return n;
}

static const char *escolher_texto_barra(int largura_disponivel) {
    if (largura_visivel_utf8(BARRA_COMPLETA) <= largura_disponivel) {
        return BARRA_COMPLETA;
    }
    if (largura_visivel_utf8(BARRA_ABREVIADA) <= largura_disponivel) {
        return BARRA_ABREVIADA;
    }
    if (largura_visivel_utf8(BARRA_MINIMA) <= largura_disponivel) {
        return BARRA_MINIMA;
    }
    return NULL; /* terminal estreito demais pra qualquer variante */
}

/*
 * Pacote 16: setlocale(LC_ALL, "") so' resolve pra UTF-8 se o AMBIENTE ja'
 * tiver uma locale .UTF-8 exportada (LANG/LC_ALL/LC_CTYPE) - ela nao forca
 * UTF-8, so' pergunta ao SO qual locale usar. Se o ambiente falhar, tenta
 * "C.UTF-8" explicitamente - locale UTF-8 minima que a glibc traz pronta em
 * praticamente toda distro Linux moderna, sem precisar de locale-gen nem de
 * nada exportado pelo usuario.
 *
 * Isso resolve so' metade do bug, e por si so' NAO era suficiente: em
 * Linux/Ubuntu (nativo ou WSL), mesmo com LANG=en_US.UTF-8 corretamente
 * setado no ambiente, o texto acentuado ainda saia como "DepM-CM-3sito" em
 * vez de "Depósito". Causa real, confirmada reproduzindo o bug neste
 * repositorio: o Makefile linkava contra a libncurses "narrow" (8-bit, via
 * `pkg-config ncurses`), que trata cada BYTE de uma sequencia UTF-8
 * multi-byte como uma celula separada, independente da locale do processo -
 * so' a variante "wide" (`libncursesw`, `pkg-config ncursesw`) sabe
 * combinar bytes multi-byte num unico caractere. No macOS a libncurses do
 * sistema e' sempre wide por baixo dos panos, entao o bug nunca apareceu
 * la' mesmo com o link "errado". A correcao ficou no Makefile (usar
 * `ncursesw`); garantir_locale_utf8() continua necessaria como a outra
 * metade (garantir que a locale seja UTF-8 pra libncursesw ter o que
 * decodificar).
 */
static void garantir_locale_utf8(void) {
    setlocale(LC_ALL, "");
    const char *codeset = nl_langinfo(CODESET);
    if (codeset != NULL && strcmp(codeset, "UTF-8") == 0) {
        return;
    }
    setlocale(LC_ALL, "C.UTF-8");
}

static void destruir_janelas(void) {
    if (janela_hud != NULL) {
        delwin(janela_hud);
        janela_hud = NULL;
    }
    if (janela_log != NULL) {
        delwin(janela_log);
        janela_log = NULL;
    }
    if (janela_mapa != NULL) {
        delwin(janela_mapa);
        janela_mapa = NULL;
    }
    if (janela_barra != NULL) {
        delwin(janela_barra);
        janela_barra = NULL;
    }
}

/*
 * Cria (ou recria, apos um resize - Pacote 28) as 4 janelas com base no
 * COLS/LINES atuais do ncurses. Extraida de ui_iniciar pra ser reaproveitada
 * por verificar_e_aplicar_resize sem duplicar a logica de layout.
 */
static void recriar_janelas(int tamanho_mapa) {
    destruir_janelas();
    mapa_tamanho_atual = tamanho_mapa;

    janela_hud = newwin(ALTURA_HUD, COLS, 0, 0);

    /*
     * Painel de mapa (Pacote 17): grid de 'tamanho_mapa' salas por lado
     * precisa de (2*tamanho-1) colunas/linhas pro conteudo (sala+porta
     * alternados, ver ui_desenhar_mapa) - mais 2 colunas/linhas de borda
     * (box()) e 2 linhas de titulo/respiro no topo. So' cria o painel se
     * sobrar largura minima decente pro log ao lado (LARGURA_MINIMA_LOG) e
     * altura suficiente pro grid inteiro - senao fica so' HUD+log, iguais
     * a antes do Pacote 17, sem corromper a tela num terminal pequeno.
     */
    int largura_grid = 2 * tamanho_mapa - 1;
    int altura_grid = 2 * tamanho_mapa - 1;
    int largura_painel = largura_grid + 4;   /* borda (2) + respiro (2) */
    int altura_painel = altura_grid + 4;     /* borda (2) + titulo (1) + respiro (1) */

    /*
     * Barra de comandos (Pacote 26): reserva 1 linha na base da tela, se
     * houver alguma das 3 variantes de texto que caiba na largura e ainda
     * sobrar pelo menos 1 linha pro log depois de descontar HUD+barra. Nao
     * corrompe a tela em terminal pequeno demais - so' deixa de aparecer,
     * mesma filosofia do painel de mapa logo abaixo.
     */
    const char *texto_barra = escolher_texto_barra(COLS);
    bool cabe_barra = texto_barra != NULL && (LINES - ALTURA_HUD - ALTURA_BARRA) >= 1;
    int altura_reservada_barra = cabe_barra ? ALTURA_BARRA : 0;
    int altura_disponivel = LINES - ALTURA_HUD - altura_reservada_barra;

    bool cabe_painel = tamanho_mapa > 0 &&
        (COLS - largura_painel) >= LARGURA_MINIMA_LOG &&
        altura_disponivel >= altura_painel;

    int largura_log = cabe_painel ? (COLS - largura_painel) : COLS;
    janela_log = newwin(altura_disponivel, largura_log, ALTURA_HUD, 0);
    scrollok(janela_log, TRUE);
    keypad(janela_log, TRUE);

    if (cabe_painel) {
        janela_mapa = newwin(altura_disponivel, largura_painel, ALTURA_HUD, largura_log);
    }

    if (cabe_barra) {
        janela_barra = newwin(ALTURA_BARRA, COLS, LINES - ALTURA_BARRA, 0);
        mvwprintw(janela_barra, 0, 0, "%s", texto_barra);
        wrefresh(janela_barra);
    }
}

/*
 * Pacote 28: o SIGWINCH do ncurses so' atualiza LINES/COLS na proxima
 * chamada de wgetch() (nao no instante do sinal em si, ver handover) - por
 * isso le o tamanho REAL do terminal direto via ioctl(TIOCGWINSZ), sem
 * depender do LINES/COLS do ncurses, que podem estar desatualizados nesse
 * meio-tempo. Chamada no inicio dos redraws (ui_desenhar_hud/
 * ui_desenhar_mapa), que ja rodam sem condicao a cada turno (Pacote
 * 17/26) - se o tamanho real mudou desde a ultima vez, sincroniza o ncurses
 * (resizeterm) e recria as 4 janelas com o layout novo. Descarta o
 * scrollback do log acumulado (nao ha como "reimprimir" o que ja rolou) -
 * aceitavel aqui, o log ja e' limpo a cada comando mesmo (ui_limpar_log).
 */
static void verificar_e_aplicar_resize(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
        return;
    }
    if (ws.ws_row == ultima_altura && ws.ws_col == ultima_largura) {
        return;
    }
    resizeterm(ws.ws_row, ws.ws_col);
    ultima_altura = ws.ws_row;
    ultima_largura = ws.ws_col;
    recriar_janelas(mapa_tamanho_atual);
}

void ui_iniciar(int tamanho_mapa) {
    /*
     * garantir_locale_utf8() cobre a metade "locale" do bug do Pacote 16
     * (ver comentario acima da funcao). A outra metade - libncurses
     * "narrow" vs "wide" - e' resolvida no CMakeLists.txt (link contra
     * ncursesw), nao aqui.
     */
    garantir_locale_utf8();
    initscr();
    cbreak();     /* le tecla sem esperar Enter, mas deixa Ctrl-C funcionando (raw() desligaria isso) */
    noecho();     /* nao ecoa a tecla digitada - o jogo controla o que aparece na tela */
    keypad(stdscr, TRUE);
    curs_set(0);  /* sem cursor piscando - nao ha campo de texto livre, so leitura de digito */

    ultima_altura = LINES;
    ultima_largura = COLS;
    recriar_janelas(tamanho_mapa);
}

void ui_encerrar(void) {
    destruir_janelas();
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

/*
 * Moldura em semigraficos Unicode simples (─│┌┐└┘, estilo TUI classico
 * tipo Clipper/dBase) em vez de box() do ncurses: box() usa o alternate
 * character set do terminfo pros cantos, que alguns terminais/emuladores
 * (e o pyte usado pra verificacao automatizada, Pacote 17) nao traduzem
 * direito, sobrando lixo tipo 'l'/'q'/'k' na tela. Caracteres UTF-8
 * literais funcionam pelo mesmo mecanismo que ja garante os acentos
 * (Pacote 16: locale UTF-8 + libncursesw), sem depender de terminfo nenhum
 * - por isso usa mvwprintw (nao mvwaddch, que e' byte-a-byte e corromperia
 * um caractere multi-byte).
 */
static void desenhar_moldura(WINDOW *win) {
    int altura, largura;
    getmaxyx(win, altura, largura);
    if (altura < 2 || largura < 2) {
        return;
    }

    char linha[512];
    int repeticoes = largura - 2;
    int max_repeticoes = (int)(sizeof(linha) / 3) - 4; /* "─" ocupa 3 bytes em UTF-8 */
    if (repeticoes > max_repeticoes) {
        repeticoes = max_repeticoes;
    }

    int pos = snprintf(linha, sizeof(linha), "┌");
    for (int i = 0; i < repeticoes; i++) {
        pos += snprintf(linha + pos, sizeof(linha) - (size_t)pos, "─");
    }
    snprintf(linha + pos, sizeof(linha) - (size_t)pos, "┐");
    mvwprintw(win, 0, 0, "%s", linha);

    pos = snprintf(linha, sizeof(linha), "└");
    for (int i = 0; i < repeticoes; i++) {
        pos += snprintf(linha + pos, sizeof(linha) - (size_t)pos, "─");
    }
    snprintf(linha + pos, sizeof(linha) - (size_t)pos, "┘");
    mvwprintw(win, altura - 1, 0, "%s", linha);

    for (int y = 1; y < altura - 1; y++) {
        mvwprintw(win, y, 0, "│");
        mvwprintw(win, y, largura - 1, "│");
    }
}

void ui_desenhar_hud(const Jogador *jogador, const BaseDeDados *bd) {
    verificar_e_aplicar_resize();

    werase(janela_hud);
    desenhar_moldura(janela_hud);

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
        if (tecla == '\f') { /* Ctrl-L, Pacote 28: convencao classica de terminal pra
                               * "redesenhar tela" - forca a checagem de resize do
                               * topo do loop sem esperar o proximo comando real. */
            return -2;
        }
        /*
         * Atalho de movimento por seta (Pacote 18): equivalente a "0" + a
         * direcao, num unico toque, pulando o prompt "para que lado".
         * KEY_UP/DOWN/LEFT/RIGHT ja chegam aqui porque keypad() esta ligado
         * em janela_log (ui_iniciar). Mapeamento combina com a orientacao
         * do painel de mapa (Pacote 17): Norte sobe uma linha na tela,
         * Leste anda pra direita - ver DELTA_LINHA/DELTA_COLUNA em
         * combat.c.
         */
        switch (tecla) {
            case KEY_UP:    return -3; /* Norte */
            case KEY_DOWN:  return -4; /* Sul */
            case KEY_RIGHT: return -5; /* Leste */
            case KEY_LEFT:  return -6; /* Oeste */
            default: break;
        }
    } while (tecla < '0' || tecla > '9');
    return tecla - '0';
}

int ui_aguardar_tecla(void) {
    return wgetch(janela_log);
}

void ui_pausar_dramatico(void) {
    static int sem_pausas = -1; /* -1 = ainda nao checado, so' le getenv uma vez */
    if (sem_pausas == -1) {
        sem_pausas = (getenv("AVENTUREIRO_SEM_PAUSAS") != NULL) ? 1 : 0;
    }
    if (!sem_pausas) {
        napms(1000);
    }
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

void ui_desenhar_mapa(const Mapa *mapa, const Jogador *jogador) {
    verificar_e_aplicar_resize();

    if (janela_mapa == NULL) {
        return; /* terminal pequeno demais pro painel - ver ui_iniciar */
    }

    werase(janela_mapa);
    desenhar_moldura(janela_mapa);
    mvwprintw(janela_mapa, 1, 2, "Mapa");

    int linha_janela = 3;
    for (int linha = 0; linha < mapa->tamanho; linha++) {
        char salas[MAX_SALAS * 2 + 1];
        int pos = 0;
        for (int coluna = 0; coluna < mapa->tamanho; coluna++) {
            const Celula *celula = &mapa->celulas[linha][coluna];
            bool eh_teleporte = (linha == mapa->teleporte_linha && coluna == mapa->teleporte_coluna);
            if (linha == jogador->linha && coluna == jogador->coluna) {
                salas[pos++] = '@';
            } else if (eh_teleporte) {
                salas[pos++] = 'o'; /* pad do teleporte - sempre visitada, e' o inicio da partida */
            } else if (celula->visitada) {
                salas[pos++] = '.';
            } else {
                salas[pos++] = ' ';
            }

            if (coluna < mapa->tamanho - 1) {
                /* Porta Leste/Oeste so aparece se pelo menos um dos dois
                 * lados ja foi visitado - conectada[] e' simetrico entre
                 * vizinhos (map.c), entao tanto faz qual lado sabe dela. */
                bool porta_conhecida = celula->visitada || mapa->celulas[linha][coluna + 1].visitada;
                salas[pos++] = (porta_conhecida && celula->conectada[LESTE]) ? '-' : ' ';
            }
        }
        salas[pos] = '\0';
        mvwprintw(janela_mapa, linha_janela++, 2, "%s", salas);

        if (linha < mapa->tamanho - 1) {
            char portas[MAX_SALAS * 2 + 1];
            pos = 0;
            for (int coluna = 0; coluna < mapa->tamanho; coluna++) {
                const Celula *celula = &mapa->celulas[linha][coluna];
                bool porta_conhecida = celula->visitada || mapa->celulas[linha + 1][coluna].visitada;
                portas[pos++] = (porta_conhecida && celula->conectada[SUL]) ? '|' : ' ';
                if (coluna < mapa->tamanho - 1) {
                    portas[pos++] = ' ';
                }
            }
            portas[pos] = '\0';
            mvwprintw(janela_mapa, linha_janela++, 2, "%s", portas);
        }
    }

    /* Legenda so' se sobrar espaco vertical - painel pequeno (grid grande
     * ou terminal raso) prioriza o grid em si, sem legenda. */
    if (linha_janela + 4 <= getmaxy(janela_mapa)) {
        mvwprintw(janela_mapa, linha_janela + 1, 2, "@ você");
        mvwprintw(janela_mapa, linha_janela + 2, 2, "o teleporte");
        mvwprintw(janela_mapa, linha_janela + 3, 2, ". visitada");
    }

    wrefresh(janela_mapa);
}
