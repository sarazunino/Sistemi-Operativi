# Mensa Simulator

Simulazione a processi (Linux, IPC System V/POSIX) di una mensa
studentesca/aziendale. Vedi docs/relazione.md per la relazione finale.

## Struttura
Vedi albero cartelle nel repo. Ogni processo del simulatore è un
eseguibile a sé stante (src/<processo>/main.c), lanciato dal
responsabile_mensa tramite fork+execve.

## Come si esegue (da implementare)
    make
    ./bin/responsabile_mensa config/config.conf
