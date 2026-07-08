#include "combat.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "player.h"
#include "util.h"

/* Custo de energia da lanterna ao examinar sala escura, linha 5030/5210 do
 * original (o limiar hardcoded de 20 e' o mesmo valor usado como custo). */
#define CUSTO_ENERGIA_LANTERNA 20

/* Drenagem de energia do escudo por sala percorrida, linha 3600 do original. */
#define CUSTO_ENERGIA_ESCUDO_POR_SALA 2

/* Chance (%) de tropecar e cair ao tentar fugir, linha 2040 do original
 * ("IF RND>.1 THEN GOTO 2048" - so 10% cai, os outros 90% fogem). */
#define CHANCE_FUGIR_SUCESSO 90

/* Chance (%) de o tripulante em panico escapar mesmo assim, linha 6801
 * ("IF RND>.33 THEN GOTO 6810" - 67% foge, 33% cai na tentativa). */
#define CHANCE_PANICO_ESCAPAR 67

/* Chance (%) de sortear um medicamento extra ao saquear o corpo de um
 * tripulante agressivo morto, linha 1830 do original ("IF RND>.2 THEN
 * GOTO 1860" - 20% ganha medicamento). */
#define CHANCE_LOOT_MEDICAMENTO 20

/* Chance (%) de sortear dinheiro extra ao saquear o corpo, linha 1860 do
 * original ("IF RND>.7 THEN GOTO 1900" - 70% ganha dinheiro, nao 30%). */
#define CHANCE_LOOT_DINHEIRO 70

/* Faixa de dinheiro (A$) saqueada do corpo, linha 1870 do original
 * ("INT(RND*200+50)" = 50 a 249). */
#define DINHEIRO_MINIMO_LOOT 50
#define DINHEIRO_MAXIMO_LOOT 249

/* Item encontrado ao examinar a sala (linha 5240-5390 do original) - ver
 * uso em comando_examinar_sala para o porque de cada limiar. */
#define ENERGIA_CARGA 100
#define ENERGIA_CARGA_LIMIAR_UMA 0.5
#define ENERGIA_CARGA_LIMIAR_DUAS 0.2
#define MEDICAMENTO_ITEM_LIMIAR_UM 0.4
#define MEDICAMENTO_ITEM_LIMIAR_DOIS 0.15
#define DINHEIRO_BASE_ITEM_SALA 50
#define DINHEIRO_FATOR_ITEM_SALA 2000

/* Limiar de suborno: aceita se a oferta superar um sorteio em [0,999],
 * linha 3190 do original ("IF X>RND*1E3 THEN GOTO 3220"). */
#define SUBORNO_LIMIAR_MAXIMO 999

/* Chance (%) de o tripulante irritado escorregar e ir embora, linha 3270
 * do original ("IF RND<.2 THEN GOTO 3300"). */
#define CHANCE_IRRITAR_SUCESSO 20

/* Chance (%) de o tripulante ficar amigavel por sorte (alem do caso de
 * vida baixa abaixo), linha 3330 do original ("IF RND<.09 OR MBP<2 THEN
 * GOTO 3360"). */
#define CHANCE_AMIGAVEL_SUCESSO 9
#define VIDA_AMIGAVEL_GARANTIDO 2

/* Faixa de dinheiro dado pelo tripulante amigavel, linha 3360 do original
 * ("INT(RND*250+1)" = 1 a 250). */
#define DINHEIRO_MINIMO_AMIGAVEL 1
#define DINHEIRO_MAXIMO_AMIGAVEL 250

/* Indexados por int (nao Direcao) de proposito - ver aviso do handover
 * (secao 6) sobre -Warray-bounds do GCC com enum usado como indice apos
 * inlining em -O2, mesmo padrao adotado em map.c. */
static const int DELTA_LINHA[NUM_DIRECOES] = { -1, 1, 0, 0 };
static const int DELTA_COLUNA[NUM_DIRECOES] = { 0, 0, 1, -1 };

static Resultado resultado_vazio(void) {
    Resultado r;
    memset(&r, 0, sizeof(r));
    r.sucesso = true;
    r.jogador_morreu = false;
    return r;
}

static void log_msg(Resultado *r, const char *formato, ...) {
    if (r->num_mensagens >= MAX_MENSAGENS_RESULTADO) {
        return;
    }
    va_list args;
    va_start(args, formato);
    vsnprintf(r->mensagens[r->num_mensagens], MAX_TAMANHO_MENSAGEM, formato, args);
    va_end(args);
    r->num_mensagens++;
}

/* Acidente no escuro (linha 7000): chance de se machucar ao examinar uma
 * sala escura sem lanterna. Dano de 1 a 5, mensagem varia por gravidade. */
static void acidente_no_escuro(Jogador *jogador, Resultado *r) {
    int dano = sorteio_intervalo(1, 5);
    if (dano == 1) {
        log_msg(r, "No escuro, voce bateu na parede.");
    } else if (dano <= 3) {
        log_msg(r, "Voce caiu no escuro e quebrou seu pe.");
    } else {
        log_msg(r, "Voce caiu no escuro e fraturou a perna.");
    }
    log_msg(r, "Voce perdeu %d ponto%s de vida.", dano, dano > 1 ? "s" : "");

    jogador->vida -= dano;
    if (jogador->vida <= 0) {
        jogador->vida = 0;
        log_msg(r, "Que azar, voce morreu.");
        r->jogador_morreu = true;
    }
}

/*
 * Descreve tipo de sala/saidas/tripulante da celula em (linha, coluna) -
 * conteudo compartilhado entre "entrar em sala nova" (abaixo) e o comando
 * Examinar interativo (Pacote 11, game.c), que repete a mesma narracao
 * antes do resultado da busca.
 */
static void narrar_sala(const Mapa *mapa, const BaseDeDados *bd, int linha, int coluna, Resultado *r) {
    const Celula *celula = &mapa->celulas[linha][coluna];
    const TipoSala *tipo = &bd->salas[celula->id_tipo_sala];
    log_msg(r, "Sala tipo: %s", tipo->nome);

    char saidas[MAX_TAMANHO_MENSAGEM] = "Saidas: ";
    bool alguma = false;
    for (int d = 0; d < NUM_DIRECOES; d++) {
        if (celula->conectada[d]) {
            if (alguma) {
                strncat(saidas, ", ", sizeof(saidas) - strlen(saidas) - 1);
            }
            strncat(saidas, direcao_nome((Direcao)d), sizeof(saidas) - strlen(saidas) - 1);
            alguma = true;
        }
    }
    if (!alguma) {
        strncat(saidas, "nenhuma", sizeof(saidas) - strlen(saidas) - 1);
    }
    log_msg(r, "%s", saidas);

    if (celula->tem_tripulante && celula->tripulante_vivo) {
        const Tripulante *tripulante = &bd->tripulantes[celula->id_tripulante];
        log_msg(r, "Ha alguem aqui: %s. \"%s.\"", tripulante->nome, tripulante->frase);
    } else {
        log_msg(r, "Nao ha ninguem aqui.");
    }
}

void combat_narrar_sala_atual(const Jogador *jogador, const Mapa *mapa, const BaseDeDados *bd, Resultado *r) {
    narrar_sala(mapa, bd, jogador->linha, jogador->coluna, r);
}

/* Entrar em sala nova (linha 6002): narra tipo de sala, saidas e presenca
 * de tripulante; drena energia do escudo se ligado (linha 6115 + 3600). */
static void entrar_em_sala(Jogador *jogador, const Mapa *mapa, const BaseDeDados *bd, Resultado *r) {
    log_msg(r, "Voce entrou numa nova sala.");
    narrar_sala(mapa, bd, jogador->linha, jogador->coluna, r);

    if (jogador->escudo_ligado) {
        jogador->energia -= CUSTO_ENERGIA_ESCUDO_POR_SALA;
        if (jogador->energia <= 0) {
            jogador->energia = 0;
            jogador->escudo_ligado = false;
            log_msg(r, "Escudo desativado: energia esgotada.");
        }
    }
}

/* Desliga o escudo se a energia zerou (linha 3620), chamada tanto ao
 * drenar energia por sala (acima) quanto antes de recusar um ataque por
 * falta de energia (linha 1508). */
static void desligar_escudo_se_sem_energia(Jogador *jogador, Resultado *r) {
    if (jogador->escudo_ligado && jogador->energia <= 0) {
        jogador->energia = 0;
        jogador->escudo_ligado = false;
        log_msg(r, "Escudo desativado: energia esgotada.");
    }
}

/*
 * Reacao do tripulante quando sobrevive ao turno do jogador (ataque que
 * errou ou nao matou, fuga que caiu, ou comunicacao que fracassou) -
 * equivalente a sub-rotina compartilhada da linha 6505 (GOTO 6500),
 * chamada de varios pontos no original. Agressivo contra-ataca (linha
 * 6510-6650, com reducao de dano pelo escudo, linha 6580-6610);
 * nao-agressivo entra em panico e foge (linha 6800-6810) - versao
 * simplificada, decisao do Pacote 0: o tripulante so desaparece da sala,
 * sem perseguicao multi-sala.
 */
static void reacao_tripulante_apos_turno(Jogador *jogador, Celula *celula, const BaseDeDados *bd, Resultado *r) {
    if (!celula->tem_tripulante || !celula->tripulante_vivo) {
        return;
    }
    const Tripulante *tripulante = &bd->tripulantes[celula->id_tripulante];

    if (!tripulante->agressivo) {
        log_msg(r, "O %s entra em panico.", tripulante->nome);
        if (!sorteio_chance(CHANCE_PANICO_ESCAPAR)) {
            log_msg(r, "E na tentativa de fugir, cai.");
            return;
        }
        log_msg(r, "E foge.");
        celula->tem_tripulante = false;
        return;
    }

    const Arma *arma = &bd->armas[tripulante->id_arma];
    log_msg(r, "O %s ataca com %s.", tripulante->nome, arma->nome);

    if (!sorteio_chance(arma->chance_acerto_percentual)) {
        log_msg(r, "Quase te acerta, mas erra.");
        return;
    }

    int dano = sorteio_intervalo(1, arma->dano_maximo);
    if (jogador->escudo_ligado) {
        /* INT(X/3+0.7), linha 6590 do original: arredonda em vez de
         * truncar (dano/3 puro subestimaria o dano reduzido). Em inteiros,
         * INT(X/3+0.7) = INT((10X+21)/30). */
        dano = (dano * 10 + 21) / 30;
        jogador->energia -= dano * 5;
        desligar_escudo_se_sem_energia(jogador, r);
        if (dano == 0) {
            log_msg(r, "O escudo absorve o golpe.");
            return;
        }
    }

    log_msg(r, "Acertou em voce. Voce perdeu %d ponto%s de vida.", dano, dano > 1 ? "s" : "");
    jogador->vida -= dano;
    if (jogador->vida <= 0) {
        jogador->vida = 0;
        log_msg(r, "Voce foi mortalmente ferido. O %s roubou-lhe tudo.", tripulante->nome);
        r->jogador_morreu = true;
    }
}

Resultado comando_mover(Jogador *jogador, Mapa *mapa, const BaseDeDados *bd, Direcao direcao) {
    Resultado r = resultado_vazio();

    const Celula *atual = &mapa->celulas[jogador->linha][jogador->coluna];
    if (atual->tem_tripulante && atual->tripulante_vivo) {
        const Tripulante *tripulante = &bd->tripulantes[atual->id_tripulante];
        log_msg(&r, "Nao se arrisque dando as costas ao %s.", tripulante->nome);
        r.sucesso = false;
        return r;
    }

    if (!atual->conectada[direcao]) {
        log_msg(&r, "Nao ha saida pelo %s.", direcao_nome(direcao));
        r.sucesso = false;
        return r;
    }

    jogador->linha += DELTA_LINHA[direcao];
    jogador->coluna += DELTA_COLUNA[direcao];

    entrar_em_sala(jogador, mapa, bd, &r);
    return r;
}

Resultado comando_trocar_arma(Jogador *jogador, const BaseDeDados *bd, int indice_no_inventario) {
    Resultado r = resultado_vazio();

    if (jogador->num_armas_obtidas <= 1) {
        log_msg(&r, "Voce so tem uma arma.");
        r.sucesso = false;
        return r;
    }

    if (!jogador_trocar_arma(jogador, indice_no_inventario)) {
        log_msg(&r, "Indice de arma invalido.");
        r.sucesso = false;
        return r;
    }

    const Arma *arma = &bd->armas[jogador->armas_obtidas[jogador->arma_atual]];
    log_msg(&r, "Sua arma agora e: %s.", arma->nome);
    return r;
}

Resultado comando_escudo(Jogador *jogador) {
    Resultado r = resultado_vazio();

    if (jogador->energia <= 0) {
        log_msg(&r, "Energia insuficiente.");
        r.sucesso = false;
        return r;
    }

    jogador->escudo_ligado = !jogador->escudo_ligado;
    log_msg(&r, "Agora seu escudo esta %s.", jogador->escudo_ligado ? "ligado" : "desligado");
    return r;
}

Resultado comando_usar_medicamento(Jogador *jogador, Mapa *mapa, const BaseDeDados *bd, const Config *cfg) {
    Resultado r = resultado_vazio();
    bool tinha_medicamento = jogador->num_medicamentos > 0;

    if (jogador_usar_medicamento(jogador, cfg->vida_inicial)) {
        log_msg(&r, "Sente-se melhor? Voce agora tem %d pontos de vida.", jogador->vida);
        Celula *celula = &mapa->celulas[jogador->linha][jogador->coluna];
        reacao_tripulante_apos_turno(jogador, celula, bd, &r);
        return r;
    }

    if (tinha_medicamento) {
        log_msg(&r, "Nao seja hipocondriaco... voce esta bem.");
    } else {
        log_msg(&r, "Voce nao tem mais medicamentos.");
    }
    r.sucesso = false;
    return r;
}

Resultado comando_situacao(const Jogador *jogador, const BaseDeDados *bd) {
    Resultado r = resultado_vazio();

    log_msg(&r, "Situacao");
    log_msg(&r, "Vida: %d", jogador->vida);
    log_msg(&r, "Medicamentos: %d", jogador->num_medicamentos);
    log_msg(&r, "Energia: %d", jogador->energia);
    log_msg(&r, "Dinheiro: %d", jogador->dinheiro);
    log_msg(&r, "Armas: %d", jogador->num_armas_obtidas);

    if (jogador->vida > 0 && jogador->num_armas_obtidas > 0) {
        const Arma *arma = &bd->armas[jogador->armas_obtidas[jogador->arma_atual]];
        log_msg(&r, "Arma atual: %s", arma->nome);
    }

    return r;
}

Resultado comando_examinar_sala(Jogador *jogador, Mapa *mapa, const Config *cfg, bool usar_lanterna) {
    Resultado r = resultado_vazio();

    Celula *celula = &mapa->celulas[jogador->linha][jogador->coluna];
    if (celula->tem_tripulante && celula->tripulante_vivo) {
        log_msg(&r, "Nao pode examinar a sala com alguem nela.");
        r.sucesso = false;
        return r;
    }

    if (celula->escura) {
        if (usar_lanterna) {
            if (jogador->energia < CUSTO_ENERGIA_LANTERNA) {
                log_msg(&r, "Energia insuficiente para usar a lanterna.");
                r.sucesso = false;
                return r;
            }
            jogador->energia -= CUSTO_ENERGIA_LANTERNA;
            log_msg(&r, "Usando a lanterna, voce examina a sala.");
        } else {
            log_msg(&r, "Esta muito escuro aqui.");
            if (sorteio_chance(cfg->chance_acidente_no_escuro)) {
                acidente_no_escuro(jogador, &r);
                if (r.jogador_morreu) {
                    return r;
                }
            }
        }
    } else {
        log_msg(&r, "Que sorte. Existe luz suficiente para examinar a sala sem usar a lanterna.");
    }

    log_msg(&r, "Voce encontrou...");
    if (!celula->tem_item || celula->item_coletado) {
        log_msg(&r, "Nada.");
        return r;
    }

    /*
     * O original (linha 5220 em diante) guarda o tipo de item por sala
     * desde a geracao do mapa (um char P/M/E fixo por sala). O mapa
     * (Pacote 4) ja simplifica presenca de item para um bool sem tipo
     * pre-definido (ver combat.h) - aqui sorteamos o tipo com peso
     * uniforme (1/3 cada), mas as quantidades/valores dentro de cada tipo
     * abaixo replicam o viés exato do original (Pacote 12).
     */
    int tipo = sorteio_intervalo(0, 2);
    if (tipo == 0) {
        /* Cargas de energia, linha 5260-5270: "X=RND"; 1 carga (50%,
         * X>.5), 2 cargas (30%, .2<=X<=.5) ou 3 cargas (20%, X<.2), cada
         * uma valendo 100. */
        double x = sorteio_uniforme();
        int cargas = (x > ENERGIA_CARGA_LIMIAR_UMA) ? 1 : (x >= ENERGIA_CARGA_LIMIAR_DUAS) ? 2 : 3;
        int energia = cargas * ENERGIA_CARGA;
        jogador->energia += energia;
        log_msg(&r, "Cargas de energia: +%d.", energia);
    } else if (tipo == 1) {
        /* Medicamentos, linha 5330-5340: "X=RND"; 1 (60%, X>.4), 2 (25%,
         * .15<X<=.4) ou 3 (15%, X<=.15) de uma vez. */
        double x = sorteio_uniforme();
        int qtd = (x > MEDICAMENTO_ITEM_LIMIAR_UM) ? 1 : (x > MEDICAMENTO_ITEM_LIMIAR_DOIS) ? 2 : 3;
        jogador->num_medicamentos += qtd;
        log_msg(&r, "Medicamentos: +%d.", qtd);
    } else {
        /* Dinheiro achado na sala, linha 5370: "INT(50+RND*RND*RND*2E3)" -
         * cubo de RND enviesa fortemente para valores baixos dentro da
         * faixa nominal 50 a 2050. */
        double x = sorteio_uniforme();
        int dinheiro = (int)(DINHEIRO_BASE_ITEM_SALA + x * x * x * DINHEIRO_FATOR_ITEM_SALA);
        jogador->dinheiro += dinheiro;
        log_msg(&r, "A$ %d.00", dinheiro);
    }

    return r;
}

Resultado comando_acionar_teleporte(Jogador *jogador, const Mapa *mapa) {
    Resultado r = resultado_vazio();

    if (jogador->linha != mapa->teleporte_linha || jogador->coluna != mapa->teleporte_coluna) {
        log_msg(&r, "Voce nao se encontra na Sala de Teleporte.");
        r.sucesso = false;
        return r;
    }

    const Celula *celula = &mapa->celulas[jogador->linha][jogador->coluna];
    if (celula->tem_tripulante && celula->tripulante_vivo) {
        log_msg(&r, "Nao pode usar o teleporte com alguem na sala.");
        r.sucesso = false;
        return r;
    }

    log_msg(&r, "Voce voltou via teleporte. Partida encerrada com sucesso.");
    return r;
}

/* Energia ganha ao saquear o corpo de um tripulante morto, linha 1750 do
 * original ("INT(RND*120+30)" = 30 a 149). */
#define ENERGIA_MINIMA_LOOT 30
#define ENERGIA_MAXIMA_LOOT 149

Resultado comando_atacar(Jogador *jogador, Mapa *mapa, const BaseDeDados *bd) {
    Resultado r = resultado_vazio();
    Celula *celula = &mapa->celulas[jogador->linha][jogador->coluna];

    if (!celula->tem_tripulante || !celula->tripulante_vivo) {
        log_msg(&r, "Atacar quem? Nao ha ninguem aqui.");
        r.sucesso = false;
        return r;
    }

    const Tripulante *tripulante = &bd->tripulantes[celula->id_tripulante];
    const Arma *arma = &bd->armas[jogador->armas_obtidas[jogador->arma_atual]];

    if (arma->custo_energia > jogador->energia) {
        log_msg(&r, "Voce nao tem energia suficiente para atacar.");
        desligar_escudo_se_sem_energia(jogador, &r);
        r.sucesso = false;
        return r;
    }

    log_msg(&r, "Voce ataca o %s.", tripulante->nome);
    jogador->energia -= arma->custo_energia;

    if (!sorteio_chance(arma->chance_acerto_percentual)) {
        log_msg(&r, "Errou. Ma sorte.");
        reacao_tripulante_apos_turno(jogador, celula, bd, &r);
        return r;
    }

    int dano = sorteio_intervalo(1, arma->dano_maximo);
    log_msg(&r, "Acertou nele, reduzindo %d ponto%s de vida.", dano, dano > 1 ? "s" : "");
    celula->tripulante_vida_atual -= dano;

    if (celula->tripulante_vida_atual > 0) {
        reacao_tripulante_apos_turno(jogador, celula, bd, &r);
        return r;
    }

    log_msg(&r, "O %s morreu.", tripulante->nome);
    celula->tripulante_vivo = false;
    celula->tem_tripulante = false;

    if (jogador_adicionar_arma(jogador, tripulante->id_arma)) {
        log_msg(&r, "Voce recolhe a arma dele: %s.", bd->armas[tripulante->id_arma].nome);
    }

    int energia_ganha = sorteio_intervalo(ENERGIA_MINIMA_LOOT, ENERGIA_MAXIMA_LOOT);
    jogador->energia += energia_ganha;
    log_msg(&r, "Ele tinha uma carga de energia: +%d.", energia_ganha);

    /* Loot extra so para tripulantes agressivos (linha 1800: MI=1) -
     * os nao-agressivos (robots/serventes) nao carregam mais nada. */
    if (tripulante->agressivo) {
        if (sorteio_chance(CHANCE_LOOT_MEDICAMENTO)) {
            jogador->num_medicamentos++; /* linha 1850: "LET M=M+1" */
            log_msg(&r, "Tambem encontrou medicamentos.");
        }
        if (sorteio_chance(CHANCE_LOOT_DINHEIRO)) {
            int dinheiro = sorteio_intervalo(DINHEIRO_MINIMO_LOOT, DINHEIRO_MAXIMO_LOOT);
            jogador->dinheiro += dinheiro;
            log_msg(&r, "E A$ %d.00.", dinheiro);
        }
    }

    return r;
}

Resultado comando_fugir(Jogador *jogador, Mapa *mapa, const BaseDeDados *bd) {
    Resultado r = resultado_vazio();
    Celula *celula = &mapa->celulas[jogador->linha][jogador->coluna];

    if (!celula->tem_tripulante || !celula->tripulante_vivo) {
        log_msg(&r, "Fugir de quem, covarde?");
        r.sucesso = false;
        return r;
    }

    if (!sorteio_chance(CHANCE_FUGIR_SUCESSO)) {
        log_msg(&r, "Voce tenta fugir, mas cai.");
        reacao_tripulante_apos_turno(jogador, celula, bd, &r);
        r.sucesso = false;
        return r;
    }

    int direcoes_disponiveis[NUM_DIRECOES];
    int n_disponiveis = 0;
    for (int d = 0; d < NUM_DIRECOES; d++) {
        if (celula->conectada[d]) {
            direcoes_disponiveis[n_disponiveis++] = d;
        }
    }
    if (n_disponiveis == 0) {
        log_msg(&r, "Nao ha para onde fugir.");
        r.sucesso = false;
        return r;
    }

    int direcao = direcoes_disponiveis[sorteio_intervalo(0, n_disponiveis - 1)];
    log_msg(&r, "Voce fugiu pelo lado %s.", direcao_nome((Direcao)direcao));

    jogador->linha += DELTA_LINHA[direcao];
    jogador->coluna += DELTA_COLUNA[direcao];

    entrar_em_sala(jogador, mapa, bd, &r);
    return r;
}

Resultado comando_comunicar(Jogador *jogador, Mapa *mapa, const BaseDeDados *bd, AcaoComunicar acao, int valor_oferecido) {
    Resultado r = resultado_vazio();
    Celula *celula = &mapa->celulas[jogador->linha][jogador->coluna];

    if (!celula->tem_tripulante || !celula->tripulante_vivo) {
        log_msg(&r, "Falando consigo mesmo... e o primeiro sinal de loucura.");
        r.sucesso = false;
        return r;
    }

    const Tripulante *tripulante = &bd->tripulantes[celula->id_tripulante];
    if (!tripulante->agressivo) {
        log_msg(&r, "O %s e pouco inteligente para comunicar-se.", tripulante->nome);
        r.sucesso = false;
        return r;
    }

    switch (acao) {
        case COMUNICAR_SUBORNAR:
            if (valor_oferecido > jogador->dinheiro) {
                log_msg(&r, "Voce nao tem todo esse dinheiro.");
                r.sucesso = false;
                return r;
            }
            log_msg(&r, "Esta pensando...");
            if (valor_oferecido > sorteio_intervalo(0, SUBORNO_LIMIAR_MAXIMO)) {
                jogador->dinheiro -= valor_oferecido;
                log_msg(&r, "Aceita, deixando-o em paz.");
                celula->tem_tripulante = false;
                return r;
            }
            log_msg(&r, "E decide nao aceitar.");
            r.sucesso = false;
            reacao_tripulante_apos_turno(jogador, celula, bd, &r);
            return r;

        case COMUNICAR_IRRITAR:
            if (sorteio_chance(CHANCE_IRRITAR_SUCESSO)) {
                log_msg(&r, "O %s escorrega e vai longe.", tripulante->nome);
                celula->tem_tripulante = false;
                return r;
            }
            log_msg(&r, "Nao gostou da sua atitude agressiva.");
            r.sucesso = false;
            reacao_tripulante_apos_turno(jogador, celula, bd, &r);
            return r;

        case COMUNICAR_AMIGAVEL:
            if (sorteio_chance(CHANCE_AMIGAVEL_SUCESSO) || celula->tripulante_vida_atual < VIDA_AMIGAVEL_GARANTIDO) {
                int dinheiro = sorteio_intervalo(DINHEIRO_MINIMO_AMIGAVEL, DINHEIRO_MAXIMO_AMIGAVEL);
                jogador->dinheiro += dinheiro;
                log_msg(&r, "O %s achou voce um cara legal, deu-lhe A$ %d.00 e foi embora.", tripulante->nome, dinheiro);
                celula->tem_tripulante = false;
                return r;
            }
            log_msg(&r, "Nao gosta do seu sorriso.");
            r.sucesso = false;
            reacao_tripulante_apos_turno(jogador, celula, bd, &r);
            return r;
    }

    return r;
}
