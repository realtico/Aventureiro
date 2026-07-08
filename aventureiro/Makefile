CC := cc
CFLAGS := -std=c11 -Wall -Wextra -Werror -g
NCURSES_CFLAGS := $(shell pkg-config --cflags ncurses)
NCURSES_LIBS := $(shell pkg-config --libs ncurses)
LDLIBS :=
SRC_DIR := src
BUILD_DIR := build
BIN := $(BUILD_DIR)/aventureiro

SRCS := $(SRC_DIR)/cJSON.c $(SRC_DIR)/data_loader.c $(SRC_DIR)/map.c $(SRC_DIR)/player.c \
        $(SRC_DIR)/combat.c $(SRC_DIR)/ui.c $(SRC_DIR)/game.c $(SRC_DIR)/main.c
OBJS := $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

.PHONY: all clean test run

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDLIBS) $(NCURSES_LIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(NCURSES_CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

run: $(BIN)
	./$(BIN)

# Pacote 9: fuzzing basico via pexpect (tests/smoke_test.py) - manda teclas
# aleatorias por varias partidas e falha se o binario crashar ou travar.
# Requer 'pip install -r tests/requirements.txt'.
test: $(BIN)
	python3 tests/smoke_test.py $(BIN)

clean:
	rm -rf $(BUILD_DIR)
