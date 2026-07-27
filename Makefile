# Makefile - compila tutti gli eseguibili del progetto
# Ogni tipo di processo (responsabile_mensa, operatore, utente, ...)
# diventa un eseguibile separato in bin/, lanciato poi con execve().
# "operatore" serve indifferentemente una qualsiasi delle 4 stazioni
# (comprese la cassa), decisa ogni giorno dal responsabile (sez. 5.2).

CC = gcc
CFLAGS = -Wvla -Wextra -Werror -D_GNU_SOURCE -Iinclude -g

# Cartelle del progetto
SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj
BIN_DIR = bin

# Moduli comuni in src/common/
COMMON_SRCS = $(wildcard $(SRC_DIR)/common/*.c)
COMMON_OBJS = $(patsubst $(SRC_DIR)/common/%.c, $(OBJ_DIR)/%.o, $(COMMON_SRCS))

# Definizione dei singoli eseguibili basati sulle sottocartelle in src/
TARGETS = \
	$(BIN_DIR)/responsabile_mensa \
	$(BIN_DIR)/operatore \
	$(BIN_DIR)/utente \
	$(BIN_DIR)/add_users \
	$(BIN_DIR)/communication_disorder

# Target principale: compila tutto
all: dirs $(TARGETS)

# Creazione delle cartelle di output se non esistono
dirs:
	@mkdir -p $(OBJ_DIR) $(BIN_DIR) output logs

# Regola generica per compilare i file oggetto comuni da src/common/
$(OBJ_DIR)/%.o: $(SRC_DIR)/common/%.c $(INC_DIR)/*.h
	$(CC) $(CFLAGS) -c $< -o $@

# Regole di compilazione per ciascun eseguibile
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

# Regole specifiche per compilare i main nelle rispettive sottocartelle mappandoli in obj/
# (Evita conflitti di nomi per file 'main.c' identici in cartelle diverse)
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

# Target di pulizia: rimuove obj/ e bin/ (oltre alle cartelle di output/log)
clean:
	rm -rf $(OBJ_DIR)/* $(BIN_DIR)/* output/* logs/*

# Target per lanciare la simulazione tramite il responsabile
run: all
	./$(BIN_DIR)/responsabile_mensa config/config.conf

.PHONY: all dirs clean run