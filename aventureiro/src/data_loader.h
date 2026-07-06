#ifndef AVENTUREIRO_DATA_LOADER_H
#define AVENTUREIRO_DATA_LOADER_H

#include <stdbool.h>

#include "types.h"

/*
 * Le config.json, rooms.json, weapons.json e crew.json de dentro de
 * 'diretorio_dados' e preenche 'bd' e 'cfg'. Em caso de arquivo ausente,
 * JSON malformado, campo faltando/de tipo errado, ou referencia invalida
 * (ex.: id_arma fora do intervalo de armas carregadas), imprime uma
 * mensagem de erro em stderr e retorna false - nunca crasha.
 */
bool carregar_dados(const char *diretorio_dados, BaseDeDados *bd, Config *cfg);

#endif
