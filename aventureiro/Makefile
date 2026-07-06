CC := cc
CFLAGS := -std=c11 -Wall -Wextra -Werror -g
LDLIBS :=
SRC_DIR := src
BUILD_DIR := build
BIN := $(BUILD_DIR)/aventureiro

# Pacote 6a: main.c roda asserts sobre combat.c (comandos basicos), alem de
# player.c, mapa e dados.
SRCS := $(SRC_DIR)/cJSON.c $(SRC_DIR)/data_loader.c $(SRC_DIR)/map.c $(SRC_DIR)/player.c $(SRC_DIR)/combat.c $(SRC_DIR)/main.c
OBJS := $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

.PHONY: all clean test run

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDLIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

run: $(BIN)
	./$(BIN)

clean:
	rm -rf $(BUILD_DIR)

test:
	@echo "Nenhum teste automatizado ainda (chega no Pacote 9)."
