# Ideias futuras (pós-Pacote 10)

> Não fazem parte do backlog de construção em [backlog/](backlog/) — são melhorias a considerar
> **depois** que o Pacote 10 (polimento) estiver validado e o jogo completo estiver jogável e fiel
> ao original. Cada uma provavelmente vira seu próprio pacote pequeno quando chegar a vez.

- **Labirinto maior** — grid além de 8x8. Já é quase de graça: `grid_size` em `data/config.json` (Pacote 1), `map.c` (Pacote 4) já generaliza para qualquer tamanho até `MAX_SALAS` (32). Só validar performance/legibilidade em grids bem maiores.
- **Novos personagens (tripulantes)** — entradas novas em `data/crew.json`. Estrutura já suporta; só respeitar `id_arma` válido e `MAX_TRIPULANTES` (32) em `types.h`.
- **Novas armas** — entradas novas em `data/weapons.json`. Mesma ideia; respeitar `MAX_ARMAS` (16).
- **Mais aleatoriedade nas armas** — hoje dano é uniforme em `[1, dano_maximo]`. O Pacote 10
  decodificou `aventureiro.p.bas` e confirmou que o original também usa `INT(RND*MMD+1)` uniforme
  para dano de arma (não há viés aqui) — só os valores de loot/item têm viés não uniforme (ver
  [Pacote 12](backlog/12-fidelidade-formulas.md)). Se quiser variância por arma (críticos, dado com
  viés), é mecânica nova, não fidelidade ao original.

> Nota: as ideias de "revelar mapa conhecido" e "perseguição multi-sala" que estavam aqui viraram
> decisões confirmadas com o usuário no Pacote 10 — não são mais ideias soltas, são os pacotes
> [13 (perseguição fiel)](backlog/13-perseguicao-fiel.md) e [14 (mapa ASCII)](backlog/14-mapa-ascii.md).

- **Scripts de instalação (Linux e macOS)** — script que verifica se as dependências de build (compilador C, `ncurses`/`pkg-config`, e `pexpect` para `make test`) estão presentes e oferece instalar as que faltarem (via `apt`/`brew`, dependendo do SO), para reduzir o atrito de quem for compilar o jogo pela primeira vez.

Guardar mais ideias aqui conforme surgirem durante a implementação dos pacotes atuais.
