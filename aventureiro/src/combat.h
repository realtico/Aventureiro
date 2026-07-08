#ifndef AVENTUREIRO_COMBAT_H
#define AVENTUREIRO_COMBAT_H

#include <stdbool.h>

#include "types.h"

#define MAX_MENSAGENS_RESULTADO 8
#define MAX_TAMANHO_MENSAGEM 96

/*
 * Buffer de mensagens narrativas produzidas por um comando, no lugar de
 * imprimir direto na tela - isso e' responsabilidade da camada de UI
 * (Pacote 7), que ainda nao existe. 'sucesso' indica se a acao pretendida
 * de fato aconteceu (ex.: false se tentou mover para uma direcao sem
 * porta); 'jogador_morreu' sinaliza vida chegando a zero (por acidente no
 * escuro nesta fase - combate propriamente dito e' o Pacote 6b) para quem
 * orquestra o jogo (Pacote 8) decidir o que fazer.
 */
typedef struct {
    char mensagens[MAX_MENSAGENS_RESULTADO][MAX_TAMANHO_MENSAGEM];
    int num_mensagens;
    bool sucesso;
    bool jogador_morreu;
} Resultado;

/*
 * Mover (linha 1004): anda na direcao indicada, se houver porta na celula
 * atual e nao houver tripulante vivo na sala bloqueando a saida (linha
 * 1004-1007 do original: "nao se arrisque dando as costas" ao tripulante -
 * resolver o encontro e' pre-requisito pra sair da sala). Ao entrar na
 * sala nova, narra tipo/saidas/tripulante (sub-rotina "entrar em sala
 * nova", linha 6002) e drena energia do escudo se estiver ligado (linha
 * 6115 + 3600).
 */
Resultado comando_mover(Jogador *jogador, Mapa *mapa, const BaseDeDados *bd, Direcao direcao);

/*
 * Trocar de arma (linha 2502): troca para a arma de indice
 * 'indice_no_inventario' dentro do inventario do jogador. Falha (sucesso
 * = false) se so houver uma arma (linha 2505: "voce so tem uma arma") ou
 * se o indice for invalido. O contra-ataque do tripulante apos trocar de
 * arma na presenca de um inimigo (linhas 2655-2670 do original) fica para
 * o Pacote 6b/8 ligarem, ja que depende de combate.
 */
Resultado comando_trocar_arma(Jogador *jogador, const BaseDeDados *bd, int indice_no_inventario);

/*
 * Escudo (linha 3501): liga/desliga o escudo. Falha se a energia estiver
 * zerada ou negativa (linha 3502: "energia insuficiente").
 */
Resultado comando_escudo(Jogador *jogador);

/*
 * Usar medicamento (linha 4010): wrapper sobre jogador_usar_medicamento
 * (Pacote 5/12) que acrescenta a narracao (sem medicamento / vida ja cheia /
 * curou). Curar com sucesso conta como uma rodada plena (linha 4100:
 * "GOTO 6500") - se houver tripulante vivo na sala, ele reage exatamente
 * como apos um ataque ou fuga, por isso a assinatura recebe 'mapa'/'bd'.
 */
Resultado comando_usar_medicamento(Jogador *jogador, Mapa *mapa, const BaseDeDados *bd, const Config *cfg);

/*
 * Situacao (linha 4510): relatorio somente-leitura de vida, medicamento,
 * energia, dinheiro, armas e arma atual.
 */
Resultado comando_situacao(const Jogador *jogador, const BaseDeDados *bd);

/*
 * Descreve tipo de sala/saidas/tripulante da sala atual do jogador, sem
 * narrar "entrada" nem drenar escudo - e' o mesmo conteudo que "entrar em
 * sala nova" (linha 6002) produz ao mover, exposto aqui para o comando
 * Examinar (Pacote 11) reaproveitar em vez de o jogador precisar sair e
 * voltar na sala so pra reler a descricao.
 */
void combat_narrar_sala_atual(const Jogador *jogador, const Mapa *mapa, const BaseDeDados *bd, Resultado *r);

/*
 * Examinar a sala (linha 5001): falha se houver tripulante vivo na sala
 * (linha 5002). Em sala clara, examina sem risco nem custo (linha
 * 5140-5150). Em sala escura, 'usar_lanterna' decide o caminho: com
 * lanterna gasta energia (linha 5210, CUSTO_ENERGIA_LANTERNA) sem risco;
 * sem lanterna arrisca acidente (linha 7000, Config::chance_acidente_no_escuro).
 * Se encontrar item nao coletado, sorteia o tipo (energia/medicamento/
 * dinheiro - o mapa (Pacote 4) simplificou o item para um bool, sem tipo
 * pre-definido por sala) e aplica o ganho.
 */
Resultado comando_examinar_sala(Jogador *jogador, Mapa *mapa, const Config *cfg, bool usar_lanterna);

/*
 * Acionar teleporte (linha 5510): so funciona parado na Sala de Teleporte
 * e sem tripulante vivo na sala (esta segunda checagem nunca deveria
 * disparar de fato, pois o gerador de mapa - Pacote 4 - garante que a Sala
 * de Teleporte nasce sempre "segura"; mantida por fidelidade/defesa).
 * sucesso = true significa que o jogador venceu a partida.
 */
Resultado comando_acionar_teleporte(Jogador *jogador, const Mapa *mapa);

/*
 * Atacar (linha 1505): ataca o tripulante da sala atual com a arma em uso.
 * Falha (sucesso = false) sem ninguem na sala ou sem energia para a arma.
 * Ao matar, sorteia loot (arma do tripulante, energia, e - so se
 * agressivo, linha 1800 - chance de medicamento/dinheiro). Se o
 * tripulante sobreviver ao ataque (errou ou nao matou), reage: agressivo
 * contra-ataca (linha 6505), nao-agressivo entra em panico e foge (linha
 * 6800).
 */
Resultado comando_atacar(Jogador *jogador, Mapa *mapa, const BaseDeDados *bd);

/*
 * Fugir (linha 2010): falha sem tripulante na sala. 10% de chance de cair
 * na tentativa (linha 2040), o que da uma reacao gratis ao tripulante.
 * Tendo sucesso, foge para uma sala vizinha aleatoria entre as conectadas
 * - versao simplificada, decisao do Pacote 0: sem o tripulante perseguir
 * pra sala nova (linhas 6800-6920 do original tinham essa perseguicao
 * multi-sala, removida de proposito).
 */
Resultado comando_fugir(Jogador *jogador, Mapa *mapa, const BaseDeDados *bd);

/* As tres posturas de "Comunicar-se" (linha 3010), escolhidas pelo chamador
 * ja que este pacote nao faz I/O interativo. */
typedef enum {
    COMUNICAR_SUBORNAR,
    COMUNICAR_IRRITAR,
    COMUNICAR_AMIGAVEL
} AcaoComunicar;

/*
 * Comunicar-se (linha 3010): falha sem tripulante na sala, ou se ele nao
 * for agressivo (linha 3040 - so tripulantes "inteligentes" conversam;
 * robots/serventes nao). 'valor_oferecido' so importa para
 * COMUNICAR_SUBORNAR (quanto dinheiro oferecer). Cada postura tem sua
 * propria chance de sucesso (linhas 3130-3410); fracasso aciona a reacao
 * do tripulante, igual ao ataque.
 */
Resultado comando_comunicar(Jogador *jogador, Mapa *mapa, const BaseDeDados *bd, AcaoComunicar acao, int valor_oferecido);

#endif
