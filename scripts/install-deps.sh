#!/usr/bin/env bash
#
# Verifica (e, com confirmação, instala) as dependências de build do
# "Aventureiro": compilador C, cmake, pkg-config, headers de ncurses, e
# python3/pip (só para o alvo `test`, que usa pexpect). Não instala nada
# silenciosamente - sempre mostra o comando antes de rodar e pede
# confirmação, já que isso mexe em pacotes do sistema.
#
# Nota: cmake ainda depende de um gerador por baixo (Unix Makefiles por
# padrão em Linux/macOS, ou seja, `make` continua sendo puxado como
# dependencia transitiva do pacote de build essencial em cada distro abaixo -
# so' nao e' mais checado/chamado diretamente por este script).
#
# Uso: ./scripts/install-deps.sh
set -euo pipefail

verde() { printf '\033[32m%s\033[0m\n' "$1"; }
amarelo() { printf '\033[33m%s\033[0m\n' "$1"; }
vermelho() { printf '\033[31m%s\033[0m\n' "$1"; }

confirmar() {
    local pergunta="$1"
    read -r -p "$pergunta [s/N] " resposta
    case "$resposta" in
        [sS]|[sS][iI][mM]) return 0 ;;
        *) return 1 ;;
    esac
}

tem_comando() { command -v "$1" >/dev/null 2>&1; }

# --- Deteccao de SO/gerenciador de pacotes ---------------------------------

SO=""
GERENCIADOR=""

if [[ "$(uname -s)" == "Darwin" ]]; then
    SO="macos"
    GERENCIADOR="brew"
elif [[ -f /etc/os-release ]]; then
    SO="linux"
    . /etc/os-release
    case "${ID:-}${ID_LIKE:-}" in
        *debian*|*ubuntu*) GERENCIADOR="apt" ;;
        *fedora*|*rhel*)   GERENCIADOR="dnf" ;;
        *arch*)            GERENCIADOR="pacman" ;;
        *) GERENCIADOR="" ;;
    esac
fi

if [[ -z "$SO" ]]; then
    vermelho "SO nao reconhecido automaticamente (nem macOS nem /etc/os-release)."
    echo "Este script cobre so Linux (apt/dnf/pacman) e macOS (brew) - ver management/ideias-futuras.md."
    echo "Instale manualmente: compilador C, cmake, pkg-config, headers de ncurses, python3 e pip."
    exit 1
fi

echo "SO detectado: $SO${GERENCIADOR:+ (gerenciador: $GERENCIADOR)}"
echo

# --- Checagem de dependencias ----------------------------------------------

FALTANDO=()

tem_comando cc || tem_comando gcc || tem_comando clang || FALTANDO+=("compilador C")
tem_comando cmake || FALTANDO+=("cmake")
tem_comando pkg-config || FALTANDO+=("pkg-config")

if tem_comando pkg-config && ! pkg-config --exists ncursesw; then
    FALTANDO+=("ncursesw (dev headers)")
elif ! tem_comando pkg-config; then
    FALTANDO+=("ncursesw (dev headers)")
fi

tem_comando python3 || FALTANDO+=("python3")

if [[ ${#FALTANDO[@]} -eq 0 ]]; then
    verde "Tudo certo: compilador C, cmake, pkg-config e ncursesw ja estao instalados."
else
    amarelo "Faltando: ${FALTANDO[*]}"
    echo

    COMANDO_INSTALL=""
    case "$SO-$GERENCIADOR" in
        macos-brew)
            if ! tem_comando brew; then
                vermelho "Homebrew nao encontrado. Instale primeiro: https://brew.sh"
                echo "Depois de instalar o Homebrew, rode este script de novo."
                exit 1
            fi
            if ! tem_comando cc && ! tem_comando clang; then
                amarelo "Tambem falta o compilador (Xcode Command Line Tools)."
                echo "Rode 'xcode-select --install' manualmente (abre um instalador grafico) e confirme antes de continuar."
            fi
            COMANDO_INSTALL="brew install cmake pkg-config ncurses"
            ;;
        linux-apt)
            COMANDO_INSTALL="sudo apt-get update && sudo apt-get install -y build-essential cmake pkg-config libncurses-dev python3 python3-pip"
            ;;
        linux-dnf)
            COMANDO_INSTALL="sudo dnf install -y gcc make cmake pkgconf-pkg-config ncurses-devel python3 python3-pip"
            ;;
        linux-pacman)
            COMANDO_INSTALL="sudo pacman -S --needed base-devel cmake pkgconf ncurses python python-pip"
            ;;
        *)
            vermelho "Distribuicao Linux nao reconhecida (ID/ID_LIKE de /etc/os-release nao bate com apt/dnf/pacman)."
            echo "Instale manualmente: compilador C, cmake, pkg-config, headers de ncurses, python3 e pip."
            exit 1
            ;;
    esac

    echo "Comando sugerido:"
    echo "  $COMANDO_INSTALL"
    echo
    if confirmar "Rodar esse comando agora?"; then
        eval "$COMANDO_INSTALL"
    else
        echo "Ok, nada instalado. Rode o comando acima manualmente quando quiser."
    fi
fi

# --- Dependencia opcional: pexpect (so' para o alvo 'test' do CMake) -------

echo
if tem_comando python3 && python3 -c "import pexpect" >/dev/null 2>&1; then
    verde "pexpect ja instalado (alvo 'test' do CMake pronto pra rodar)."
else
    amarelo "pexpect nao encontrado - so' e' necessario para o alvo 'test' (fuzzing automatizado), nao para jogar."
    echo "Comando sugerido:"
    echo "  pip3 install -r tests/requirements.txt"
    echo
    if confirmar "Rodar esse comando agora?"; then
        pip3 install -r tests/requirements.txt
    else
        echo "Ok, nada instalado. Rode o comando acima manualmente se for usar o alvo 'test'."
    fi
fi
