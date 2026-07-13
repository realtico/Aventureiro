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
 * Altura do HUD (linhas do topo, incluindo borda). O log ocupa o resto da
 * tela, numa janela ncurses separada com scrollok ligado - assim redesenhar
 * o HUD nunca atropela a rolagem do log, e vice-versa.
 *
 * Pacote 30: o HUD tem 2 variantes de layout - LARGA (2 linhas de conteudo,
 * o formato classico) e ESTREITA (3 linhas com rotulos abreviados, ver
 * ui_desenhar_hud). Qual delas cabe depende so' de COLS (ver
 * LARGURA_HUD_LARGA abaixo), decidido uma vez em recriar_janelas - tanto pra
 * dimensionar janela_hud quanto pra reservar altura_disponivel pro
 * log/mapa.
 */
#define ALTURA_HUD_LARGA 4
#define ALTURA_HUD_ESTREITA 5

/*
 * Largura minima pra manter o HUD classico de 2 linhas sem quebrar sozinho.
 * A segunda linha ("Arma: %-24s  Escudo: %s  Medicamentos: %d") e' a que
 * manda: com o nome de arma mais longo hoje na base ("Pistola Fotônica", 16
 * colunas visiveis - o %-24s so' garante um MINIMO de 24, nomes maiores nao
 * sao truncados), ela ocupa ate 67 colunas visiveis a partir da coluna 2, ou
 * seja precisa de COLS >= 71 pra nao estourar a janela (COLS - 4 >= 67, os 4
 * sao a margem esquerda de 2 + a borda/margem direita de 2, mesma conta de
 * LARGURA_MINIMA_LOG). Isso e' bem mais que os 55/60 cogitados inicialmente
 * pro limiar - aqueles ainda deixariam terminais de 60-70 colunas
 * corrompendo o HUD sem que ninguem notasse, ja que o bug so' foi
 * verificado em 30 e 53 colunas. 72 da uma margem de 1 coluna sobre o pior
 * caso real sem precisar abreviar nome de arma (isso fica pro Pacote 31).
 */
#define LARGURA_HUD_LARGA 72

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

/*
 * Pacote 31: copia 'origem' pra 'destino' truncando pra no maximo
 * 'largura_maxima' colunas visiveis (UTF-8-aware, mesma contagem de
 * largura_visivel_utf8), anexando "…" no lugar do que faltou quando trunca.
 * Defesa pro campo de nome de arma do HUD (ui_desenhar_hud): MAX_NOME
 * permite nomes de ate 47 colunas, bem mais do que qualquer nome real hoje
 * (o mais longo tem 16) - sem isso, um nome de arma futuro mais comprido
 * estouraria janela_hud (sem scrollok, ao contrario de janela_log) e
 * reproduziria a mesma corrupcao que o Pacote 30 corrigiu, so' que disparada
 * por dado em vez de por terminal estreito.
 */
static void truncar_visivel_utf8(char *destino, size_t tamanho_destino, const char *origem, int largura_maxima) {
    if (largura_visivel_utf8(origem) <= largura_maxima || largura_maxima <= 1) {
        snprintf(destino, tamanho_destino, "%s", origem);
        return;
    }

    int largura_alvo = largura_maxima - 1; /* reserva 1 coluna pro "…" */
    int largura_acumulada = 0;
    size_t bytes_copiados = 0;
    for (const unsigned char *p = (const unsigned char *)origem; *p != '\0';) {
        size_t tamanho_char = 1;
        if ((*p & 0xE0) == 0xC0) {
            tamanho_char = 2;
        } else if ((*p & 0xF0) == 0xE0) {
            tamanho_char = 3;
        } else if ((*p & 0xF8) == 0xF0) {
            tamanho_char = 4;
        }
        if (largura_acumulada + 1 > largura_alvo) {
            break;
        }
        largura_acumulada++;
        bytes_copiados += tamanho_char;
        p += tamanho_char;
    }
    if (bytes_copiados >= tamanho_destino) {
        bytes_copiados = tamanho_destino - 1;
    }
    memcpy(destino, origem, bytes_copiados);
    snprintf(destino + bytes_copiados, tamanho_destino - bytes_copiados, "…");
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

    int altura_hud = (COLS < LARGURA_HUD_LARGA) ? ALTURA_HUD_ESTREITA : ALTURA_HUD_LARGA;
    janela_hud = newwin(altura_hud, COLS, 0, 0);

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
    bool cabe_barra = texto_barra != NULL && (LINES - altura_hud - ALTURA_BARRA) >= 1;
    int altura_reservada_barra = cabe_barra ? ALTURA_BARRA : 0;
    int altura_disponivel = LINES - altura_hud - altura_reservada_barra;

    bool cabe_painel = tamanho_mapa > 0 &&
        (COLS - largura_painel) >= LARGURA_MINIMA_LOG &&
        altura_disponivel >= altura_painel;

    int largura_log = cabe_painel ? (COLS - largura_painel) : COLS;
    janela_log = newwin(altura_disponivel, largura_log, altura_hud, 0);
    scrollok(janela_log, TRUE);
    keypad(janela_log, TRUE);

    if (cabe_painel) {
        janela_mapa = newwin(altura_disponivel, largura_painel, altura_hud, largura_log);
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

/*
 * Pacote 31: escreve 'texto' em 'win' quebrando entre palavras na largura
 * atual da janela, em vez de deixar o ncurses quebrar por coluna (sem nocao
 * de palavra - corta qualquer palavra, ou nome de arma/personagem/item, que
 * atravesse a borda direita). janela_log nao tem moldura (ver
 * desenhar_moldura, nunca chamada nela) nem scrollok limita a largura - so'
 * a altura -, entao a largura util e' a largura inteira da janela.
 *
 * Colapsa espacos internos multiplos pra um so' ao remontar as linhas -
 * nenhuma chamada de ui_log no jogo depende de espacamento manual pra
 * alinhar colunas (isso e' so' texto narrativo; alinhamento de campo, tipo
 * o HUD, usa mvwprintw com padding proprio, nao ui_log). Uma string vazia ou
 * so' de espacos (usada como linha em branco separadora, ver
 * game_tela_titulo) ainda produz uma linha em branco - preserva o
 * espacamento entre paragrafos.
 *
 * Uma palavra sozinha mais larga que a janela (pathologico - nao acontece
 * com o texto atual do jogo) nao e' quebrada aqui; sai na propria linha e
 * deixa o ncurses fazer o wrap por coluna soh' pra ela, como fallback -
 * ainda nao corrompe nada (janela_log tem scrollok, ao contrario do HUD do
 * Pacote 30), so' quebra essa palavra especifica no meio.
 */
/*
 * Escreve 'linha' (largura visivel 'largura_linha') em 'win' e avanca pra
 * proxima linha - a NAO ser que 'linha' preencha a janela (largura_linha ==
 * largura_janela) exatamente: nesse caso o proprio ncurses ja fica com o
 * cursor pendente de quebra automatica (auto-wrap) assim que o proximo
 * caractere for escrito, e um '\n' explicito aqui duplicaria o avanco,
 * pulando uma linha em branco - achado ao vivo num terminal de exatamente
 * 53 colunas (Termux), onde uma das mensagens de examinar sala quebrada
 * bate ponta a ponta com a largura da janela.
 */
static void escrever_linha_com_avanco(WINDOW *win, const char *linha, int largura_linha, int largura_janela) {
    waddstr(win, linha);
    if (largura_linha < largura_janela) {
        wclrtoeol(win); /* ver comentario da funcao chamadora sobre linha reaproveitada */
        waddch(win, '\n');
    }
}

static void escrever_com_quebra_de_palavra(WINDOW *win, const char *texto) {
    int largura = getmaxx(win);
    if (largura <= 1) {
        waddstr(win, texto);
        waddch(win, '\n');
        return;
    }

    char linha_atual[1024] = "";
    int largura_atual = 0;

    const char *p = texto;
    while (*p != '\0') {
        while (*p == ' ') {
            p++;
        }
        const char *inicio_palavra = p;
        while (*p != '\0' && *p != ' ') {
            p++;
        }
        size_t tamanho_bytes = (size_t)(p - inicio_palavra);
        if (tamanho_bytes == 0) {
            break;
        }
        char palavra[256];
        if (tamanho_bytes >= sizeof(palavra)) {
            tamanho_bytes = sizeof(palavra) - 1;
        }
        memcpy(palavra, inicio_palavra, tamanho_bytes);
        palavra[tamanho_bytes] = '\0';
        int largura_palavra = largura_visivel_utf8(palavra);

        if (largura_atual > 0 && largura_atual + 1 + largura_palavra > largura) {
            escrever_linha_com_avanco(win, linha_atual, largura_atual, largura);
            linha_atual[0] = '\0';
            largura_atual = 0;
        }
        if (largura_atual > 0) {
            strncat(linha_atual, " ", sizeof(linha_atual) - strlen(linha_atual) - 1);
            largura_atual += 1;
        }
        strncat(linha_atual, palavra, sizeof(linha_atual) - strlen(linha_atual) - 1);
        largura_atual += largura_palavra;
    }

    escrever_linha_com_avanco(win, linha_atual, largura_atual, largura);
}

void ui_log(const char *fmt, ...) {
    char linha[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(linha, sizeof(linha), fmt, args);
    va_end(args);

    escrever_com_quebra_de_palavra(janela_log, linha);

    /*
     * redrawwin() antes do refresh: janela_log nao esta ancorada no topo/
     * base fisico da tela (fica entre o HUD e a barra de comandos), entao
     * quando ela rola por conta propria (scrollok, ao encher de linhas), o
     * ncurses otimiza a saida assumindo que a linha recem-exposta pelo
     * scroll ja esta em branco e nao reenvia o clear-to-EOL - descoberto
     * testando este pacote: confirmado reproduzindo o problema num programa
     * ncurses minimo e alimentando os bytes brutos capturados direto num
     * pyte.Stream isolado (sem pty nenhum envolvido), que mostrou sobra de
     * texto de escritas bem anteriores (de paragrafos diferentes) vazando
     * pra dentro de uma linha nova mais curta. redrawwin() descarta toda
     * informacao de otimizacao da janela e forca reescrita completa no
     * proximo wrefresh, contornando o problema custe o que custar.
     */
    redrawwin(janela_log);
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
    const char *texto_escudo = jogador->escudo_ligado ? "ligado" : "desligado";

    /*
     * Pacote 31: trunca defensivamente pro pior caso da janela mais estreita
     * que cada variante ainda suporta - 20 cabe em ESTREITA ate 30 colunas
     * (disponivel 26 - "Arma: " 6 = 20), 24 casa com o padding de LARGA
     * (%-24s abaixo). Nomes reais de hoje (max 16) nunca sao afetados; ver
     * truncar_visivel_utf8.
     */
    char nome_arma_seguro[MAX_NOME + 4];
    bool hud_estreito = COLS < LARGURA_HUD_LARGA;
    truncar_visivel_utf8(nome_arma_seguro, sizeof(nome_arma_seguro), nome_arma, hud_estreito ? 20 : 24);

    if (hud_estreito) {
        /*
         * Pacote 30: variante ESTREITA, 3 linhas com rotulos abreviados
         * (rotulos de campo do HUD sao chrome de UI, nao "mensagem" -
         * abreviar NOMES de entidade, no log e em qualquer lugar que
         * dependa do wrap do ncurses, e' o Pacote 31). Cabe ate 30 colunas
         * com o pior caso real de hoje (nome de arma de 16 colunas,
         * dinheiro de 6 digitos, "Esc:desligado") - ver conta em
         * LARGURA_HUD_LARGA.
         */
        mvwprintw(janela_hud, 1, 2, "Vida:%d En:%d $:%d",
                  jogador->vida, jogador->energia, jogador->dinheiro);
        mvwprintw(janela_hud, 2, 2, "Arma: %s", nome_arma_seguro);
        mvwprintw(janela_hud, 3, 2, "Esc:%s Med:%d", texto_escudo, jogador->num_medicamentos);
    } else {
        mvwprintw(janela_hud, 1, 2, "Vida: %-4d  Energia: %-4d  Dinheiro: %-6d",
                  jogador->vida, jogador->energia, jogador->dinheiro);
        mvwprintw(janela_hud, 2, 2, "Arma: %-24s  Escudo: %s  Medicamentos: %d",
                  nome_arma_seguro, texto_escudo, jogador->num_medicamentos);
    }

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
        if (tecla == 'm' || tecla == 'M') {
            /* Pacote 30: reintroduz o pseudo-comando de mapa em tela cheia
             * (originalmente Pacote 14, removido no 17 quando o painel
             * lateral virou permanente) como fallback pra quando o painel
             * nao cabe (terminal estreito) - mas fica disponivel sempre,
             * igual 'h'/'H', sem checar largura aqui: nao ha' motivo pra
             * negar a tecla num terminal largo, so' fica redundante la'. */
            return -7;
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

/*
 * Desenha o grid ASCII do mapa (salas/portas/legenda) dentro de 'win', a
 * partir de 'linha_inicial'. Extraida de ui_desenhar_mapa (Pacote 30) pra
 * ser reaproveitada tambem por ui_mostrar_mapa_tela_cheia - a janela e' o
 * unico dado que muda entre painel lateral e overlay de tela cheia, tudo o
 * resto (o que cada celula representa) e' identico.
 */
static void desenhar_grid_mapa(WINDOW *win, int linha_inicial, const Mapa *mapa, const Jogador *jogador) {
    int linha_janela = linha_inicial;
    for (int linha = 0; linha < mapa->tamanho; linha++) {
        /*
         * MAX_SALAS*3+1: cada sala ocupa ate 2 bytes agora (· e × sao UTF-8
         * de 2 bytes, ver abaixo), mais 1 byte de porta Leste/Oeste entre
         * salas adjacentes, mais o terminador - MAX_SALAS*2 (salas) +
         * (MAX_SALAS-1) (portas) + 1 cabe em MAX_SALAS*3+1 com folga.
         */
        char salas[MAX_SALAS * 3 + 1];
        int pos = 0;
        for (int coluna = 0; coluna < mapa->tamanho; coluna++) {
            const Celula *celula = &mapa->celulas[linha][coluna];
            bool eh_teleporte = (linha == mapa->teleporte_linha && coluna == mapa->teleporte_coluna);
            if (linha == jogador->linha && coluna == jogador->coluna) {
                salas[pos++] = '@';
            } else if (eh_teleporte) {
                salas[pos++] = 'o'; /* pad do teleporte - sempre visitada, e' o inicio da partida */
            } else if (celula->examinada) {
                /* × (U+00D7 MULTIPLICATION SIGN, Pacote 30 sidetrack;
                 * criterio trocado no Pacote 33) - sala ja vasculhada com o
                 * comando 8 (celula->examinada, ver combat.c), tenha achado
                 * item ou nao. Antes so' marcava com item_coletado (so'
                 * salas com item ja pego) - sala sem item nunca ganhava a
                 * marca mesmo depois de examinada, e nao dava pra saber se
                 * uma sala 'visitada' foi vasculhada ou so' fugida. Agora
                 * serve de trilha de migalhas independente de haver item.
                 * Centralizado no quadrado do caractere, diferente da letra
                 * latina "x" - se nao ficar distinto o bastante de "@" numa
                 * fonte de terminal especifica, alternativa e' ✕ (U+2715). */
                memcpy(&salas[pos], "×", 2);
                pos += 2;
            } else if (celula->visitada) {
                /* · (U+00B7 MIDDLE DOT, Pacote 30 sidetrack) no lugar do
                 * "." antigo - centralizado verticalmente, glifo padrao com
                 * bom suporte em fontes monoespacadas de terminal. */
                memcpy(&salas[pos], "·", 2);
                pos += 2;
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
        mvwprintw(win, linha_janela++, 2, "%s", salas);

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
            mvwprintw(win, linha_janela++, 2, "%s", portas);
        }
    }

    /* Legenda so' se sobrar espaco vertical - janela pequena (grid grande
     * ou terminal raso) prioriza o grid em si, sem legenda. */
    if (linha_janela + 5 <= getmaxy(win)) {
        mvwprintw(win, linha_janela + 1, 2, "@ você");
        mvwprintw(win, linha_janela + 2, 2, "o teleporte");
        mvwprintw(win, linha_janela + 3, 2, "· visitada");
        mvwprintw(win, linha_janela + 4, 2, "× examinada");
    }
}

void ui_desenhar_mapa(const Mapa *mapa, const Jogador *jogador) {
    verificar_e_aplicar_resize();

    if (janela_mapa == NULL) {
        return; /* terminal pequeno demais pro painel - ver ui_iniciar */
    }

    werase(janela_mapa);
    desenhar_moldura(janela_mapa);
    mvwprintw(janela_mapa, 1, 2, "Mapa");
    desenhar_grid_mapa(janela_mapa, 3, mapa, jogador);
    wrefresh(janela_mapa);
}

/*
 * Pacote 30: mostra o mapa em tela cheia como um overlay temporario -
 * fallback pra quando o painel lateral permanente (janela_mapa) nao cabe no
 * terminal (Pacote 17: cabe_painel falso), mas disponivel sempre (ver
 * ui_ler_comando). Bloqueia ate uma tecla ser apertada, depois fecha o
 * overlay e forca o redesenho das janelas permanentes - sem isso, o texto
 * do overlay ficaria "gravado" na tela por baixo delas, ja' que nenhuma das
 * chamadas de wrefresh seguintes (HUD/mapa no topo do loop) toca as areas de
 * log/barra que o overlay cobriu.
 */
void ui_mostrar_mapa_tela_cheia(const Mapa *mapa, const Jogador *jogador) {
    WINDOW *overlay = newwin(LINES, COLS, 0, 0);
    keypad(overlay, TRUE);
    desenhar_moldura(overlay);

    /*
     * Titulo com 2 variantes (mesmo espirito de escolher_texto_barra): o
     * texto completo (43 colunas) estoura a propria moldura do overlay num
     * terminal de 30 colunas - achado testando manualmente este pacote, o
     * mesmo tipo de vazamento que o resto do Pacote 30 corrige no HUD,
     * desta vez no titulo do overlay.
     */
    const char *titulo_completo = "Mapa (pressione qualquer tecla para voltar)";
    const char *titulo_curto = "Mapa (tecla p/ voltar)";
    int largura_disponivel = COLS - 4;
    const char *titulo = (largura_visivel_utf8(titulo_completo) <= largura_disponivel)
                              ? titulo_completo
                              : (largura_visivel_utf8(titulo_curto) <= largura_disponivel) ? titulo_curto : "Mapa";
    mvwprintw(overlay, 1, 2, "%s", titulo);

    desenhar_grid_mapa(overlay, 3, mapa, jogador);
    wrefresh(overlay);

    wgetch(overlay);
    delwin(overlay);

    if (janela_hud != NULL) {
        touchwin(janela_hud);
        wrefresh(janela_hud);
    }
    if (janela_mapa != NULL) {
        touchwin(janela_mapa);
        wrefresh(janela_mapa);
    }
    if (janela_barra != NULL) {
        touchwin(janela_barra);
        wrefresh(janela_barra);
    }
    if (janela_log != NULL) {
        touchwin(janela_log);
        wrefresh(janela_log);
    }
}
