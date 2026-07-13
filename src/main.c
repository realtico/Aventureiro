/*
 * Ponto de entrada definitivo (Pacote 9), substituindo os main.c
 * provisorios dos Pacotes 5/6a/6b (bateria de asserts sem I/O, agora
 * superada pelo fuzzing via tests/smoke_test.py) e do Pacote 8
 * (game_smoke.c, que ligava tudo manualmente antes deste main.c existir).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "data_loader.h"
#include "game.h"
#include "map.h"
#include "player.h"
#include "types.h"
#include "ui.h"

static void imprimir_uso(const char *nome_programa) {
    fprintf(stderr, "Uso: %s [--data-dir DIR] [--seed N]\n", nome_programa);
    fprintf(stderr, "  --data-dir DIR   diretório com config.json/rooms.json/weapons.json/crew.json (default: data)\n");
    fprintf(stderr, "  --seed N         semente do gerador aleatório (default: relógio do sistema)\n");
}

int main(int argc, char **argv) {
    const char *diretorio_dados = "data";
    unsigned int seed = (unsigned int)time(NULL);
    bool tem_seed = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--data-dir") == 0 && i + 1 < argc) {
            diretorio_dados = argv[++i];
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seed = (unsigned int)strtoul(argv[++i], NULL, 10);
            tem_seed = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            imprimir_uso(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "main: argumento desconhecido '%s'\n\n", argv[i]);
            imprimir_uso(argv[0]);
            return 1;
        }
    }
    (void)tem_seed; /* so influencia qual valor 'seed' recebeu, ja tratado acima */

    BaseDeDados bd;
    Config cfg;
    if (!carregar_dados(diretorio_dados, &bd, &cfg)) {
        fprintf(stderr, "main: falha ao carregar dados de '%s'\n", diretorio_dados);
        return 1;
    }

    srand(seed);
    Mapa mapa = gerar_mapa(&cfg, &bd);
    Jogador jogador = jogador_iniciar(&cfg, &mapa);

    ui_iniciar(mapa.tamanho);
    game_tela_titulo();
    FimDeJogo fim = game_loop(&jogador, &mapa, &bd, &cfg);
    if (fim == JOGO_FIM_MORTE) {
        game_tela_morte();
    } else {
        game_tela_vitoria(&jogador, &bd);
    }
    ui_encerrar();

    return 0;
}
