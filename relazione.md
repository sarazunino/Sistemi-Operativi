# Relazione di progetto — Mensa Simulator (versione completa)

## 1. Introduzione e obiettivi

Il progetto simula il funzionamento di una mensa con code alle stazioni
di distribuzione, cassa, tavoli, operatori con turni e pause, utenti con
e senza ticket e gruppi che pranzano insieme. Sono implementate sia la
versione minima sia le estensioni della versione completa
(communication disorder, aggiunta dinamica di utenti, export CSV,
utenti con/senza ticket, gruppi, refill a tempo variabile).

## 2. Architettura

### Processi

Ogni processo è un eseguibile distinto, lanciato con `fork()` +
`execve()` dal responsabile mensa (o da `add_users`):

- `responsabile_mensa` — crea le risorse IPC, il menu del giorno, gli
  operatori, i cassieri e gli utenti; scandisce le giornate, rifornisce
  le stazioni, legge e stampa le statistiche, decide la terminazione e
  dealloca le risorse IPC;
- `operatore` — serve una fra le stazioni primi, secondi-contorni e
  dolce-caffè (il tipo è passato come argomento);
- `cassiere` — serve la stazione cassa;
- `utente` — un utente della mensa;
- `add_users`, `communication_disorder` — eseguibili accessori della
  versione completa, invocabili da linea di comando a simulazione
  avviata.

I moduli comuni (`src/common/`) sono compilati una sola volta e linkati
in tutti gli eseguibili: `config` (parsing della configurazione),
`ipc_utils` (creazione/accesso/deallocazione delle IPC), `stats`
(aggiornamento, stampa ed export delle statistiche), `simtime`
(conversione del tempo simulato e generazione dei valori casuali).

### Risorse IPC

**Memoria condivisa** (2 segmenti):

- `StatoStazioni` — menu del giorno e porzioni disponibili per ciascun
  piatto, postazioni totali/libere per stazione, operatori assegnati e
  in pausa, utenti in coda, numero di figli attesi alle barriere;
- `Statistiche` — tutti i contatori giornalieri e cumulativi.

**Semafori** (un unico set, `NUM_SEMAFORI` elementi):

- 5 mutex: statistiche e stato di ciascuna delle 4 stazioni;
- 4 semafori contatori per le postazioni delle stazioni, inizializzati a
  `NOF_WK_SEATS_*`;
- `SEM_POSTAZIONI_TICKET` — lettori di ticket (`NOF_TICKET_READERS`);
- `SEM_TAVOLI` — posti a sedere (`NOF_TABLE_SEATS`);
- `SEM_GATE_START` / `SEM_BARRIERA_READY` — sincronizzazione di inizio e
  fine giornata fra responsabile e figli;
- `SEM_MUTEX_ATTESA` — mutex sui contatori degli utenti in coda;
- `SEM_CASSA_ATTIVA` — flag di funzionamento dei pagamenti automatici.

**Code di messaggi** (5): una per ciascuna delle 4 stazioni, più una per
la comunicazione fra membri di un gruppo e capogruppo. Su ogni coda di
stazione le richieste viaggiano con `mtype = MSG_RICHIESTA` (letto dagli
operatori) e le risposte con `mtype = pid` del destinatario: un solo
oggetto IPC realizza quindi sia la coda di attesa sia il canale di
risposta punto-punto.

Non c'è alcuna attesa attiva: ogni attesa è una `semop()` o una
`msgrcv()` bloccante, oppure una `sigsuspend()`.

### Tempo simulato

Un minuto simulato corrisponde a `N_NANO_SECS` nanosecondi reali, quindi
un secondo simulato a `N_NANO_SECS/60`. Tutti i tempi di configurazione
sono in secondi simulati e vengono convertiti da `simtime.c`, così i
tempi di servizio, le pause, il consumo del pasto, il refill e la durata
del guasto restano confrontabili fra loro e con la durata della giornata.

## 3. Scelte progettuali

### Assegnazione operatore-stazione (sez. 5.2)

All'inizio della simulazione ogni stazione riceve un operatore (per
questo `NOF_WORKERS >= 4` è un vincolo verificato all'avvio); i restanti
vengono assegnati privilegiando la stazione con il tempo medio di
servizio *per operatore* più alto, cioè massimizzando ad ogni passo
`AVG_SRVC / numero di addetti`. Il risultato è una distribuzione
proporzionale ai tempi medi di servizio.

### Pause degli operatori (sez. 5.3)

Ogni operatore, dopo aver **terminato di servire il cliente corrente**,
può decidere con probabilità `PAUSE_PROBABILITY` di mettersi in pausa,
al più `NOF_PAUSE` volte al giorno. La pausa è concessa solo se, sotto
mutex della stazione, risulta occupata più di una postazione: così la
stazione non resta mai sguarnita. L'operatore rilascia allora la
postazione (semaforo + contatore in memoria condivisa), aggiorna le
statistiche, dorme e infine ricompete per una postazione.

Un operatore che non trova postazioni libere resta in attesa passiva sul
semaforo delle postazioni: si sveglierà quando un collega andrà in pausa
o quando la giornata finirà (l'attesa è interrompibile dal segnale, vedi
`sem_wait_idx_intr()`, altrimenti non potrebbe raggiungere la barriera di
fine giornata).

### Utenti con e senza ticket (sez. 6)

Il tipo di utente è deciso una sola volta alla creazione del processo:
con probabilità `TICKET_PERCENTAGE` (80%) l'utente è "w ticket". La
**fila per la lettura del ticket** è modellata come una stazione di
servizio con `NOF_TICKET_READERS` postazioni, rappresentate dal semaforo
contatore `SEM_POSTAZIONI_TICKET`: l'utente con ticket vi si accoda in
attesa passiva prima di entrare nella mensa (come in Figura 1), impiega
`AVG_SRVC_TICKET ± 20%` e solo dopo si dirige alle stazioni. Lo sconto
`TICKET_DISCOUNT_PCT` viene applicato dal cassiere sull'importo del
singolo utente; il tempo di attesa alla lettura del ticket e l'importo
complessivo degli sconti sono contabilizzati separatamente nelle
statistiche e nel CSV.

L'utente senza ticket salta questa fila: paga il prezzo pieno ma entra
subito in mensa. È così visibile il compromesso fra i due tipi di utente.

### Gruppi che pranzano insieme (sez. 6)

Il responsabile estrae, per ogni gruppo, una dimensione casuale fra 1 e
`MAX_USERS_PER_GROUP` e crea prima il capogruppo, poi i membri
passando loro il pid del capogruppo: l'associazione utenti-gruppo è
quindi determinata alla creazione dei processi.

Ogni membro, terminato il giro delle stazioni, invia il proprio ordine
al capogruppo sulla coda dei gruppi (`mtype` = pid del capogruppo) e
attende. Il capogruppo raccoglie esattamente `num_membri - 1` ordini,
compone un'unica `MsgRichiestaCassa` e la invia alla cassa; il cassiere
calcola l'importo di ciascun membro e risponde singolarmente a ognuno.
Solo allora i membri si siedono a mangiare: il pagamento avviene quindi
dopo che *tutti* si sono riforniti.

Per garantire che la raccolta termini sempre, **ogni membro invia il
proprio messaggio anche quando non pranza** (campo `partecipa = 0`):
il capogruppo sa così sempre quanti messaggi attendere e non può
restare bloccato indefinitamente.

### Scelta della coda e piatti esauriti (sez. 5.5)

L'utente compone il menu secondo le probabilità di configurazione
(garantendo almeno un piatto principale), poi, fra le stazioni che deve
ancora visitare, sceglie ogni volta quella con la coda più corta,
misurata leggendo `msg_qnum` con `msgctl(IPC_STAT)`. La valutazione è
ripetuta dopo ogni piatto ottenuto.

Se al momento del servizio il piatto scelto è esaurito, l'operatore ne
sostituisce automaticamente un altro dello stesso tipo
(`ESITO_SOSTITUITO`); se sono finiti tutti i piatti di quel tipo
(`ESITO_TIPO_ESAURITO`) l'utente ripiega sull'altro tipo di piatto
principale, e se anche quello è esaurito desiste ed esce senza passare
in cassa. Dolce e caffè si prendono per ultimi e in un'unica visita alla
stazione coffee, che ha scorte illimitate.

### Refill delle porzioni (sez. 5.2 / sez. 6)

Le stazioni primi e secondi sono inizializzate ogni mattina con
`AVG_REFILL_PRIMI` / `AVG_REFILL_SECONDI` porzioni per ciascun tipo di
piatto. Il responsabile fa avanzare la giornata a blocchi di 10 minuti
simulati e ad ogni blocco valuta il rifornimento, che avviene ogni
`AVG_REFILL_TIME ± REFILL_VARIABILITY_PERCENT%` fino al massimo di
`MAX_PORZIONI_*`. Le porzioni rimaste a fine giornata sono conteggiate
come "piatti avanzati".

### Communication disorder (sez. 6)

`SEM_CASSA_ATTIVA` vale 1 quando i pagamenti automatici funzionano. Il
processo `communication_disorder` esegue una `wait` che lo porta a 0,
dorme per `STOP_DURATION` secondi simulati e infine lo riporta a 1. I
cassieri eseguono una coppia `wait`/`signal` su questo semaforo prima di
ogni incasso: se il semaforo è a 0 restano sospesi in attesa passiva e
riprendono automaticamente al ripristino, mentre gli utenti continuano
ad accodarsi alla cassa.

### Aggiunta dinamica di utenti (sez. 6)

`add_users` **non crea processi direttamente**: invia una richiesta
(`MsgRichiestaAddUsers`, numero di utenti) su una coda di messaggi
dedicata ed esce. È il responsabile, che la controlla in modo non
bloccante ad ogni blocco di 10 minuti simulati, a fare `fork()`+`execve()`
dei nuovi utenti e a incrementare sotto mutex il contatore dei figli
attesi in memoria condivisa.

Questa indirezione non è incidentale: se fosse `add_users` a creare i
processi (come in una prima versione), il figlio dovrebbe unirsi al
process group del responsabile con `setpgid()` per poter ricevere
`SIGUSR1`/`SIGTERM` via `killpg()`. Ma `setpgid()` può unire un processo
solo a un process group della **propria stessa sessione**: lanciando
`add_users` da un terminale diverso da quello del responsabile (l'uso
normale previsto dal testo) la chiamata fallisce con `EPERM`, i nuovi
utenti restano nel process group della shell che li ha creati, non
ricevono mai i segnali di fine giornata e il responsabile resta bloccato
per sempre alla barriera successiva. Facendo forkare i nuovi utenti
al responsabile stesso, questi ereditano automaticamente il suo process
group (comportamento normale di `fork()`) e vengono anche
correttamente attesi da `waitpid()` a fine simulazione, senza alcuna
dipendenza dalla sessione del terminale da cui `add_users` viene invocato.

Il segnale che il nuovo utente emette su `SEM_BARRIERA_READY` al termine
della propria inizializzazione vale come presenza alla barriera della
giornata in corso; dal giorno successivo si allinea al normale ciclo di
`SEM_GATE_START`.

## 4. Sintassi del file `menu.txt`

Una riga per prodotto, nel formato `TIPO;Nome`, con `TIPO` fra `PRIMO`,
`SECONDO`, `CONTORNO`, `DOLCE`, `CAFFE`. Le righe vuote e quelle che
iniziano con `#` sono ignorate. All'avvio il responsabile verifica che
siano presenti almeno 2 primi, 2 secondi, 1 contorno, 1 dolce e i 4 tipi
di caffè, altrimenti la simulazione non parte. I prezzi non compaiono
nel menu: sono fissati nella configurazione e uguali per tutti i piatti
dello stesso tipo (il contorno è compreso nel prezzo del secondo).

## 5. Sincronizzazione delle giornate e terminazione

All'avvio il responsabile attende su `SEM_BARRIERA_READY` un segnale da
ciascuno dei figli: la simulazione comincia solo quando tutti hanno
completato l'inizializzazione. Ogni mattina il responsabile esegue
`rilascia_gate()`, cioè altrettante `signal` su `SEM_GATE_START`; ogni
figlio ne consuma una e lavora fino a fine giornata.

Alla chiusura il responsabile invia `SIGUSR1` a tutto il process group e
attende di nuovo una `signal` per figlio. I segnali sono installati
**senza** `SA_RESTART`, così ogni `msgrcv()`/`semop()` bloccante ritorna
con `EINTR` e il processo può abbandonare la coda: gli utenti ancora in
fila rinunciano al pasto e vengono contati fra i non serviti. Gli utenti
che hanno finito prima attendono la chiusura con `sigsuspend()` sotto
maschera, evitando la corsa fra il test del flag e la sospensione.

Poiché a barriera raggiunta nessun figlio sta lavorando, il responsabile
può leggere lo stato delle stazioni per contare i piatti avanzati e
svuotare le code di messaggi, così che ogni giornata inizi pulita.

**Drain di fine giornata.** Un operatore (o il cassiere) smette di
leggere dalla propria coda non appena nota il flag di fine giornata,
all'inizio dell'iterazione successiva del proprio ciclo di servizio. Se
ci sono ancora richieste accodate in quel momento — condizione tutt'altro
che rara quando la domanda eccede la capacità di servizio, come nella
configurazione di overload — gli utenti che le hanno inviate restano
bloccati in un `msgrcv()` che attende proprio quella risposta
(instradata per `mtype` sul loro pid): nessun altro processo può mai
rispondergli, quindi resterebbero in attesa per sempre e il responsabile
non raggiungerebbe mai la barriera. Per questo, prima di rilasciare la
postazione, operatore e cassiere svuotano la propria coda in modo non
bloccante (`IPC_NOWAIT`) rispondendo con un esito di "non riuscito"
(`ESITO_TIPO_ESAURITO` per i piatti, `totale_pagato = -1` per la cassa)
a chiunque sia rimasto in sospeso: quegli utenti rinunciano al piatto o
al pasto esattamente come chi trova la coda già chiusa, e vengono
conteggiati fra i non serviti.

La simulazione termina:

- per **timeout**, al raggiungimento di `SIM_DURATION` giorni;
- per **overload**, quando gli utenti ancora in coda alla chiusura
  superano `OVERLOAD_THRESHOLD`.

In entrambi i casi il responsabile invia `SIGTERM` al process group,
sblocca i figli eventualmente fermi al cancello, li attende con
`waitpid()`, stampa la causa di terminazione e le statistiche finali e
infine rimuove tutti i segmenti, i semafori e le code create.

## 6. Testing e configurazioni di prova

- `config/config_timeout.conf` — mensa sovradimensionata rispetto agli
  utenti (12 operatori, molte postazioni, servizio rapido, giornata
  lunga, soglia di overload alta): le code si svuotano sempre entro la
  chiusura e la simulazione arriva al quinto giorno → **TIMEOUT**.
- `config/config_overload.conf` — 200 utenti, il minimo di operatori
  (uno per stazione), una sola postazione per stazione, un solo lettore
  di ticket, servizio lento e giornata di 60 minuti simulati: alla
  chiusura restano in coda molti più di `OVERLOAD_THRESHOLD = 20`
  utenti → **OVERLOAD** già al primo giorno.

## 7. Compilazione ed esecuzione

    make
    ./bin/responsabile_mensa config/config.conf

e, a simulazione avviata, da un altro terminale:

    ./bin/add_users 20
    ./bin/communication_disorder 900

Il progetto è compilato con `gcc -Wvla -Wextra -Werror -D_GNU_SOURCE`.
Le statistiche giornaliere sono stampate a video e accodate a
`output/statistiche_simulazione.csv`.
