# Pacote 34 — bug: 10ª arma não podia ser selecionada na troca de arma

**Tamanho:** S · **Depende de:** [Pacote 8](08-game.md)

## Objetivo

Usuário reportou (relato do irmão jogando): ao ter 10 armas no inventário, escolher a arma listada
como "10" no menu de troca de arma sempre respondia `"Índice de arma inválido."`, embora as opções 1
a 9 funcionassem normalmente.

Causa: `comando_trocar_arma_interativo` ([game.c:97](../../src/game.c#L97), antes da correção) lia a
escolha com `ui_ler_comando()` ([ui.c:555](../../src/ui.c#L555)), que bloqueia até uma **única** tecla
`0`-`9` e devolve `tecla - '0'` — ou seja, o maior valor que essa função pode retornar é `9`. Não havia
como digitar dois dígitos, então a arma de índice 10 (décima da lista) era estruturalmente
inatingível: qualquer tentativa de digitar "10" já disparava na primeira tecla (`1`), interpretada como
escolha da arma 1, e a segunda tecla (`0`) ficava sobrando pro próximo prompt. `ui_ler_comando()` é
apropriada pros comandos de um dígito do jogo (mover, examinar, etc.), mas não pra esta lista, cujo
tamanho cresce com `jogador->num_armas_obtidas` e já ultrapassa 9 no cenário relatado (dez armas
listadas na tela, ver captura do usuário).

## Entregáveis

- `comando_trocar_arma_interativo` ([game.c](../../src/game.c)): troca `ui_ler_comando()` por
  `ui_ler_numero()` ([ui.c:610](../../src/ui.c#L610)) — mesma função já usada em
  `comando_comunicar_interativo` pro valor de suborno, que lê uma linha com eco (`wgetnstr`) e
  converte com `atoi`, aceitando naturalmente números de múltiplos dígitos.

## Critério de aceite

Build limpo (`-Wall -Wextra -Werror`, sem avisos). Com 10 armas no inventário, digitar `10` e Enter no
prompt "Que arma quer usar" seleciona a décima arma da lista (mensagem `"Sua arma agora é: <nome>"`),
em vez de `"Índice de arma inválido."`. Seleção de armas 1-9 continua funcionando como antes. Índices
fora do intervalo (`0`, negativos, maior que `num_armas_obtidas`) continuam retornando
`"Índice de arma inválido."` via `comando_trocar_arma` ([combat.c:399](../../src/combat.c#L399)), sem
mudança de comportamento aí.
