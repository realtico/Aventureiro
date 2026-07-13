# Handover: "O Aventureiro" (ZX81) → C + ncurses

> Documento de transferência para retomar este trabalho em outra sessão (com Claude ou outro modelo). Não contém código C — é o mapa mental necessário para começar a conversão com contexto completo.

---

## 1. Objetivo

Recriar o jogo de aventura em texto **"O Aventureiro"** (ZX81 BASIC, publicado pela Ciberne Software no Brasil, provavelmente port de um jogo europeu de nave espacial/tripulação hostil) como um programa em **C puro + ncurses**, mantendo o espírito e as regras do original, mas com uma arquitetura de software limpa e moderna:

- Código C estruturado em módulos com responsabilidade única.
- Dados de jogo (salas, armas, tripulantes) **fora do código**, em arquivos **JSON** (usando a biblioteca `cJSON`, single-file, vendorizada no projeto).
- Parâmetros de balanceamento/configuração (tamanho do mapa, chances de eventos, vida/energia iniciais) num `config.json` — **nada disso deve estar hardcoded** em `#define`.
- Muito comentário explicando o "porquê", não só o "o quê" — e sempre que uma decisão de design tiver origem numa linha específica do BASIC original, comentar a referência (ex: `/* equivalente as linhas 1505-1940 do original */`).

---

## 2. O jogo original: arquitetura em BASIC

O `.p` do ZX81 foi desmontado e decodificado por completo (tokens + números ocultos). Isso é o que se descobriu:

### 2.1 Fluxo geral

```
Inicialização (linhas 1-9)
    → variáveis, DIM, POKEs de configuração
Criação do mapa da nave (linhas 46-270)
    → gera aleatoriamente as saídas de cada sala (labirinto 8x8)
Tela de título (linhas 10-20)
    → história do jogo + lista de comandos
Loop principal (linha 500)
    → lê uma tecla (0-9) e despacha via GOSUB 500+comando*500
```

### 2.2 Os dez comandos do jogador

| Tecla | Comando | Linha original | Efeito |
|---|---|---|---|
| 0 | Mover | 1004 | Anda N/S/L/O entre salas conectadas |
| 1 | Atacar | 1505 | Combate contra o tripulante da sala |
| 2 | Fugir | 2010 | Foge para uma sala vizinha aleatória |
| 3 | Trocar de arma | 2502 | Escolhe entre as armas já obtidas |
| 4 | Comunicar-se | 3010 | Subornar / Irritar / ser Amigável com o tripulante |
| 5 | Escudo | 3501 | Liga/desliga o escudo (reduz dano, consome energia) |
| 6 | Usar medicamento | 4010 | Recupera vida, se tiver medicamento e não estiver com vida cheia |
| 7 | Situação | 4510 | Mostra vida, energia, dinheiro, armas |
| 8 | Examinar a sala | 5001 | Procura itens; em salas escuras, arrisca acidente |
| 9 | Acionar teleporte | 5510 | Só funciona na Sala de Teleporte — termina a partida com sucesso, mantendo o que foi coletado |

### 2.3 Sub-rotinas de apoio (compartilhadas entre comandos)

| Rotina | Linha | Função |
|---|---|---|
| Entrar em sala nova | 6002 | Descreve a sala, sorteia se há tripulante e o que ele carrega |
| Contra-ataque do inimigo | 6505 | O tripulante revida depois de um ataque que não o mata; pode matar o jogador |
| Inimigo foge | 6800 | Tripulante não-agressivo pode entrar em pânico e fugir para outra sala |
| Acidente no escuro | 7000 | Chance de se machucar ao examinar uma sala escura sem lanterna |
| Aguardar tecla | 8000 | Pausa universal ("pressione uma tecla") entre eventos |
| Imprimir direção | 6400 | Converte código numérico de direção em texto (Norte/Sul/Leste/Oeste) |

### 2.4 Morte e vitória

- Se a vida (`BP`) chega a zero: o jogador perde tudo, tela de "você foi mortalmente ferido", pergunta se quer jogar de novo (reinicia do zero, mapa novo).
- Não há mapa fixo: a nave é toda regenerada aleatoriamente a cada partida.
- "Vencer" = voltar à Sala de Teleporte e usar o comando 9, o que encerra a partida preservando o que foi coletado.

---

## 3. O que o código de máquina faz (a parte "sensível" da engenharia reversa)

O programa tem uma **linha 0** que começa com o byte `0xEA` — que é ao mesmo tempo:
- o token `REM` do ZX81 (então o BASIC trata a linha inteira como comentário/lixo ilegível), **e**
- um opcode Z80 válido (`JP PE,nn`), usado apenas como disfarce — essa primeira instrução nunca é de fato executada.

Escondidas dentro dessa "linha REM" existem **duas sub-rotinas reais em Z80**, chamadas via `USR <endereço>`:

### 3.1 `RT` (endereço 16601) — equivalente a um `RESTORE`

1. Lê o valor que o comando `RAND <linha>` acabou de gravar na variável de sistema `SEED` (endereço 16434) — esse é o **número da linha REM alvo** (ex.: `RAND 9330 : RAND USR RT` aponta para a linha 9330).
2. Varre a tabela de linhas do programa (a partir do endereço 16509, início do BASIC) comparando números de linha, usando o mesmo algoritmo de busca que `GOTO`/`GOSUB` usam internamente.
3. Ao encontrar a linha, salva o **endereço de início do texto daquela linha** num ponteiro "cursor" — guardado nos bytes 16507-16508, que são oficialmente listados como "não usados" nas system variables do ZX81 (um esconderijo clássico usado por programadores da época).

### 3.2 `RD` (endereço 16514) — equivalente a um `READ`

1. Usa o cursor salvo por `RT` e copia caracteres, um a um, para dentro do buffer da variável `A$` (dimensionada no início do programa com `DIM A$(32)`), **até encontrar uma vírgula ou o fim da linha**.
2. Avança o cursor para o próximo campo.
3. Devolve, no par de registradores `BC` (convenção de retorno do `USR` no ZX81), **o tamanho do texto copiado** — e é esse valor que o BASIC usa para fatiar `A$` corretamente: `A$( TO USR RD)`.

### 3.3 Por que isso existe

O **ZX81 BASIC não tem `DATA`/`READ`/`RESTORE` nativos** (diferente de BBC BASIC, Spectrum BASIC, MSX BASIC etc.). O programador precisava de uma forma compacta de armazenar tabelas de salas/armas/tripulantes sem escrever dezenas de `LET` e `DIM` de arrays. A solução foi:

- Guardar cada registro de dado como uma linha `REM ,campo1,campo2,campo3,...` (o BASIC ignora o conteúdo).
- Reimplementar `RESTORE` (`RT`) e `READ` (`RD`) em Z80, tratando **o próprio código-fonte do programa como um banco de dados**.

Isso é uma forte evidência de que o jogo é um **port** de uma versão que rodava originalmente numa plataforma com `DATA/READ/RESTORE` de fábrica (suspeita mais forte: ecossistema britânico de 8 bits — BBC Micro/Acorn e vizinhos — dada a popularidade do ZX81 no Reino Unido e a cultura de listagens de revista compartilhadas entre essas plataformas). Não foi possível confirmar o jogo de origem exato.

### 3.4 Padrão de uso no BASIC

```basic
RAND 9330+VAL T$(I)*10   : REM aponta para a linha certa da tabela de armas
RAND USR RT              : REM RESTORE - posiciona o cursor
LET W$=A$( TO USR RD)    : REM READ - primeiro campo (nome)
LET MD=VAL A$( TO USR RD): REM READ - segundo campo (dano)
LET H=VAL A$( TO USR RD) : REM READ - terceiro campo (precisão)
LET P=VAL A$( TO USR RD) : REM READ - quarto campo (custo de energia)
```

**Implicação direta para a conversão em C:** isso é exatamente por que faz sentido representar essas tabelas como **JSON estruturado** em vez de replicar o truque de "ler o próprio código-fonte" — o C já tem `struct`/arrays/parsers de sobra; o hack só existia por limitação do ZX81 BASIC.

---

## 4. Tabelas de dados extraídas (prontas para virar JSON/CSV)

### 4.1 Tipos de sala (linhas REM 9110-9220)

```
0  Cela de Detenção
1  Depósito de Armas
2  Enfermaria
3  Sala do Pessoal
4  Oficina
5  Controle Auxiliar
6  Sala do Gerador
7  Armazém
8  Refeitório
9  Depósito de Lixo
10 Controle Central
11 Sala de Teleporte   (reservada — sempre a sala inicial/final)
```

### 4.2 Armas (linhas REM 9310-9400) — campos: nome, dano_máximo, chance_acerto(%), custo_energia

```
0  Dardos,            2,  25, 0
1  Bumerangue,        1,  50, 0
2  Pistola Laser,     4,  60, 12
3  Rifle Phaser,      5,  70, 17
4  Arpão Iônico,      8,  50, 20
5  Revólver Sônico,   20, 40, 10
6  Sabre Iônico,      4,  40, 8
7  Pistola Fotônica,  4,  45, 8
8  Bisturi Laser,     5,  85, 5
9  Espada Laser,      5,  35, 9
```

### 4.3 Tripulantes/monstros (linhas REM 9510-9700) — campos: nome, frase, vida, agressivo(0/1), id_arma

```
0  Comandante Arconida,       Tenha cuidado,                        18, 1, 2
1  Piloto Betiano,             Poderia ser pior,                     10, 1, 7
2  Piloto Auxiliar,             Não se preocupe,                      8,  1, 7
3  Navegador Ackio,              Arrogante mas fraco,                   12, 1, 6
4  Navegador Zorvita,             Traiçoeiro,                            12, 1, 6
5  Oficial de Comunicações,        Fique frio,                             7,  1, 5
6  Robot de Segurança M-1,          Fácil,                                   5,  0, 4
7  Robot de Segurança M-2,           Não muito fácil,                         10, 0, 2
8  Robot de Segurança M-3,            Altamente perigoso,                      20, 0, 3
9  Servente Klaxon,                    Criatura patética,                       4,  0, 1
10 Oficial de Segurança,                Corra pela sua vida,                      40, 1, 5
11 Médico Buteriano,                     Inteligente mas pouco perigoso,            8,  1, 8
12 Enfermeiro Golgariano,                 Não é problema,                            7,  1, 8
13 Engenheiro de Bordo,                    Esse não vai ser fácil,                    20, 1, 9
14 Andróide Defeituoso,                     Vai sair faísca,                           15, 0, 4
15 Mecânico Torkral,                         Criatura divertida,                        8,  0, 0
16 Mecânico Rigeliano,                        Perigoso mas pouco resistente,              5,  0, 3
17 Carregador Frix,                            Pobre criatura,                              4,  0, 0
18 Carregador Arbock,                           Pouco amigável,                              20, 0, 1
19 Carregador Falita,                            Feio mas fraco,                              4,  0, 9
```

*(`id_arma` refere-se ao índice da tabela de armas acima — é a arma que o tripulante usa para revidar.)*

---

## 5. Filosofia da versão em C

1. **Dados fora do código.** `config.json`, `rooms.json`, `weapons.json`, `crew.json` — carregados uma vez no início via `cJSON`. Balancear o jogo (ex.: mudar dano de uma arma, adicionar uma sala) deve ser possível **sem recompilar**.

2. **Configurável de verdade.** O tamanho do grid da nave (`grid_size`) e as chances de eventos aleatórios (tripulante na sala, item na sala, acidente no escuro, portas extras no labirinto) vivem em `config.json`, não em `#define`.

3. **Separação por responsabilidade, não por conveniência.** Cada módulo faz uma coisa: o que existe (tipos/dados) fica separado de o que acontece (lógica de jogo), que fica separado de como é mostrado na tela (ncurses). Isso permite, por exemplo, trocar ncurses por outra biblioteca de UI, ou testar a lógica de jogo sem terminal nenhum, sem tocar no resto.

4. **Labirinto sempre solucionável.** O original sorteava portas de forma independente por sala/direção, o que podia (na teoria) gerar salas inalcançáveis. A versão em C deve usar um gerador de labirinto que **garanta conectividade total** (ex.: busca em profundidade aleatória / randomized DFS a partir da Sala de Teleporte), com portas extras adicionadas depois para criar atalhos/ciclos.

5. **Fidelidade nas regras, liberdade na implementação.** Mecânicas centrais do original devem ser preservadas (dez comandos, vida vs. energia como recursos distintos, sala escura como risco, teleporte como única forma de "vencer" preservando o que foi coletado, morte = perde tudo). Detalhes de fórmulas obscuras do BASIC original (algumas constantes ficaram ofuscadas via truques tipo `CODE"?"` para economizar bytes) podem ser substituídos por valores razoáveis — não é necessário decifrar cada fórmula original byte a byte.

6. **Comentário generoso.** Cada função não-trivial deve explicar o "porquê" da escolha, e referenciar a linha/rotina do BASIC original quando aplicável — isso é tanto documentação quanto prova de rastreabilidade da engenharia reversa.

---

## 6. Esboço de arquitetura da versão em C

```
aventureiro/
  data/
    config.json      -> grid_size, chances de eventos, stats iniciais do jogador
    rooms.json        -> tabela de tipos de sala (secao 4.1)
    weapons.json        -> tabela de armas (secao 4.2)
    crew.json             -> tabela de tripulantes (secao 4.3)
  src/
    cJSON.h / cJSON.c        -> biblioteca de terceiros vendorizada (parsing JSON)
    util.h                     -> sorteios (rand em [0,n), chance %, intervalo) - so header, inline
    types.h                      -> structs "substantivo": Config, TipoSala, Arma, Tripulante, BaseDeDados
    data_loader.h / .c             -> le os .json e preenche BaseDeDados
    map.h / map.c                    -> Celula, Mapa; geracao do labirinto (DFS + portas extras); povoamento de salas/itens/tripulantes
    player.h / player.c                -> struct Jogador; iniciar, adicionar arma, usar medicamento
    combat.h / combat.c                  -> atacar, contra-ataque, fugir, comunicar, examinar sala, escudo
    ui.h / ui.c                            -> tudo que toca ncurses: HUD, log de mensagens rolante, prompt, leitura de tecla
    game.h / game.c                          -> o "maestro": tela de titulo, entrar em sala, loop de comandos, telas de fim
    main.c                                     -> parse de args, seed do RNG, carregar dados, iniciar/encerrar ncurses
  Makefile
```

### Por que essa ordem de dependências

`types.h` não depende de nada (é só structs). `map.h` depende só de `types.h`. `player.h` depende de `types.h` + `map.h` (o jogador precisa saber onde é a sala de teleporte para nascer lá). `combat.h` e `game.h` são os únicos que enxergam tudo, porque são a camada de orquestração/interação.

`ui.h` é isolado de propósito: é o único lugar que sabe que existe ncurses. Um log de mensagens (`ui_log(fmt, ...)`) desacopla a lógica de jogo de como o texto aparece na tela.

### Pontos de atenção específicos (aprendidos ao prototipar uma primeira versão)

- **GCC `-Warray-bounds` com enums usados como índice de array**, especialmente depois de inlining em `-O2`: prefira `int` simples para índices de laços/arrays e faça cast explícito para o tipo enum só onde a função realmente exige (ex.: `direcao_nome((Direcao)d)`). Um clamp defensivo (`if (x < 0) x = 0;` etc.), além de ser mais seguro, também costuma silenciar falsos positivos do analisador estático.
- **`rand()` exige `<stdlib.h>`** — fácil de esquecer em arquivos que só usam via uma função utilitária de outro header.
- Testar um binário ncurses sem terminal interativo de verdade é possível com `pexpect` (Python) simulando teclas via pty, e opcionalmente `pyte` para renderizar a tela sem os códigos ANSI — útil para smoke test e fuzzing de comandos aleatórios antes de jogar manualmente.

---

## 7. Decisões que ainda precisam ser validadas na próxima sessão

- [ ] Tamanho de grid padrão (o original usava 8x8) — manter como default em `config.json`?
- [ ] Fórmulas de dano/cura/loot: usar valores "razoáveis" definidos livremente, ou tentar recuperar as constantes ofuscadas do BASIC original (`CODE"X"` etc.)?
- [ ] Nível de fidelidade da fuga/perseguição de inimigos (o original tinha uma lógica de perseguição multi-sala nas linhas 6800-6920 que pode ser simplificada).
- [ ] Quer um mapa visual (ASCII) na tela, ou manter 100% textual como o original (sem visualização de mapa, só descrição da sala atual)?
- [ ] Salvar/carregar progresso entre sessões, ou manter (como o original) uma partida = uma sessão contínua?

---

## 8. Resumo de uma frase

**O ZX81 não tinha `DATA/READ/RESTORE`, então o autor original escondeu uma mini-implementação disso em Z80 dentro de uma linha `REM`, usando o próprio código-fonte como banco de dados — e a versão em C deve substituir esse truque por JSON de verdade, mantendo as mesmas regras de jogo (dez comandos, vida vs. energia, sala escura, teleporte como saída "vitoriosa") numa arquitetura modular e configurável.**
