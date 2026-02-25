CC = gcc
CFLAGS = -Wall -Wextra -O3 -Iinclude
SRC_DIR = src
OBJ_DIR = obj
BIN = zippo

SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR) $(BIN)

debug: CFLAGS = -Wall -Wextra -g -O0 -Iinclude
debug: clean $(BIN)

.PHONY: all clean
