#include "data_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

static char *ler_arquivo(const char *caminho) {
    FILE *f = fopen(caminho, "rb");
    if (!f) {
        fprintf(stderr, "data_loader: nao foi possivel abrir '%s'\n", caminho);
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long tamanho = ftell(f);
    if (tamanho < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    char *buffer = malloc((size_t)tamanho + 1);
    if (!buffer) {
        fclose(f);
        return NULL;
    }

    size_t lido = fread(buffer, 1, (size_t)tamanho, f);
    fclose(f);
    buffer[lido] = '\0';
    return buffer;
}

/* 'texto_bruto_out' precisa sobreviver enquanto o cJSON* retornado for usado
 * (cJSON referencia as strings do buffer original em vez de copia-las). */
static cJSON *carregar_json(const char *diretorio, const char *nome_arquivo, char **texto_bruto_out) {
    char caminho[512];
    snprintf(caminho, sizeof(caminho), "%s/%s", diretorio, nome_arquivo);

    char *texto = ler_arquivo(caminho);
    if (!texto) {
        return NULL;
    }

    cJSON *json = cJSON_Parse(texto);
    if (!json) {
        const char *erro = cJSON_GetErrorPtr();
        fprintf(stderr, "data_loader: JSON invalido em '%s' (perto de: %.40s)\n", caminho, erro ? erro : "?");
        free(texto);
        return NULL;
    }

    *texto_bruto_out = texto;
    return json;
}

static bool copiar_string(const cJSON *objeto, const char *campo, char *destino, size_t tamanho_destino) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(objeto, campo);
    if (!cJSON_IsString(v) || v->valuestring == NULL) {
        return false;
    }
    snprintf(destino, tamanho_destino, "%s", v->valuestring);
    return true;
}

static bool copiar_int(const cJSON *objeto, const char *campo, int *destino) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(objeto, campo);
    if (!cJSON_IsNumber(v)) {
        return false;
    }
    *destino = v->valueint;
    return true;
}

/* 'teleporte' e o unico campo opcional dos JSONs - so a Sala de Teleporte o
 * declara; toda outra sala e implicitamente teleporte=false. */
static void copiar_bool_opcional(const cJSON *objeto, const char *campo, bool *destino, bool padrao) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(objeto, campo);
    *destino = cJSON_IsBool(v) ? cJSON_IsTrue(v) : padrao;
}

static bool copiar_bool_obrigatorio(const cJSON *objeto, const char *campo, bool *destino) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(objeto, campo);
    if (!cJSON_IsBool(v)) {
        return false;
    }
    *destino = cJSON_IsTrue(v);
    return true;
}

static bool carregar_config(const char *dir, Config *cfg) {
    char *texto = NULL;
    cJSON *raiz = carregar_json(dir, "config.json", &texto);
    if (!raiz) {
        return false;
    }

    bool ok = copiar_int(raiz, "grid_size", &cfg->grid_size);

    const cJSON *jogador = cJSON_GetObjectItemCaseSensitive(raiz, "jogador");
    ok = ok && cJSON_IsObject(jogador);
    ok = ok && copiar_int(jogador, "vida_inicial", &cfg->vida_inicial);
    ok = ok && copiar_int(jogador, "energia_inicial", &cfg->energia_inicial);
    ok = ok && copiar_int(jogador, "dinheiro_inicial", &cfg->dinheiro_inicial);
    ok = ok && copiar_int(jogador, "medicamentos_iniciais", &cfg->medicamentos_iniciais);

    const cJSON *chances = cJSON_GetObjectItemCaseSensitive(raiz, "chances_percentual");
    ok = ok && cJSON_IsObject(chances);
    ok = ok && copiar_int(chances, "tripulante_na_sala", &cfg->chance_tripulante_na_sala);
    ok = ok && copiar_int(chances, "item_na_sala", &cfg->chance_item_na_sala);
    ok = ok && copiar_int(chances, "sala_escura", &cfg->chance_sala_escura);
    ok = ok && copiar_int(chances, "acidente_no_escuro", &cfg->chance_acidente_no_escuro);
    ok = ok && copiar_int(chances, "porta_extra_labirinto", &cfg->chance_porta_extra_labirinto);

    if (!ok) {
        fprintf(stderr, "data_loader: campo faltando ou invalido em config.json\n");
    }

    cJSON_Delete(raiz);
    free(texto);
    return ok;
}

static bool carregar_salas(const char *dir, BaseDeDados *bd) {
    char *texto = NULL;
    cJSON *raiz = carregar_json(dir, "rooms.json", &texto);
    if (!raiz) {
        return false;
    }

    const cJSON *salas = cJSON_GetObjectItemCaseSensitive(raiz, "salas");
    if (!cJSON_IsArray(salas)) {
        fprintf(stderr, "data_loader: rooms.json sem array 'salas'\n");
        cJSON_Delete(raiz);
        free(texto);
        return false;
    }

    int n = cJSON_GetArraySize(salas);
    if (n > MAX_SALAS) {
        fprintf(stderr, "data_loader: rooms.json tem %d salas, maximo suportado e %d\n", n, MAX_SALAS);
        cJSON_Delete(raiz);
        free(texto);
        return false;
    }

    bool ok = true;
    for (int i = 0; i < n && ok; i++) {
        const cJSON *item = cJSON_GetArrayItem(salas, i);
        TipoSala *sala = &bd->salas[i];
        ok = ok && copiar_int(item, "id", &sala->id);
        ok = ok && copiar_string(item, "nome", sala->nome, sizeof(sala->nome));
        copiar_bool_opcional(item, "teleporte", &sala->teleporte, false);
    }

    if (!ok) {
        fprintf(stderr, "data_loader: campo invalido em rooms.json\n");
    } else {
        bd->num_salas = n;
    }

    cJSON_Delete(raiz);
    free(texto);
    return ok;
}

static bool carregar_armas(const char *dir, BaseDeDados *bd) {
    char *texto = NULL;
    cJSON *raiz = carregar_json(dir, "weapons.json", &texto);
    if (!raiz) {
        return false;
    }

    const cJSON *armas = cJSON_GetObjectItemCaseSensitive(raiz, "armas");
    if (!cJSON_IsArray(armas)) {
        fprintf(stderr, "data_loader: weapons.json sem array 'armas'\n");
        cJSON_Delete(raiz);
        free(texto);
        return false;
    }

    int n = cJSON_GetArraySize(armas);
    if (n > MAX_ARMAS) {
        fprintf(stderr, "data_loader: weapons.json tem %d armas, maximo suportado e %d\n", n, MAX_ARMAS);
        cJSON_Delete(raiz);
        free(texto);
        return false;
    }

    bool ok = true;
    for (int i = 0; i < n && ok; i++) {
        const cJSON *item = cJSON_GetArrayItem(armas, i);
        Arma *arma = &bd->armas[i];
        ok = ok && copiar_int(item, "id", &arma->id);
        ok = ok && copiar_string(item, "nome", arma->nome, sizeof(arma->nome));
        ok = ok && copiar_int(item, "dano_maximo", &arma->dano_maximo);
        ok = ok && copiar_int(item, "chance_acerto_percentual", &arma->chance_acerto_percentual);
        ok = ok && copiar_int(item, "custo_energia", &arma->custo_energia);
    }

    if (!ok) {
        fprintf(stderr, "data_loader: campo invalido em weapons.json\n");
    } else {
        bd->num_armas = n;
    }

    cJSON_Delete(raiz);
    free(texto);
    return ok;
}

/* Carrega por ultimo porque valida id_arma contra bd->num_armas, ja
 * preenchido por carregar_armas. */
static bool carregar_tripulantes(const char *dir, BaseDeDados *bd) {
    char *texto = NULL;
    cJSON *raiz = carregar_json(dir, "crew.json", &texto);
    if (!raiz) {
        return false;
    }

    const cJSON *tripulantes = cJSON_GetObjectItemCaseSensitive(raiz, "tripulantes");
    if (!cJSON_IsArray(tripulantes)) {
        fprintf(stderr, "data_loader: crew.json sem array 'tripulantes'\n");
        cJSON_Delete(raiz);
        free(texto);
        return false;
    }

    int n = cJSON_GetArraySize(tripulantes);
    if (n > MAX_TRIPULANTES) {
        fprintf(stderr, "data_loader: crew.json tem %d tripulantes, maximo suportado e %d\n", n, MAX_TRIPULANTES);
        cJSON_Delete(raiz);
        free(texto);
        return false;
    }

    bool ok = true;
    for (int i = 0; i < n && ok; i++) {
        const cJSON *item = cJSON_GetArrayItem(tripulantes, i);
        Tripulante *tripulante = &bd->tripulantes[i];
        ok = ok && copiar_int(item, "id", &tripulante->id);
        ok = ok && copiar_string(item, "nome", tripulante->nome, sizeof(tripulante->nome));
        ok = ok && copiar_string(item, "frase", tripulante->frase, sizeof(tripulante->frase));
        ok = ok && copiar_int(item, "vida", &tripulante->vida);
        ok = ok && copiar_bool_obrigatorio(item, "agressivo", &tripulante->agressivo);
        ok = ok && copiar_int(item, "id_arma", &tripulante->id_arma);
        if (ok && (tripulante->id_arma < 0 || tripulante->id_arma >= bd->num_armas)) {
            fprintf(stderr, "data_loader: tripulante '%s' referencia id_arma %d fora do intervalo (0..%d)\n",
                    tripulante->nome, tripulante->id_arma, bd->num_armas - 1);
            ok = false;
        }
    }

    if (!ok) {
        fprintf(stderr, "data_loader: campo invalido em crew.json\n");
    } else {
        bd->num_tripulantes = n;
    }

    cJSON_Delete(raiz);
    free(texto);
    return ok;
}

bool carregar_dados(const char *diretorio_dados, BaseDeDados *bd, Config *cfg) {
    memset(bd, 0, sizeof(*bd));
    memset(cfg, 0, sizeof(*cfg));

    /* Ordem importa: tripulantes valida id_arma contra armas ja carregadas. */
    return carregar_config(diretorio_dados, cfg)
        && carregar_salas(diretorio_dados, bd)
        && carregar_armas(diretorio_dados, bd)
        && carregar_tripulantes(diretorio_dados, bd);
}
