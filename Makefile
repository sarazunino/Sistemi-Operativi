CC = gcc
CFLAGS = -Wvla -Wextra -Werror -D_GNU_SOURCE -Iinclude -g

SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj
BIN_DIR = bin

COMMON_SRCS = $(wildcard $(SRC_DIR)/common/*.c)
COMMON_OBJS = $(patsubst $(SRC_DIR)/common/%.c, $(OBJ_DIR)/%.o, $(COMMON_SRCS))

TARGETS = \
	$(BIN_DIR)/responsabile_mensa \
	$(BIN_DIR)/operatore \
	$(BIN_DIR)/utente \
	$(BIN_DIR)/add_users \
	$(BIN_DIR)/communication_disorder

all: dirs $(TARGETS)

dirs:
	@mkdir -p $(OBJ_DIR) $(BIN_DIR) output logs

$(OBJ_DIR)/%.o: $(SRC_DIR)/common/%.c $(INC_DIR)/*.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR)/responsabile_mensa: $(OBJ_DIR)/main_responsabile.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) $^ -o $@

$(BIN_DIR)/operatore: $(OBJ_DIR)/main_operatore.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) $^ -o $@

$(BIN_DIR)/utente: $(OBJ_DIR)/main_utente.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) $^ -o $@

$(BIN_DIR)/add_users: $(OBJ_DIR)/main_add_users.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) $^ -o $@

$(BIN_DIR)/communication_disorder: $(OBJ_DIR)/main_communication_disorder.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) $^ -o $@

$(OBJ_DIR)/main_responsabile.o: $(SRC_DIR)/responsabile_mensa/main.c $(INC_DIR)/*.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/main_operatore.o: $(SRC_DIR)/operatore/main.c $(INC_DIR)/*.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/main_utente.o: $(SRC_DIR)/utente/main.c $(INC_DIR)/*.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/main_add_users.o: $(SRC_DIR)/add_users/main.c $(INC_DIR)/*.h
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/main_communication_disorder.o: $(SRC_DIR)/communication_disorder/main.c $(INC_DIR)/*.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR)/* $(BIN_DIR)/* output/* logs/*

run: all
	./$(BIN_DIR)/responsabile_mensa config/config.conf

.PHONY: all dirs clean run