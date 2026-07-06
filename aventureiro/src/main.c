/*
 * main.c provisorio dos Pacotes 5/6a: bateria de asserts sobre player.c e
 * combat.c, sem I/O nem ncurses. Sera substituido pelo main.c definitivo
 * no Pacote 9.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "combat.h"
#include "data_loader.h"
#include "map.h"
#include "player.h"
#include "types.h"

int main(int argc, char **argv) {
    const char *diretorio_dados = (argc > 1) ? argv[1] : "data";

    BaseDeDados bd;
    Config cfg;
    if (!carregar_dados(diretorio_dados, &bd, &cfg)) {
        fprintf(stderr, "main: falha ao carregar dados de '%s'\n", diretorio_dados);
        return 1;
    }

    srand((unsigned int)time(NULL));
    Mapa mapa = gerar_mapa(&cfg, &bd);

    /* jogador nasce na Sala de Teleporte com os valores iniciais de config.json */
    Jogador jogador = jogador_iniciar(&cfg, &mapa);
    assert(jogador.vida == cfg.vida_inicial);
    assert(jogador.energia == cfg.energia_inicial);
    assert(jogador.dinheiro == cfg.dinheiro_inicial);
    assert(jogador.linha == mapa.teleporte_linha);
    assert(jogador.coluna == mapa.teleporte_coluna);
    assert(jogador.num_armas_obtidas == 1);
    assert(jogador.armas_obtidas[0] == 2); /* Pistola Laser, igual ao original */
    assert(jogador.arma_atual == 0);
    assert(!jogador.tem_medicamento);
    assert(!jogador.escudo_ligado);

    /* usar medicamento sem ter: nao faz nada */
    bool curou = jogador_usar_medicamento(&jogador, cfg.vida_inicial);
    assert(!curou);
    assert(jogador.vida == cfg.vida_inicial);

    /* tomar dano, ganhar medicamento, curar ate a vida maxima */
    jogador.vida -= 5;
    jogador.tem_medicamento = true;
    curou = jogador_usar_medicamento(&jogador, cfg.vida_inicial);
    assert(curou);
    assert(jogador.vida == cfg.vida_inicial);
    assert(!jogador.tem_medicamento);

    /* usar medicamento com vida cheia: nao consome, nao faz nada */
    jogador.tem_medicamento = true;
    curou = jogador_usar_medicamento(&jogador, cfg.vida_inicial);
    assert(!curou);
    assert(jogador.tem_medicamento);

    /* adicionar arma nova */
    bool ok = jogador_adicionar_arma(&jogador, 3);
    assert(ok);
    assert(jogador.num_armas_obtidas == 2);

    /* adicionar arma duplicada falha e nao altera o inventario */
    ok = jogador_adicionar_arma(&jogador, 3);
    assert(!ok);
    assert(jogador.num_armas_obtidas == 2);

    /* trocar para uma arma valida do inventario */
    ok = jogador_trocar_arma(&jogador, 1);
    assert(ok);
    assert(jogador.arma_atual == 1);

    /* trocar para indice fora do inventario falha e nao altera arma_atual */
    ok = jogador_trocar_arma(&jogador, 5);
    assert(!ok);
    assert(jogador.arma_atual == 1);

    /* inventario cheio: parar de aceitar novas armas sem crashar */
    for (int id = 0; id < MAX_ARMAS_JOGADOR + 5; id++) {
        jogador_adicionar_arma(&jogador, 100 + id);
    }
    assert(jogador.num_armas_obtidas == MAX_ARMAS_JOGADOR);

    printf("Pacote 5 OK: todos os asserts de player.c passaram.\n");

    /* --- Pacote 6a: comandos basicos de combat.c --- */
    /* Jogador novo, isolado do jogador acima (que terminou com armas
     * falsas de teste no inventario) para nao contaminar os lookups em
     * bd.armas feitos por comando_trocar_arma/comando_situacao. */
    Jogador j = jogador_iniciar(&cfg, &mapa);
    Resultado res;

    /* trocar arma: com uma unica arma, comando falha e nao altera nada */
    res = comando_trocar_arma(&j, &bd, 0);
    assert(!res.sucesso);
    assert(j.arma_atual == 0);

    /* adiciona uma segunda arma e troca com sucesso */
    jogador_adicionar_arma(&j, 3); /* Rifle Phaser */
    res = comando_trocar_arma(&j, &bd, 1);
    assert(res.sucesso);
    assert(j.arma_atual == 1);
    assert(strstr(res.mensagens[0], "Rifle Phaser") != NULL);

    /* trocar para indice fora do inventario falha sem alterar arma_atual */
    res = comando_trocar_arma(&j, &bd, 9);
    assert(!res.sucesso);
    assert(j.arma_atual == 1);

    /* escudo: liga, desliga, e falha sem energia */
    res = comando_escudo(&j);
    assert(res.sucesso);
    assert(j.escudo_ligado);

    res = comando_escudo(&j);
    assert(res.sucesso);
    assert(!j.escudo_ligado);

    j.energia = 0;
    res = comando_escudo(&j);
    assert(!res.sucesso);
    assert(!j.escudo_ligado);
    j.energia = cfg.energia_inicial;

    /* usar medicamento: sem ter, falha */
    res = comando_usar_medicamento(&j, &cfg);
    assert(!res.sucesso);

    /* ganha medicamento mas vida cheia: falha, nao consome */
    j.tem_medicamento = true;
    res = comando_usar_medicamento(&j, &cfg);
    assert(!res.sucesso);
    assert(j.tem_medicamento);

    /* toma dano e cura ate a vida maxima */
    j.vida -= 5;
    res = comando_usar_medicamento(&j, &cfg);
    assert(res.sucesso);
    assert(j.vida == cfg.vida_inicial);
    assert(!j.tem_medicamento);

    /* situacao: so relata, nao falha */
    res = comando_situacao(&j, &bd);
    assert(res.sucesso);
    assert(res.num_mensagens > 0);

    /* mover: bloqueado por tripulante vivo na sala atual */
    int linha_inicial = j.linha;
    int coluna_inicial = j.coluna;
    Celula *atual = &mapa.celulas[linha_inicial][coluna_inicial];
    atual->tem_tripulante = true;
    atual->id_tripulante = 0;
    atual->tripulante_vivo = true;
    res = comando_mover(&j, &mapa, &bd, NORTE);
    assert(!res.sucesso);
    assert(j.linha == linha_inicial && j.coluna == coluna_inicial);

    /* mover: sem tripulante, mas sem porta na direcao tentada */
    atual->tem_tripulante = false;
    atual->tripulante_vivo = false;
    for (int d = 0; d < NUM_DIRECOES; d++) {
        atual->conectada[d] = false;
    }
    res = comando_mover(&j, &mapa, &bd, NORTE);
    assert(!res.sucesso);
    assert(j.linha == linha_inicial && j.coluna == coluna_inicial);

    /* mover: com porta valida, jogador se desloca e recebe narracao da sala */
    Direcao direcao_teste = (linha_inicial + 1 < mapa.tamanho) ? SUL : NORTE;
    int delta_linha = (direcao_teste == SUL) ? 1 : -1;
    atual->conectada[direcao_teste] = true;
    res = comando_mover(&j, &mapa, &bd, direcao_teste);
    assert(res.sucesso);
    assert(j.linha == linha_inicial + delta_linha && j.coluna == coluna_inicial);
    assert(res.num_mensagens > 0);

    /* examinar sala: cenario controlado na sala nova */
    Celula *nova = &mapa.celulas[j.linha][j.coluna];
    nova->tem_tripulante = false;
    nova->tripulante_vivo = false;
    nova->escura = false;
    nova->tem_item = false;
    nova->item_coletado = false;

    /* sala clara, sem item: examina sem risco, encontra nada */
    res = comando_examinar_sala(&j, &mapa, &cfg, false);
    assert(res.sucesso);

    /* sala clara com item: coleta uma vez, na segunda vez nao ha mais nada */
    nova->tem_item = true;
    nova->item_coletado = false;
    res = comando_examinar_sala(&j, &mapa, &cfg, false);
    assert(res.sucesso);
    assert(nova->item_coletado);
    res = comando_examinar_sala(&j, &mapa, &cfg, false);
    assert(res.sucesso);

    /* sala escura, lanterna sem energia suficiente: falha, nao gasta energia */
    nova->escura = true;
    int energia_guardada = j.energia;
    j.energia = 5;
    res = comando_examinar_sala(&j, &mapa, &cfg, true);
    assert(!res.sucesso);
    assert(j.energia == 5);
    j.energia = energia_guardada;

    /* sala escura, lanterna com energia: sucesso, gasta energia da lanterna */
    int energia_antes_lanterna = j.energia;
    res = comando_examinar_sala(&j, &mapa, &cfg, true);
    assert(res.sucesso);
    assert(j.energia == energia_antes_lanterna - 20);

    /* sala escura sem lanterna, acidente garantido (chance 100%): perde vida */
    Config cfg_acidente_garantido = cfg;
    cfg_acidente_garantido.chance_acidente_no_escuro = 100;
    int vida_antes_acidente = j.vida;
    res = comando_examinar_sala(&j, &mapa, &cfg_acidente_garantido, false);
    assert(j.vida < vida_antes_acidente);

    /* tripulante presente: nao pode examinar */
    nova->tem_tripulante = true;
    nova->tripulante_vivo = true;
    res = comando_examinar_sala(&j, &mapa, &cfg, false);
    assert(!res.sucesso);
    nova->tem_tripulante = false;
    nova->tripulante_vivo = false;

    /* teleporte: falha fora da Sala de Teleporte */
    res = comando_acionar_teleporte(&j, &mapa);
    assert(!res.sucesso);

    /* teleporte: sucesso na Sala de Teleporte, sem tripulante */
    j.linha = mapa.teleporte_linha;
    j.coluna = mapa.teleporte_coluna;
    res = comando_acionar_teleporte(&j, &mapa);
    assert(res.sucesso);

    printf("Pacote 6a OK: todos os asserts de combat.c (comandos basicos) passaram.\n");

    /* --- Pacote 6b: atacar, fugir, comunicar-se --- */
    /* Jogador novo, isolado dos anteriores, nascendo na Sala de Teleporte
     * (que sobrou com uma unica porta depois das mutacoes do Pacote 6a
     * acima - serve bem para o teste de fugir, que so precisa de 1 saida). */
    Jogador k = jogador_iniciar(&cfg, &mapa);
    Celula *sala_combate = &mapa.celulas[k.linha][k.coluna];

    /* atacar ate matar: tripulante agressivo com arma diferente da do
     * jogador (para testar o loot), vida baixa para morrer logo, energia
     * de sobra pro jogador para nao esbarrar no teste em erros de sorte. */
    sala_combate->tem_tripulante = true;
    sala_combate->id_tripulante = 3; /* Navegador Ackio: agressivo, arma id 6 (Sabre Ionico) */
    sala_combate->tripulante_vivo = true;
    sala_combate->tripulante_vida_atual = 2;
    k.energia = 100000;

    int tentativas = 0;
    while (sala_combate->tripulante_vivo && tentativas < 200) {
        res = comando_atacar(&k, &mapa, &bd);
        tentativas++;
    }
    assert(tentativas < 200);
    assert(!sala_combate->tripulante_vivo);
    assert(!sala_combate->tem_tripulante);
    assert(k.num_armas_obtidas == 2);
    assert(k.armas_obtidas[1] == 6); /* Sabre Ionico looted do corpo */

    /* atacar sem ninguem na sala: falha */
    res = comando_atacar(&k, &mapa, &bd);
    assert(!res.sucesso);

    /* atacar sem energia para a arma: falha */
    sala_combate->tem_tripulante = true;
    sala_combate->id_tripulante = 3;
    sala_combate->tripulante_vivo = true;
    sala_combate->tripulante_vida_atual = 1000;
    int energia_antes_sem_energia = 0;
    k.energia = energia_antes_sem_energia;
    res = comando_atacar(&k, &mapa, &bd);
    assert(!res.sucesso);
    assert(k.energia == energia_antes_sem_energia);

    /* contra-ataque so acontece se o tripulante sobreviver: vida alta
     * garante que ele nao morre num golpe so, entao ou erra ou reage. A
     * energia da arma e' consumida sempre, acerte ou erre. */
    k.energia = 100000;
    int energia_antes_ataque = k.energia;
    const Arma *arma_de_k = &bd.armas[k.armas_obtidas[k.arma_atual]];
    res = comando_atacar(&k, &mapa, &bd);
    assert(res.sucesso);
    assert(k.energia == energia_antes_ataque - arma_de_k->custo_energia);
    assert(sala_combate->tripulante_vivo);
    assert(k.vida > 0); /* Sabre Ionico (dano max 4) nao mata os 20 de vida inicial num golpe */

    /* fugir: sala com tripulante e exatamente uma porta (sobrou do
     * Pacote 6a); sucesso desloca o jogador, queda mantem no lugar - os
     * dois resultados sao validos, entao aceitamos ambos sem forcar RNG. */
    int linha_antes_fugir = k.linha;
    int coluna_antes_fugir = k.coluna;
    res = comando_fugir(&k, &mapa, &bd);
    assert(res.num_mensagens > 0);
    if (res.sucesso) {
        assert(k.linha != linha_antes_fugir || k.coluna != coluna_antes_fugir);
    } else {
        assert(k.linha == linha_antes_fugir && k.coluna == coluna_antes_fugir);
    }

    /* fugir sem tripulante na sala atual: falha */
    Celula *onde_k_esta = &mapa.celulas[k.linha][k.coluna];
    onde_k_esta->tem_tripulante = false;
    res = comando_fugir(&k, &mapa, &bd);
    assert(!res.sucesso);

    /* comunicar-se: de volta a Sala de Teleporte para um cenario limpo */
    k.linha = mapa.teleporte_linha;
    k.coluna = mapa.teleporte_coluna;
    Celula *sala_comunicar = &mapa.celulas[k.linha][k.coluna];

    /* tripulante nao-agressivo: nao consegue se comunicar */
    sala_comunicar->tem_tripulante = true;
    sala_comunicar->id_tripulante = 6; /* Robot de Seguranca M-1: agressivo = false */
    sala_comunicar->tripulante_vivo = true;
    sala_comunicar->tripulante_vida_atual = bd.tripulantes[6].vida;
    res = comando_comunicar(&k, &mapa, &bd, COMUNICAR_AMIGAVEL, 0);
    assert(!res.sucesso);

    /* subornar com oferta de 0: rejeicao garantida (0 nunca supera o
     * sorteio em [0,999]) - fracasso deterministico com reacao do
     * tripulante agressivo */
    sala_comunicar->id_tripulante = 3; /* Navegador Ackio: agressivo */
    sala_comunicar->tripulante_vida_atual = 1000;
    k.dinheiro = 2000;
    res = comando_comunicar(&k, &mapa, &bd, COMUNICAR_SUBORNAR, 0);
    assert(!res.sucesso);
    assert(k.dinheiro == 2000);
    assert(sala_comunicar->tem_tripulante);

    /* subornar oferecendo mais dinheiro do que se tem: falha sem custo */
    res = comando_comunicar(&k, &mapa, &bd, COMUNICAR_SUBORNAR, k.dinheiro + 1);
    assert(!res.sucesso);
    assert(k.dinheiro == 2000);

    /* amigavel com o tripulante quase morto: sucesso garantido (linha
     * 3330 do original, MBP<2), tripulante deixa a sala em paz */
    sala_comunicar->tripulante_vida_atual = 1;
    int dinheiro_antes_amigavel = k.dinheiro;
    res = comando_comunicar(&k, &mapa, &bd, COMUNICAR_AMIGAVEL, 0);
    assert(res.sucesso);
    assert(k.dinheiro > dinheiro_antes_amigavel);
    assert(!sala_comunicar->tem_tripulante);

    printf("Pacote 6b OK: todos os asserts de combat.c (atacar/fugir/comunicar) passaram.\n");
    return 0;
}
