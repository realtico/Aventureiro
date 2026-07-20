# Gerenciamento do projeto

Backlog de construção de ["O Aventureiro" em C + ncurses](../handover_aventureiro_c.md), quebrado em
pacotes pequenos — um arquivo por pacote — para que cada sessão de trabalho carregue só o pacote que
está executando, em vez do backlog inteiro.

## Como usar

1. Abra **só** o arquivo do pacote atual em `backlog/`. Não precisa ler os outros pacotes nem o
   handover inteiro de novo — cada arquivo de pacote é autocontido (objetivo, entregáveis, critério
   de aceite, dependências).
2. Trate cada pacote como uma sessão fechada: implementa, compila, roda o critério de aceite, para.
   Não abra o próximo pacote na mesma sessão "só para adiantar" — foi assim que a primeira tentativa
   estourou o orçamento de créditos.
3. Ao terminar um pacote, marque-o como concluído na tabela abaixo (edite este README).

## Ordem de execução

```
P0 (scaffold) → P1 (dados JSON) → P2 (types/util) → P3 (data_loader)
                                                            ↓
                                                      P4 (map)
                                                            ↓
                                                      P5 (player)
                                                            ↓
                                              P6a/P6b (combat)      P7 (ui) [pode ser paralelo a P4-P6]
                                                            ↘        ↙
                                                          P8 (game)
                                                            ↓
                                                    P9 (main + testes)
                                                            ↓
                                                    P10 (polimento)
                                                            ↓
                              ┌──────────────┬──────────────┬──────────────┐
                              ↓              ↓              ↓              ↓
                        P11 (melhorias  P12 (formulas  P13 (persegui-  P14 (mapa
                        de jogabilidade) restantes)     cao fiel)       ASCII)
```

## Pacotes

| # | Arquivo | Tamanho | Depende de | Status |
|---|---|---|---|---|
| 0 | [backlog/00-scaffold.md](backlog/00-scaffold.md) | S | — | [x] |
| 1 | [backlog/01-dados-json.md](backlog/01-dados-json.md) | S | — | [x] |
| 2 | [backlog/02-types-util.md](backlog/02-types-util.md) | S | — | [x] |
| 3 | [backlog/03-data-loader.md](backlog/03-data-loader.md) | M | P1, P2 | [x] |
| 4 | [backlog/04-map.md](backlog/04-map.md) | M | P2, P3 | [x] |
| 5 | [backlog/05-player.md](backlog/05-player.md) | S | P2, P4 | [x] |
| 6a | [backlog/06a-combat-basico.md](backlog/06a-combat-basico.md) | M | P4, P5 | [x] |
| 6b | [backlog/06b-combat-avancado.md](backlog/06b-combat-avancado.md) | M | P6a | [x] |
| 7 | [backlog/07-ui.md](backlog/07-ui.md) | M | P2 | [x] |
| 8 | [backlog/08-game.md](backlog/08-game.md) | M | P6b, P7 | [x] |
| 9 | [backlog/09-main-testes.md](backlog/09-main-testes.md) | M | P8 | [x] |
| 10 | [backlog/10-polimento.md](backlog/10-polimento.md) | S | P9 | [x] |
| 11 | [backlog/11-melhorias-jogabilidade.md](backlog/11-melhorias-jogabilidade.md) | S | P10 | [x] |
| 12 | [backlog/12-fidelidade-formulas.md](backlog/12-fidelidade-formulas.md) | M | P10 | [x] |
| 13 | [backlog/13-perseguicao-fiel.md](backlog/13-perseguicao-fiel.md) | M | P10 | [x] |
| 14 | [backlog/14-mapa-ascii.md](backlog/14-mapa-ascii.md) | S | P10 | [x] |
| 15 | [backlog/15-narrar-arma-tripulante.md](backlog/15-narrar-arma-tripulante.md) | S | P10 | [x] |
| 16 | [backlog/16-bug-acentos-linux.md](backlog/16-bug-acentos-linux.md) | S | P10 | [x] |
| 17 | [backlog/17-mapa-continuo.md](backlog/17-mapa-continuo.md) | M | P14 | [x] |
| 18 | [backlog/18-atalho-setas.md](backlog/18-atalho-setas.md) | S | P6a, P7 | [x] |
| 19 | [backlog/19-remover-espera-ajuda-mapa.md](backlog/19-remover-espera-ajuda-mapa.md) | S | P11, P14 | [x] |
| 20 | [backlog/20-pausas-dramaticas.md](backlog/20-pausas-dramaticas.md) | M | P6a, P15 | [x] |
| 21 | [backlog/21-migracao-cmake.md](backlog/21-migracao-cmake.md) | S | P9, P16 | [x] |
| 22 | [backlog/22-medicamentos-iniciais.md](backlog/22-medicamentos-iniciais.md) | S | P12 | [x] |
| 23 | [backlog/23-fugir-nao-seguido.md](backlog/23-fugir-nao-seguido.md) | S | P13 | [x] |
| 24 | [backlog/24-item-sala-nao-marcava-coletado.md](backlog/24-item-sala-nao-marcava-coletado.md) | S | P12 | [x] |
| 25 | [backlog/25-densidade-labirinto.md](backlog/25-densidade-labirinto.md) | S | P4 | [ ] |
| 26 | [backlog/26-barra-comandos.md](backlog/26-barra-comandos.md) | M | P11, P17 | [ ] |
| 27 | [backlog/27-mapa-antes-da-descricao.md](backlog/27-mapa-antes-da-descricao.md) | S | P17, P20 | [ ] |
| 34 | [backlog/34-troca-arma-10-nao-selecionavel.md](backlog/34-troca-arma-10-nao-selecionavel.md) | S | P8 | [x] |

## Depois do backlog

[ideias-futuras.md](ideias-futuras.md) — melhorias cogitadas para depois do Pacote 10 validado
(grid maior, novos personagens/armas, mais aleatoriedade de dano). Não mexer nisso antes da hora.

## Notas de execução

- P7 (ui) não depende de P4-P6 — pode ser feito em paralelo/intercalado se quiser variar o trabalho.
- P6 (combat) já vem pré-dividido em 6a/6b por ser o maior risco de estourar orçamento; se ainda
  assim ficar grande, divida por comando individual dentro do arquivo do pacote.
- As decisões em aberto do handover (seção 7) foram resolvidas com defaults no Pacote 0 exatamente
  para não bloquear o início da implementação, e revisadas com o usuário no Pacote 10 (ver
  [aventureiro/README.md](../aventureiro/README.md)). Resultado: grid 8x8 confirmado; fórmulas de
  dano/cura/loot devem seguir o original (`aventureiro.p.bas`, já no repo) em vez de valores livres
  — Pacote 10 já corrigiu os desvios simples, os que exigem mudar `types.h` foram para o Pacote 12;
  fuga/perseguição fiel virou o Pacote 13; mapa visual (ASCII, via comando extra) virou o Pacote 14;
  save/load continua fora de escopo.
