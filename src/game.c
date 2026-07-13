#include "game.h"

#include <string.h>

#include "combat.h"
#include "ui.h"

void game_tela_titulo(void) {
    ui_limpar_log();
    ui_log("::O AVENTUREIRO::");
    ui_log("Em busca de aventuras, você se teleporta clandestinamente a bordo de uma");
    ui_log("nave cargueira cuja tripulação é formada por seres exóticos de diversos");
    ui_log("planetas. Seu objetivo é acumular todo o dinheiro que você conseguir,");
    ui_log("vasculhando as salas da nave. Cuidado, a tripulação poderá ter as mais");
    ui_log("diversas reações, e até por sua vida em perigo. Para sobreviver você");
    ui_log("dispõe de uma lanterna, uma pistola laser, um escudo e de \"Twin\", seu");
    ui_log("tradutor e conselheiro eletrônico.");
    ui_log(" ");
    ui_log("Você começa com o nível máximo de energia vital e de energia.");
    ui_log(" ");
    ui_log("Existem 10 comandos - a barra na base da tela sempre mostra um resumo deles.");
    ui_log("Aperte H a qualquer momento pra ver a lista completa, por extenso.");
    ui_log(" ");
    ui_log("Sempre que você utilizar algum dos seus equipamentos (lanterna, escudo");
    ui_log("ou arma) haverá um gasto nas suas reservas de energia. Para terminar o");
    ui_log("jogo você deve retornar à Sala de Teleporte e acionar o teleporte (9).");
    ui_log(" ");
    ui_log("Boa sorte... (pressione uma tecla para começar)");
    ui_aguardar_tecla();
    ui_limpar_log();
}

/* Le uma tecla ate que corresponda a uma das opcoes em 'teclas' (string
 * com um caractere por opcao aceita, maiusculo) e devolve o indice dela
 * dentro de 'teclas'. Usada pelas escolhas de direcao (N/S/L/O), postura
 * de comunicacao (S/I/A) e sala escura (E/L/D) - todas leem uma unica
 * tecla dentre um conjunto pequeno, igual ao original. */
static int ler_opcao(const char *teclas) {
    for (;;) {
        int tecla = ui_aguardar_tecla();
        if (tecla >= 'a' && tecla <= 'z') {
            tecla = tecla - 'a' + 'A';
        }
        for (int i = 0; teclas[i] != '\0'; i++) {
            if (tecla == teclas[i]) {
                return i;
            }
        }
    }
}

static void narrar(const Resultado *res) {
    for (int i = 0; i < res->num_mensagens; i++) {
        ui_log("%s", res->mensagens[i]);
        if (res->pausa_apos[i]) {
            ui_pausar_dramatico(); /* Pacote 20 */
        }
    }
}

/*
 * Mover (linha 1004): so oferece e aceita as direcoes com porta de fato na
 * sala atual (Celula::conectada[]) - antes o jogo aceitava as quatro
 * teclas N/S/L/O e so descobria a falta de porta depois, na mensagem de
 * "Nao ha saida pelo ..." de combat.c (Pacote 11). O gerador de mapa
 * (map.c) garante conectividade total, entao ha sempre pelo menos uma
 * direcao disponivel - 'ler_opcao' nunca recebe uma string vazia aqui.
 */
static Resultado comando_mover_interativo(Jogador *jogador, Mapa *mapa, const BaseDeDados *bd) {
    static const char LETRAS_DIRECAO[NUM_DIRECOES] = { 'N', 'S', 'L', 'O' };

    const Celula *atual = &mapa->celulas[jogador->linha][jogador->coluna];
    char teclas[NUM_DIRECOES + 1];
    int direcoes_disponiveis[NUM_DIRECOES];
    int n = 0;
    for (int d = 0; d < NUM_DIRECOES; d++) {
        if (atual->conectada[d]) {
            teclas[n] = LETRAS_DIRECAO[d];
            direcoes_disponiveis[n] = d;
            n++;
        }
    }
    teclas[n] = '\0';

    ui_log("Para que lado você quer se movimentar (%s)?", teclas);
    int escolha = ler_opcao(teclas);
    return comando_mover(jogador, mapa, bd, (Direcao)direcoes_disponiveis[escolha]);
}

static Resultado comando_trocar_arma_interativo(Jogador *jogador, const BaseDeDados *bd) {
    ui_log("Você tem as seguintes armas:");
    for (int i = 0; i < jogador->num_armas_obtidas; i++) {
        const Arma *arma = &bd->armas[jogador->armas_obtidas[i]];
        ui_log("%d. %s", i + 1, arma->nome);
    }
    ui_log("Que arma quer usar (1 a %d)?", jogador->num_armas_obtidas);
    int escolha = ui_ler_comando();
    return comando_trocar_arma(jogador, bd, escolha - 1);
}

static Resultado comando_comunicar_interativo(Jogador *jogador, Mapa *mapa, const BaseDeDados *bd) {
    ui_log("Quer subornar, irritar, ou ser amigável (S/I/A)?");
    static const AcaoComunicar OPCOES[3] = { COMUNICAR_SUBORNAR, COMUNICAR_IRRITAR, COMUNICAR_AMIGAVEL };
    int escolha = ler_opcao("SIA");
    AcaoComunicar acao = OPCOES[escolha];

    int valor_oferecido = 0;
    if (acao == COMUNICAR_SUBORNAR) {
        ui_log("Quanto você quer oferecer?");
        valor_oferecido = ui_ler_numero();
    }
    return comando_comunicar(jogador, mapa, bd, acao, valor_oferecido);
}

/*
 * Examinar a sala (linha 5001): repete a mesma descricao de tipo/saidas/
 * tripulante que "entrar em sala nova" mostra ao mover (Pacote 11) - assim
 * o jogador nao precisa sair e voltar na sala so pra reler. So pergunta
 * E/L/D (linha 5040) se a sala atual estiver escura - em sala clara nao ha
 * nada pra escolher (linha 5140, "existe luz suficiente"). 'desistiu'
 * sinaliza que o jogador cancelou (D) sem chamar combat.c - caso em que
 * nao ha Resultado de busca.
 */
static bool comando_examinar_sala_interativo(Jogador *jogador, Mapa *mapa, const BaseDeDados *bd, const Config *cfg, Resultado *res) {
    const Celula *atual = &mapa->celulas[jogador->linha][jogador->coluna];
    bool usar_lanterna = false;

    Resultado descricao;
    memset(&descricao, 0, sizeof(descricao));
    combat_narrar_sala_atual(jogador, mapa, bd, &descricao);
    narrar(&descricao);

    if (atual->escura) {
        ui_log("Está muito escuro aqui.");
        ui_log("Quer examinar a sala no escuro, utilizar a Lanterna, ou Desistir (E/L/D)?");
        int escolha = ler_opcao("ELD");
        if (escolha == 2) {
            return false; /* Desistir */
        }
        usar_lanterna = (escolha == 1);
    }

    *res = comando_examinar_sala(jogador, mapa, cfg, usar_lanterna);
    return true;
}

/* Ajuda (h/H, Pacote 11): reexibe so a lista de comandos, sem repetir a
 * historia completa da tela de titulo. */
static void mostrar_ajuda(void) {
    ui_limpar_log();
    ui_log("Existem 10 comandos:");
    ui_log("0 Mudar de sala      1 Atacar             2 Fugir");
    ui_log("3 Trocar de arma     4 Comunicar-se       5 Escudo (lig/des)");
    ui_log("6 Usar medicamentos  7 Situação           8 Examinar a sala");
    ui_log("9 Acionar teleporte");
    ui_log(" ");
    ui_log("Extra: a barra na base da tela mostra esse resumo sempre, sem precisar");
    ui_log("apertar H de novo. O mapa das salas visitadas fica sempre visível no");
    ui_log("painel à direita (ou aperte M pra vê-lo em tela cheia, útil se o painel");
    ui_log("não couber). As setas do teclado movem direto na direção, sem precisar");
    ui_log("digitar 0 antes.");
}

FimDeJogo game_loop(Jogador *jogador, Mapa *mapa, const BaseDeDados *bd, const Config *cfg) {
    /* Sala de Teleporte: o jogador "nasce" ali sem passar por
     * entrar_em_sala, entao marca visitada aqui (Pacote 14). */
    mapa->celulas[jogador->linha][jogador->coluna].visitada = true;

    /* Pacote 20: tratar o inicio como uma "entrada" tambem, narrando tipo
     * de sala + saidas antes do primeiro comando - antes o jogador comecava
     * o jogo sem ver nenhuma descricao da sala onde esta. A Sala de
     * Teleporte nunca tem tripulante (nasce "segura", ver map.c), entao
     * isso nao dispara a sequencia de pausas de apresentacao. */
    Resultado descricao_inicial;
    memset(&descricao_inicial, 0, sizeof(descricao_inicial));
    combat_narrar_sala_atual(jogador, mapa, bd, &descricao_inicial);
    narrar(&descricao_inicial);

    for (;;) {
        ui_desenhar_hud(jogador, bd);
        ui_desenhar_mapa(mapa, jogador); /* painel sempre visivel, Pacote 17 */
        int comando = ui_ler_comando();

        if (comando == -1) {
            mostrar_ajuda(); /* pseudo-comando: nao consome rodada */
            continue;
        }

        if (comando == -2) {
            /* Ctrl-L, Pacote 28: pseudo-comando que nao consome rodada - so
             * volta ao topo do loop, onde ui_desenhar_hud/ui_desenhar_mapa
             * ja checam e aplicam resize de terminal (verificar_e_aplicar_
             * resize em ui.c). Deixa o jogador forcar a sincronizacao da
             * tela sem esperar o proximo comando de verdade. */
            continue;
        }

        if (comando == -7) {
            /* 'm'/'M', Pacote 30: pseudo-comando de mapa em tela cheia, nao
             * consome rodada - fallback pra quando o painel lateral nao
             * cabe no terminal (ver ui_ler_comando/ui_mostrar_mapa_tela_cheia). */
            ui_mostrar_mapa_tela_cheia(mapa, jogador);
            continue;
        }

        /*
         * Atalho de seta (Pacote 18): -3..-6 ja' veem de ui_ler_comando()
         * como uma direcao escolhida, entao pula comando_mover_interativo
         * (que pediria a direcao de novo) e chama comando_mover() direto -
         * se nao houver porta nesse sentido, o proprio comando_mover ja'
         * devolve a mensagem "Nao ha saida pelo ..." (antes so' alcancavel
         * via chamada direta, nunca pelo prompt manual, que so' oferece as
         * direcoes com porta de fato).
         */
        Direcao direcao_atalho = NORTE;
        bool eh_atalho_direcao = true;
        switch (comando) {
            case -3: direcao_atalho = NORTE; break;
            case -4: direcao_atalho = SUL; break;
            case -5: direcao_atalho = LESTE; break;
            case -6: direcao_atalho = OESTE; break;
            default: eh_atalho_direcao = false; break;
        }

        /* Limpar a tela a cada comando e' fiel ao original (linha 600:
         * "CLS" antes de despachar o comando, linha 605) - evita que o log
         * rolante acumule a narracao de todos os comandos anteriores. */
        ui_limpar_log();

        Resultado res;
        bool tem_resultado = true;

        if (eh_atalho_direcao) {
            res = comando_mover(jogador, mapa, bd, direcao_atalho);
        } else {
            switch (comando) {
                case 0:
                    res = comando_mover_interativo(jogador, mapa, bd);
                    break;
                case 1:
                    res = comando_atacar(jogador, mapa, bd);
                    break;
                case 2:
                    res = comando_fugir(jogador, mapa, bd);
                    break;
                case 3:
                    res = comando_trocar_arma_interativo(jogador, bd);
                    break;
                case 4:
                    res = comando_comunicar_interativo(jogador, mapa, bd);
                    break;
                case 5:
                    res = comando_escudo(jogador);
                    break;
                case 6:
                    res = comando_usar_medicamento(jogador, mapa, bd, cfg);
                    break;
                case 7:
                    res = comando_situacao(jogador, bd);
                    break;
                case 8:
                    tem_resultado = comando_examinar_sala_interativo(jogador, mapa, bd, cfg, &res);
                    if (!tem_resultado) {
                        ui_log("Você desistiu.");
                    }
                    break;
                default: /* case 9 */
                    res = comando_acionar_teleporte(jogador, mapa);
                    break;
            }
        }

        if (!tem_resultado) {
            continue;
        }

        /* Pacote 27: mapa redesenhado antes de narrar - jogador->linha/coluna
         * ja' foi atualizado pelo comando (movimento/fuga/etc.) nesse ponto,
         * entao o painel ja mostra a posicao nova enquanto a narracao (com
         * as pausas dramaticas do Pacote 20) ainda esta rolando, em vez de
         * so pular pra posicao nova quando a narracao termina. HUD continua
         * depois de narrar - vida/energia mudam por motivos que a narracao
         * so explica depois (dano recebido etc.), diferente da posicao. */
        ui_desenhar_mapa(mapa, jogador);
        narrar(&res);
        ui_desenhar_hud(jogador, bd);

        /*
         * Perseguicao (Pacote 13): um tripulante fugiu com sucesso da sala
         * nesta rodada (linha 6831 do original: "QUER SEGUI-LO (S/N)?").
         * Ele ja foi relocado pra sala de destino independente da escolha
         * - so perguntamos se o jogador quer ir atras.
         */
        if (res.tripulante_fugiu) {
            ui_log("Quer segui-lo (S/N)?");
            int escolha = ler_opcao("SN");
            if (escolha == 0) {
                Resultado perseguicao = combate_seguir_tripulante_fugido(jogador, mapa, bd, res.trilha_fuga, res.trilha_fuga_tamanho);
                ui_desenhar_mapa(mapa, jogador); /* Pacote 27: mapa antes de narrar, ver comentario acima */
                narrar(&perseguicao);
                ui_desenhar_hud(jogador, bd);
            }
        }

        if (res.jogador_morreu) {
            return JOGO_FIM_MORTE;
        }
        if (comando == 9 && res.sucesso) {
            return JOGO_FIM_VITORIA;
        }
    }
}

void game_tela_morte(void) {
    ui_limpar_log();
    ui_log("Você foi mortalmente ferido.");
    ui_log("A tripulação roubou-lhe tudo e desintegrou seu corpo.");
    ui_log(" ");
    ui_log("Que azar... fim de jogo.");
    ui_aguardar_tecla();
}

void game_tela_vitoria(const Jogador *jogador, const BaseDeDados *bd) {
    ui_limpar_log();
    ui_log("Você voltou via teleporte.");
    ui_log("Você tem:");
    ui_log("Dinheiro: %d", jogador->dinheiro);
    for (int i = 0; i < jogador->num_armas_obtidas; i++) {
        ui_log("- %s", bd->armas[jogador->armas_obtidas[i]].nome);
    }
    ui_log(" ");
    ui_log("Parabéns, você venceu!");
    ui_aguardar_tecla();
}
