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

/* Entrar em sala nova (linha 6002): narra tipo de sala, saidas e presenca
 * de tripulante; drena energia do escudo se ligado (linha 6115 + 3600). */
static void entrar_em_sala(Jogador *jogador, const Mapa *mapa, const BaseDeDados *bd, Resultado *r) {
    log_msg(r, "Voce entrou numa nova sala.");

    const Celula *celula = &mapa->celulas[jogador->linha][jogador->coluna];
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
        if (!sorteio_chance(67)) {
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
        dano = dano / 3;
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

Resultado comando_usar_medicamento(Jogador *jogador, const Config *cfg) {
    Resultado r = resultado_vazio();
    bool tinha_medicamento = jogador->tem_medicamento;

    if (jogador_usar_medicamento(jogador, cfg->vida_inicial)) {
        log_msg(&r, "Sente-se melhor? Voce agora tem %d pontos de vida.", jogador->vida);
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
    log_msg(&r, "Medicamento: %s", jogador->tem_medicamento ? "sim" : "nao");
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

    celula->item_coletado = true;
    int tipo = sorteio_intervalo(0, 2);
    if (tipo == 0) {
        int energia = sorteio_intervalo(1, 3) * 100;
        jogador->energia += energia;
        log_msg(&r, "Cargas de energia: +%d.", energia);
    } else if (tipo == 1) {
        jogador->tem_medicamento = true;
        log_msg(&r, "Medicamento.");
    } else {
        int dinheiro = sorteio_intervalo(50, 2000);
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
 * original (o range exato la e' obscuro/parcialmente morto; usamos um
 * valor razoavel, ver handover secao 5.5). */
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
        if (sorteio_chance(20)) {
            jogador->tem_medicamento = true;
            log_msg(&r, "Tambem encontrou medicamentos.");
        }
        if (sorteio_chance(30)) {
            int dinheiro = sorteio_intervalo(50, 249);
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

    if (!sorteio_chance(90)) {
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
            if (valor_oferecido > sorteio_intervalo(0, 999)) {
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
            if (sorteio_chance(20)) {
                log_msg(&r, "O %s escorrega e vai longe.", tripulante->nome);
                celula->tem_tripulante = false;
                return r;
            }
            log_msg(&r, "Nao gostou da sua atitude agressiva.");
            r.sucesso = false;
            reacao_tripulante_apos_turno(jogador, celula, bd, &r);
            return r;

        case COMUNICAR_AMIGAVEL:
            if (sorteio_chance(9) || celula->tripulante_vida_atual < 2) {
                int dinheiro = sorteio_intervalo(1, 250);
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
