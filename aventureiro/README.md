# aventureiro (build em progresso)

Remake de "O Aventureiro" (ZX81 BASIC) em C + ncurses. Ver
[../handover_aventureiro_c.md](../handover_aventureiro_c.md) para o contexto completo da engenharia
reversa e [../management/README.md](../management/README.md) para o backlog de construção.

## Decisões da seção 7 do handover (revisadas no Pacote 10)

O [Pacote 0](../management/backlog/00-scaffold.md) travou estas decisões com defaults só para destravar
a implementação. No [Pacote 10](../management/backlog/10-polimento.md) foram revisadas com o usuário —
o resultado final de cada uma:

- **Grid**: **confirmado 8x8**, igual ao original (`data/config.json: grid_size`, ajustável sem
  recompilar).
- **Fórmulas de dano/cura/loot**: usuário pediu fidelidade ao original em vez de valores livres. O
  `aventureiro.p.bas` (listagem original decodificada) já estava no repo e permitiu decodificar as
  fórmulas exatas. O Pacote 10 já corrigiu dois desvios encontrados em `combat.c` (chance de
  dinheiro no loot pós-combate era 30%, o original usa 70%; redução de dano pelo escudo truncava
  em vez de arredondar como o original). O restante — cura parcial (não cheia) e medicamento como
  contador em vez de booleano — fica para o
  [Pacote 12](../management/backlog/12-fidelidade-formulas.md).
- **Fuga/perseguição de inimigos**: usuário pediu a lógica fiel ao original (linhas 6507/6800-6920),
  incluindo a inversão descoberta entre quem foge e quem contra-ataca. Vai para o
  [Pacote 13](../management/backlog/13-perseguicao-fiel.md).
- **Mapa visual**: usuário pediu um mapa ASCII acionado por um comando dedicado (tecla `M`), fora
  dos dez comandos originais. Vai para o
  [Pacote 14](../management/backlog/14-mapa-ascii.md).
- **Save/load**: mantido como no original — não implementado, uma partida = uma sessão contínua.

## Estrutura

```
aventureiro/
  data/     -> config.json, rooms.json, weapons.json, crew.json (Pacote 1)
  src/      -> código C + cJSON vendorizado (Pacotes 2+)
  Makefile
```

## Build e execução

```
make            # compila
make run        # compila e joga (equivalente a ./build/aventureiro)
make clean
make test       # fuzzing automatizado via pexpect (pip install -r tests/requirements.txt)
```

`./build/aventureiro [--data-dir DIR] [--seed N]` aceita `--data-dir` (default `data`) e `--seed`
(default: relógio do sistema, útil para reproduzir uma partida).
