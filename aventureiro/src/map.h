#ifndef AVENTUREIRO_MAP_H
#define AVENTUREIRO_MAP_H

#include <stdbool.h>

#include "types.h"

/*
 * Gera um novo labirinto grid_size x grid_size (Config::grid_size), com
 * conectividade total garantida a partir da Sala de Teleporte via
 * randomized DFS / recursive backtracker, e portas extras adicionadas
 * depois para criar atalhos/ciclos. Salas sao povoadas com tipo,
 * tripulante e item conforme as chances de config.json.
 *
 * O original (linhas 46-270) sorteava as saidas de cada sala de forma
 * independente por sala/direcao, o que podia (na teoria) gerar salas
 * inalcancaveis - esta versao evita esse problema por construcao (ver
 * secao 5.4 do handover).
 */
Mapa gerar_mapa(const Config *cfg, const BaseDeDados *bd);

/*
 * Verifica via BFS que todas as celulas do mapa sao alcancaveis a partir
 * da Sala de Teleporte. Usada em testes/debug - a geracao normal ja
 * garante isso por construcao, entao isto nunca deveria falhar em
 * runtime; existe para o smoke test do Pacote 4 provar a garantia.
 */
bool mapa_totalmente_conectado(const Mapa *mapa);

#endif
