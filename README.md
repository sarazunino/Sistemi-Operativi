# Mensa Simulator

Simulazione a processi del Servizio Mensa ‘Oasi del Golfo’

## Struttura
Vedi albero cartelle nel repo. Ogni processo del simulatore è un
eseguibile a sé stante (src/<processo>/main.c), lanciato dal
responsabile_mensa tramite fork+execve.

## Come si esegue (da implementare)
    make ./bin/responsabile_mensa config/config.conf
