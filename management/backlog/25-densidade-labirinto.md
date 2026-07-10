# Pacote 25 — labirinto pouco conectado (poucas salas, muito corredor)

**Tamanho:** S · **Depende de:** [Pacote 4](04-map.md)

## Objetivo

Usuário reportou sensação de mapa pouco conectado: "muito labirinto e pouco salas". Investigado se
é fidelidade ao original ou desvio evitável.

**Não dá pra comparar diretamente com o original** — a geração de portas roda inteira dentro de uma
rotina em código de máquina (`USR RD`, chamada em `aventureiro.p.bas:80`, dentro do loop de
preenchimento de cada célula), sem nenhuma lógica de probabilidade visível em BASIC. O
`handover_aventureiro_c.md` (linha 184) já documentava isso desde o Pacote 0: *"o original sorteava
portas de forma independente por sala/direção, o que podia (na teoria) gerar salas inalcançáveis"*
— inferido jogando o original, não lido do código-fonte (que é opaco). A decisão tomada no Pacote 0
foi trocar por um algoritmo com conectividade garantida.

**O algoritmo atual (`map.c`) é uma árvore geradora (randomized DFS / recursive backtracker) +
portas extras**, não uma tentativa de aproximar o original:

1. `esculpir_labirinto()`: DFS aleatório a partir da Sala de Teleporte gera uma árvore geradora do
   grid — por definição, exatamente `N-1` portas pra `N` salas (63 portas pras 64 salas de um 8x8),
   **zero ciclos**. Garante conectividade total por construção, mas é estruturalmente o tipo de
   labirinto mais "corredor" que existe (uma árvore não tem loop nenhum).
2. `adicionar_portas_extras()`: pra cada par de salas vizinhas ainda sem porta, sorteia
   `chance_porta_extra_labirinto`% (hoje **15%**, `config.json`) de abrir uma porta extra ali.

Conta pro grid 8x8 padrão: 112 pares de salas vizinhas possíveis, 63 já usados pela árvore, sobram
49 — com 15% de chance cada, sobram ~7 portas extras. Total ≈ 70 de 112 possíveis (62%), **média de
~2,2 portas por sala**. É esse "verniz fino de atalhos por cima de uma árvore" que dá a sensação de
labirinto excessivo.

## Entregáveis (opção escolhida: aumentar a densidade via config)

- `data/config.json`: subir `chance_porta_extra_labirinto` de 15 pra um valor que aproxime a média
  de portas/sala do alvo pedido (~3). Contas feitas (grid 8x8, 63 portas da árvore + 49 pares
  restantes):

  | `chance_porta_extra_labirinto` | portas totais (esperado) | média portas/sala |
  |---|---|---|
  | 15% (atual) | ~70 | ~2,20 |
  | 50% | ~88 | ~2,73 |
  | 65% | ~95 | ~2,96 |
  | **70%** | **~97** | **~3,04** |
  | 80% | ~102 | ~3,19 |

  **70%** é o valor mais próximo do alvo de ~3 portas/sala.
- Não muda o algoritmo (`map.c` continua garantindo conectividade total por construção via
  `mapa_totalmente_conectado()`), só a quantidade de ciclos/atalhos adicionados por cima da árvore
  geradora. Reversível trocando o número de volta.

## Critério de aceite

Jogando manualmente (ou inspecionando o mapa ASCII, Pacote 14/17): sensação de mais salas
interligadas, menos corredores únicos sem alternativa. `tests/smoke_test.py` continua passando
(gerador de mapa não muda de algoritmo, só o parâmetro).

**Resolvido e confirmado.** Adotada a solução simples (só `config.json`, sem mudar o algoritmo).
Jogando manualmente com valores diferentes de `chance_porta_extra_labirinto`: **60% já pareceu
demais** — o mapa perde a sensação de labirinto, fica aberto demais/sem corredores únicos de
verdade. Optou-se por **50%** em vez do 70% originalmente calculado como "mais próximo do alvo
matemático de ~3 portas/sala" — a sensação de jogo (60% já excessivo) pesou mais que a conta em
teoria, e 50% ficou no meio do caminho entre o original 15% (muito labirinto/pouca sala) e o 60%
testado (muito aberto). Pela tabela já calculada acima: 50% dá ≈88 portas totais, ≈2,73 portas/sala
(ante ≈2,20 em 15%). `data/config.json`: `chances_percentual.porta_extra_labirinto` alterado de 15
para **50**. Sem mudança em `map.c` — `esculpir_labirinto()`/`adicionar_portas_extras()`/
`mapa_totalmente_conectado()` continuam iguais, só o parâmetro de entrada muda; reversível trocando o
número de volta a qualquer momento.

## Alternativa não escolhida, registrada para o futuro

Trocar o algoritmo inteiro por sorteio independente de porta por par de salas vizinhas (mais perto
da hipótese do handover sobre o original) + um passo de reparo de conectividade (ex.: rodar
`mapa_totalmente_conectado()`, e se `false`, unir componentes desconectados com portas extras) em
vez de árvore geradora + extras. Produziria uma distribuição de portas mais uniforme/orgânica em vez
do viés "estrutura de árvore com atalhos". Fica em `management/ideias-futuras.md` como possibilidade
se ajustar só o parâmetro não for suficiente.
