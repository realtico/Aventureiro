# Pacote 33 — bug: sala sem item examinada nunca ganhava marca × no mapa

**Tamanho:** S · **Depende de:** [Pacote 24](24-item-sala-nao-marcava-coletado.md), [Pacote 30](30-hud-corrompe-terminal-estreito.md)

## Objetivo

Usuário reportou: a marca `×` do mapa (introduzida no sidetrack do [Pacote 30](30-hud-corrompe-terminal-estreito.md))
deveria indicar que a sala foi **examinada** (comando 8), mas o que ficou implementado foi "sala teve
item coletado" — quando a sala não tem item nenhum, o comando 8 nunca marca a sala, mesmo depois de
examinada. A intenção original do usuário era dupla: (1) lembrar se já vasculhou aquela sala em busca
de item, e (2) servir de trilha de migalhas no mapa — distinguir uma sala que ele parou pra vasculhar
de uma sala por onde só passou (ou de onde fugiu) sem examinar.

Causa: `desenhar_grid_mapa` ([ui.c:623](../../aventureiro/src/ui.c#L623), antes da correção) usava
`celula->item_coletado` pra decidir o símbolo `×`, e esse campo só vira `true` dentro de
`comando_examinar_sala` ([combat.c](../../aventureiro/src/combat.c)) no ramo em que `tem_item` já era
`true` — ver Pacote 24. Uma sala sem item (`tem_item == false`, ~70% das salas com
`chances_percentual.item_na_sala = 30` em `data/config.json`) sempre cai em `"Nada."` e nunca passa por
esse ramo, então `item_coletado` fica `false` pra sempre, e a sala nunca ganha `×` — mesmo tendo sido
examinada dezenas de vezes.

## Entregáveis

- Novo campo `Celula::examinada` ([types.h](../../aventureiro/src/types.h)), distinto de
  `item_coletado`: marca que o comando 8 foi usado com sucesso nesta sala, tenha achado item ou não.
- `comando_examinar_sala` ([combat.c](../../aventureiro/src/combat.c)): seta `celula->examinada = true`
  logo após os dois guardas que hoje recusam o exame (tripulante vivo na sala; energia insuficiente pra
  lanterna numa sala escura) — ou seja, exame malsucedido (recusado) não marca a sala, mas qualquer
  exame que efetivamente aconteça marca, mesmo o que termina em acidente no escuro.
- `desenhar_grid_mapa` ([ui.c](../../aventureiro/src/ui.c)): trocar a condição do símbolo `×` de
  `celula->item_coletado` para `celula->examinada`. Legenda atualizada de `"× item coletado"` pra
  `"× examinada"`.
- `tests/smoke_test.py`: novo caso `verificar_sala_sem_item_examinada_marca_x`, seed 6 (sala ao Norte da
  Sala de Teleporte não tem item — confirmado via `"Nada."` após o comando 8), confirmando que a
  contagem de `×` na tela sobe depois do exame.

## Critério de aceite

Jogando manualmente: examinar uma sala sem item mostra `"Nada."` e a sala passa a aparecer com `×` no
mapa (não mais `·`) depois disso, mesmo sem nunca ter tido item. Examinar uma sala com item continua
funcionando como antes (mostra a recompensa, marca `×`). Uma sala apenas visitada (entrou e nunca usou
o comando 8, ou fugiu antes de examinar) continua aparecendo só com `·`, nunca `×`. `tests/smoke_test.py`
continua passando, incluindo o novo caso.

**Resolvido e confirmado.** Build limpo (`-Wall -Wextra -Werror`, sem avisos). Validado em duas camadas:
(1) harness C standalone descartável (fora do repositório, linkando `map.c`/`combat.c`/`data_loader.c`
reais) que gera o mapa do seed 1, acha uma sala sem item/sem tripulante/não escura fora da Sala de
Teleporte, chama `comando_examinar_sala` direto e confirma `examinada == true` e `item_coletado ==
false` depois — prova a lógica isolada da UI; (2) `tests/smoke_test.py` completo (todas as verificações
de tela via pexpect+pyte, incluindo a nova `verificar_sala_sem_item_examinada_marca_x` com seed 6) rodado
sem falha, mais o fuzzer de comandos aleatórios. Confirmado também que a Sala de Teleporte continua
sempre mostrando `o` (prioridade no `desenhar_grid_mapa` não mudou), mesmo depois de examinada — só
salas normais mostram `×`.
