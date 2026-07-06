#ifndef AVENTUREIRO_TYPES_H
#define AVENTUREIRO_TYPES_H

#include <stdbool.h>

/*
 * Limites de capacidade dos arrays estaticos abaixo. Nao sao parametros de
 * balanceamento (esses vivem em config.json, ver Pacote 1) - sao so o
 * tamanho maximo que os dados em data/ podem ter. data_loader.c
 * (Pacote 3) preenche BaseDeDados::num_* com a contagem real lida do JSON,
 * que pode ser menor ou igual a estes maximos.
 */
#define MAX_SALAS 32
#define MAX_ARMAS 16
#define MAX_TRIPULANTES 32
#define MAX_NOME 48
#define MAX_FRASE 64
#define MAX_ARMAS_JOGADOR 16

/* Direcoes cardeais de movimento entre celulas do mapa. */
typedef enum {
    NORTE = 0,
    SUL = 1,
    LESTE = 2,
    OESTE = 3
} Direcao;

#define NUM_DIRECOES 4

/* Parametros de balanceamento carregados de data/config.json (Pacote 1). */
typedef struct {
    int grid_size;

    int vida_inicial;
    int energia_inicial;
    int dinheiro_inicial;

    int chance_tripulante_na_sala;
    int chance_item_na_sala;
    int chance_sala_escura;
    int chance_acidente_no_escuro;
    int chance_porta_extra_labirinto;
} Config;

/* Um tipo de sala da nave (data/rooms.json), equivalente as linhas REM 9110-9220 do original. */
typedef struct {
    int id;
    char nome[MAX_NOME];
    bool teleporte; /* true so para a Sala de Teleporte - inicio/fim de partida. */
} TipoSala;

/* Uma arma (data/weapons.json), equivalente as linhas REM 9310-9400 do original. */
typedef struct {
    int id;
    char nome[MAX_NOME];
    int dano_maximo;
    int chance_acerto_percentual;
    int custo_energia;
} Arma;

/* Um tripulante/monstro (data/crew.json), equivalente as linhas REM 9510-9700 do original. */
typedef struct {
    int id;
    char nome[MAX_NOME];
    char frase[MAX_FRASE];
    int vida;
    bool agressivo;
    int id_arma; /* indice em BaseDeDados::armas usado pelo tripulante ao revidar. */
} Tripulante;

/* Todas as tabelas de dados do jogo, carregadas uma vez no inicio (Pacote 3). */
typedef struct {
    TipoSala salas[MAX_SALAS];
    int num_salas;

    Arma armas[MAX_ARMAS];
    int num_armas;

    Tripulante tripulantes[MAX_TRIPULANTES];
    int num_tripulantes;
} BaseDeDados;

/*
 * Uma celula do labirinto da nave. 'conectada[d]' indica se ha porta na
 * direcao d (indexada por Direcao) para a celula vizinha - o gerador de
 * mapa (Pacote 4) preenche isso garantindo conectividade total via DFS.
 */
typedef struct {
    int id_tipo_sala; /* indice em BaseDeDados::salas */

    bool tem_tripulante;
    int id_tripulante; /* indice em BaseDeDados::tripulantes, valido so se tem_tripulante */
    bool tripulante_vivo;
    int tripulante_vida_atual; /* copia mutavel de Tripulante::vida (Pacote 6b) - o
                                 * template em BaseDeDados e' compartilhado/imutavel,
                                 * mas o combate precisa acumular dano por sala. */

    bool tem_item;
    bool item_coletado;

    bool escura;
    bool conectada[NUM_DIRECOES];
} Celula;

/* O labirinto inteiro: grid_size x grid_size celulas, regenerado a cada partida. */
typedef struct {
    int tamanho; /* lado do grid quadrado, igual a Config::grid_size */
    Celula celulas[MAX_SALAS][MAX_SALAS]; /* [linha][coluna], so os primeiros 'tamanho' de cada eixo sao usados */
    int teleporte_linha;
    int teleporte_coluna;
} Mapa;

/* Estado do jogador durante a partida. */
typedef struct {
    int vida;
    int energia;
    int dinheiro;

    int linha;
    int coluna;

    int armas_obtidas[MAX_ARMAS_JOGADOR];
    int num_armas_obtidas;
    int arma_atual; /* indice dentro de armas_obtidas, nao em BaseDeDados::armas */

    bool tem_medicamento;
    bool escudo_ligado;
} Jogador;

#endif
