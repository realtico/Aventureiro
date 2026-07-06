#ifndef AVENTUREIRO_UTIL_H
#define AVENTUREIRO_UTIL_H

#include <stdbool.h>
#include <stdlib.h> /* rand() - facil de esquecer incluir quando so se usa via estas inlines */

#include "types.h"

/* Sorteia um inteiro em [minimo, maximo], inclusive nas duas pontas. */
static inline int sorteio_intervalo(int minimo, int maximo) {
    if (maximo <= minimo) {
        return minimo;
    }
    return minimo + rand() % (maximo - minimo + 1);
}

/*
 * Retorna true com probabilidade 'percentual' em 100. Usada para todas as
 * chances de config.json (tripulante na sala, item na sala, acidente no
 * escuro, etc.) - centralizar aqui evita reimplementar rand()%100 espalhado
 * pelo codigo com off-by-one diferentes em cada lugar.
 */
static inline bool sorteio_chance(int percentual) {
    if (percentual <= 0) {
        return false;
    }
    if (percentual >= 100) {
        return true;
    }
    return sorteio_intervalo(1, 100) <= percentual;
}

/*
 * Converte uma direcao numerica em texto (equivalente a sub-rotina de
 * "imprimir direcao", linha 6400 do original). Recebe int em vez de Direcao
 * nas chamadas internas (loops/arrays) para evitar o falso positivo de
 * -Warray-bounds do GCC com enum usado como indice depois de inlining em
 * -O2 (ver secao 6 do handover) - o cast para Direcao so acontece aqui,
 * no unico lugar que de fato exige o tipo enumerado.
 */
static inline const char *direcao_nome(Direcao direcao) {
    switch (direcao) {
        case NORTE: return "Norte";
        case SUL:   return "Sul";
        case LESTE: return "Leste";
        case OESTE: return "Oeste";
    }
    return "?";
}

#endif
