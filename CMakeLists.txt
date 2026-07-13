cmake_minimum_required(VERSION 3.16)
project(aventureiro C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_compile_options(-Wall -Wextra -Werror)

# ncursesw via pkg-config, nao find_package(Curses): o FindCurses generico do
# CMake nao distingue de forma confiavel a variante wide da narrow, e foi
# exatamente a lib narrow a causa raiz do bug de acentos quebrados no Linux
# (Pacote 16, management/backlog/16-bug-acentos-linux.md). pkg_check_modules
# com REQUIRED falha o configure com mensagem clara se ncursesw nao for
# encontrada, em vez de linkar a lib errada silenciosamente.
find_package(PkgConfig REQUIRED)
pkg_check_modules(NCURSESW REQUIRED IMPORTED_TARGET ncursesw)

add_executable(aventureiro
    src/cJSON.c
    src/data_loader.c
    src/map.c
    src/player.c
    src/combat.c
    src/ui.c
    src/game.c
    src/main.c
)
target_link_libraries(aventureiro PRIVATE PkgConfig::NCURSESW)

# Alvo de teste (Pacote 9): fuzzing basico via pexpect (tests/smoke_test.py).
# Python3 e' dependencia so' do teste, nao do build do jogo - se nao for
# encontrado, o configure segue normalmente e so' avisa que o alvo nao vai
# estar disponivel. find_program (nao find_package(Python3)) de proposito:
# resolve pelo PATH igual o antigo `python3 tests/smoke_test.py` do Makefile -
# find_package(Python3) as vezes prefere um Python de framework do sistema
# (sem pexpect instalado) em vez do interpretador ativo no PATH do usuario.
find_program(PYTHON3_EXECUTABLE NAMES python3)
if(PYTHON3_EXECUTABLE)
    enable_testing()
    # enable_testing() ja' cria um alvo 'test' (via CTest) que roda todo teste
    # registrado com add_test() - nao precisa de um add_custom_target proprio,
    # CMake reserva esse nome e rejeita redefini-lo.
    # WORKING_DIRECTORY = CMAKE_SOURCE_DIR (nao o default do CTest, que e'
    # CMAKE_BINARY_DIR): o jogo espera 'data/' relativo ao cwd (default de
    # --data-dir em main.c) - sem isso o binario morre na hora (falha ao
    # carregar dados) e o teste so' ve um EOF confuso, nao o erro real.
    add_test(NAME smoke_test
        COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/smoke_test.py $<TARGET_FILE:aventureiro>
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
else()
    message(WARNING "python3 nao encontrado - alvo 'test' (tests/smoke_test.py) indisponivel. "
                     "O jogo compila normalmente; python3 so' e' necessario para o fuzzing automatizado.")
endif()
