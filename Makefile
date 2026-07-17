# Makefile - compila tutti gli eseguibili del progetto
# Ogni processo (responsabile_mensa, operatore, cassiere, utente, ...)
# deve diventare un eseguibile separato in bin/, lanciato poi con execve().
#
# Target minimi da implementare:
#   all       -> compila tutto
#   clean     -> rimuove obj/ e bin/
#   run       -> lancia la simulazione (bin/responsabile_mensa)
#
# Flag obbligatori da richiesta di progetto:
CFLAGS = -Wvla -Wextra -Werror -D_GNU_SOURCE -Iinclude -g

# TODO: regole di compilazione per ogni eseguibile in src/*/main.c
# TODO: regola per compilare src/common/*.c come oggetti condivisi (obj/)
