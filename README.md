# Mensa Simulator

Simulazione a processi del Servizio Mensa "Oasi del Golfo" (versione completa).

## Struttura

Ogni processo del simulatore è un eseguibile a sé stante
(`src/<processo>/main.c`), lanciato tramite `fork()` + `execve()`:

| Eseguibile                     | Ruolo |
|--------------------------------|-------|
| `bin/responsabile_mensa`       | crea risorse IPC, processi, gestisce le giornate, stampa/esporta le statistiche |
| `bin/operatore`                | serve una qualsiasi delle 4 stazioni (primi, secondi-contorni, dolce-caffè, cassa), decisa ogni mattina dal responsabile |
| `bin/utente`                   | utente (con o senza ticket, singolo o membro di un gruppo) |
| `bin/add_users`                | aggiunge N_NEW_USERS utenti a simulazione avviata |
| `bin/communication_disorder`   | blocca i pagamenti automatici per STOP_DURATION |

Moduli comuni in `src/common/`: `config.c` (lettura configurazione),
`ipc_utils.c` (shared memory, semafori, code di messaggi), `stats.c`
(statistiche e export CSV), `simtime.c` (tempo simulato e casualità).

## Compilazione

    make

Flag usati: `gcc -Wvla -Wextra -Werror -D_GNU_SOURCE`.

## Esecuzione

    ./bin/responsabile_mensa config/config.conf

oppure, ripulendo prima eventuali IPC rimaste da esecuzioni precedenti:

    ./scripts/run_simulation.sh config/config.conf

Configurazioni che forzano le due cause di terminazione (sez. 5.6):

    ./bin/responsabile_mensa config/config_timeout.conf    # TIMEOUT
    ./bin/responsabile_mensa config/config_overload.conf   # OVERLOAD

### Eseguibili della versione completa

Da lanciare in un secondo terminale, mentre la simulazione è in corso
(funzionano correttamente anche se lanciati da un terminale diverso da
quello del responsabile: `add_users` comunica con esso solo tramite una
coda di messaggi, non forka direttamente i nuovi utenti):

    ./bin/add_users [n_utenti] [config_path]
    ./bin/communication_disorder [durata_secondi_simulati] [config_path]

Senza argomenti usano `N_NEW_USERS` / `STOP_DURATION` di
`config/config.conf`. I nuovi utenti richiesti da `add_users` iniziano a
partecipare dal giorno successivo a quello in cui vengono richiesti.

## Configurazione

Tutti i parametri sono letti a runtime da `config/config.conf`
(sintassi `CHIAVE=valore`, `#` per i commenti): cambiarli non richiede
una nuova compilazione. Il menu del giorno è in `config/menu.txt`
(sintassi `TIPO;Nome`, con `TIPO` fra `PRIMO`, `SECONDO`, `CONTORNO`,
`DOLCE`, `CAFFE`).

I tempi (`AVG_SRVC_*`, pause, consumo, refill, `STOP_DURATION`) sono
espressi in **secondi di tempo simulato**: un minuto simulato dura
`N_NANO_SECS` nanosecondi reali.

## Output

A fine giornata le statistiche vengono stampate a video e accodate a
`output/statistiche_simulazione.csv` (una riga per giornata).
