# Relazione di progetto

Il progetto simula il funzionamento del servizio mensa ‘Oasi del Golfo’. Sono implementate sia la
versione minima sia la versione completa.

## Architettura

### Processi

- `responsabile_mensa`: crea le risorse IPC, legge il menu del giorno, crea gli
  operatori e gli utenti; scandisce le giornate, decide ogni mattina
  l'assegnazione operatore-stazione, rifornisce le stazioni, legge e
  stampa le statistiche, decide la terminazione e dealloca le risorse IPC;
- `operatore`: un unico eseguibile in grado di servire una qualsiasi
  delle 4 stazioni (primi, secondi-contorni, dolce-caffè, cassa): quale
  delle quattro viene deciso dal responsabile ogni
  mattina e comunicato via memoria condivisa;
- `utente`: un utente della mensa;
- `add_users`, `communication_disorder`: eseguibili della
  versione completa, invocabili da linea di comando a simulazione
  avviata.

### Risorse IPC

**Memoria condivisa**:

- `StatoStazioni`: menu del giorno e porzioni disponibili per ciascun
  piatto, postazioni totali/libere per stazione, operatori assegnati e
  in pausa, utenti in coda, numero di figli attesi alle barriere;
- `Statistiche`: tutti i contatori giornalieri e cumulativi.

**Semafori**:

- 5 mutex: statistiche e stato di ciascuna delle 4 stazioni;
- 4 semafori contatori per le postazioni delle stazioni;
- `SEM_POSTAZIONI_TICKET`: lettori di ticket;
- `SEM_TAVOLI`: posti a sedere;
- `SEM_GATE_START` / `SEM_BARRIERA_READY`: sincronizzazione di inizio e fine giornata fra responsabile e figli;
- `SEM_MUTEX_ATTESA`: mutex sui contatori degli utenti in coda;
- `SEM_CASSA_ATTIVA`: flag di funzionamento dei pagamenti automatici.

**Code di messaggi**: una per ciascuna delle 4 stazioni, una per la
comunicazione fra membri di un gruppo e capogruppo, e una per le
richieste di `./bin/add_users` al responsabile. Su ogni coda di stazione le richieste viaggiano con
`mtype = MSG_RICHIESTA` (letto dall'operatore assegnato quel giorno) e le
risposte con `mtype = pid` del destinatario: un solo oggetto IPC realizza
quindi sia la coda di attesa sia il canale di risposta punto-punto.

### Tempo simulato

Un minuto simulato corrisponde a `N_NANO_SECS` nanosecondi reali, quindi
un secondo simulato a `N_NANO_SECS/60`. Tutti i tempi di configurazione
sono in secondi simulati e vengono convertiti da `simtime.c`, così i
tempi di servizio, le pause, il consumo del pasto, il refill e la durata
del guasto restano confrontabili fra loro e con la durata della giornata.

## Scelte progettuali

### Assegnazione operatore-stazione

`Operatore` e `cassiere` sono un **unico eseguibile** in grado di servire una qualsiasi delle 4
stazioni: la stazione è  decisa dal responsabile ogni mattina e
pubblicata in memoria condivisa (`stato->stazione_operatore_oggi[id]`,
indicizzata per id dell'operatore).

Ad ogni giornata, prima di rilasciare il cancello (`SEM_GATE_START`), il
responsabile ricalcola l'assegnazione: ogni stazione riceve subito un
operatore; i restanti vengono assegnati privilegiando la stazione con
il tempo medio di servizio *per operatore* più alto, cioè massimizzando
ad ogni passo `AVG_SRVC / numero di addetti`. Il risultato è una
distribuzione proporzionale ai tempi medi di servizio, ricalcolata da
zero ogni mattina.

La scrittura di `stazione_operatore_oggi[]` avviene interamente prima
della `signal` su `SEM_GATE_START`: le operazioni sui semafori fanno da
barriera di memoria, quindi ogni operatore, appena si risveglia dal
cancello, vede l'assegnazione di quella giornata senza
bisogno di un mutex dedicato. L'operatore stesso non sa in anticipo quale
stazione servirà: apre tutte e 4 le code di messaggi all'avvio e sceglie
quale usare solo dopo aver letto la propria voce dell'array, ogni
mattina.

### Pause degli operatori

Ogni operatore, dopo aver **terminato di servire il cliente corrente**,
può decidere con probabilità `PAUSE_PROBABILITY` di mettersi in pausa,
al più `NOF_PAUSE` volte al giorno. La pausa è concessa solo se, sotto
mutex della stazione, risulta occupata più di una postazione: così la
stazione non resta mai sguarnita. L'operatore rilascia allora la
postazione, aggiorna le
statistiche, dorme e infine ricompete per una postazione.

Un operatore che non trova postazioni libere resta in attesa passiva sul
semaforo delle postazioni: si sveglierà quando un collega andrà in pausa
o quando la giornata finirà.

### Utenti con e senza ticket

Il tipo di utente è deciso una sola volta alla creazione del processo. La
**fila per la lettura del ticket** è modellata come una stazione di
servizio: l'utente con ticket vi si accoda in
attesa passiva prima di entrare nella mensa. 
Il tempo di attesa alla lettura del ticket e l'importo
complessivo degli sconti sono contabilizzati separatamente nelle
statistiche e nel CSV.

L'utente senza ticket salta questa fila: paga il prezzo pieno ma entra
subito in mensa.

### Gruppi che pranzano insieme 

Il responsabile estrae, per ogni gruppo, una dimensione casuale fra 1 e
`MAX_USERS_PER_GROUP` e crea prima il capogruppo, poi i membri
passando loro il pid del capogruppo: l'associazione utenti-gruppo è
quindi determinata alla creazione dei processi.

Ogni membro, terminato il giro delle stazioni, invia il proprio ordine
al capogruppo sulla coda dei gruppi e attende. 
Il capogruppo compone un'unica `MsgRichiestaCassa` e la invia alla cassa; il cassiere
calcola l'importo di ciascun membro e risponde singolarmente a ognuno.
Solo allora i membri si siedono a mangiare: il pagamento avviene quindi
dopo che tutti si sono riforniti.

Per garantire che la raccolta termini sempre, ogni membro invia il
proprio messaggio anche quando non pranza (campo `partecipa = 0`):
il capogruppo sa così sempre quanti messaggi attendere e non può
restare bloccato indefinitamente.

### Scelta della coda e piatti esauriti 

L'utente compone il menu secondo le probabilità di configurazione
(garantendo almeno un piatto principale), poi, fra le stazioni che deve
ancora visitare, sceglie ogni volta quella con la coda più corta. 
La valutazione è ripetuta dopo ogni piatto ottenuto.

Se al momento del servizio il piatto scelto è esaurito, l'operatore ne
sostituisce automaticamente un altro dello stesso tipo; 
se sono finiti tutti i piatti di quel tipo l'utente ripiega sull'altro tipo di piatto
principale, e se anche quello è esaurito desiste ed esce senza passare
in cassa. Dolce e caffè si prendono per ultimi e in un'unica visita alla
stazione coffee, che ha scorte illimitate.

### Refill delle porzioni 

Le stazioni primi e secondi sono inizializzate ogni mattina. 
Il responsabile fa avanzare la giornata a blocchi di 10 minuti
simulati e ad ogni blocco valuta il rifornimento. 
Le porzioni rimaste a fine giornata sono conteggiate
come "piatti avanzati".

### Communication disorder

`SEM_CASSA_ATTIVA` vale 1 quando i pagamenti automatici funzionano. Il
processo `communication_disorder` esegue una `wait` che lo porta a 0,
dorme per `STOP_DURATION` secondi simulati e infine lo riporta a 1. I
cassieri eseguono una coppia `wait`/`signal` su questo semaforo prima di
ogni incasso: se il semaforo è a 0 restano sospesi in attesa passiva e
riprendono automaticamente al ripristino, mentre gli utenti continuano
ad accodarsi alla cassa.

### Aggiunta dinamica di utenti

`add_users` non crea processi direttamente, invia una richiesta su una coda di messaggi
dedicata ed esce. È il responsabile, che la controlla in modo non
bloccante ad ogni blocco di 10 minuti simulati, a fare `fork()`+`execve()`
dei nuovi utenti e a incrementare sotto mutex il contatore dei figli
attesi in memoria condivisa.

Il segnale che il nuovo utente emette su `SEM_BARRIERA_READY` al termine
della propria inizializzazione vale come presenza alla barriera della
giornata in corso; dal giorno successivo si allinea al normale ciclo di
`SEM_GATE_START`.

## Sincronizzazione delle giornate e terminazione

All'avvio il responsabile attende su `SEM_BARRIERA_READY` un segnale da
ciascuno dei figli: la simulazione comincia solo quando tutti hanno
completato l'inizializzazione. Ogni mattina il responsabile esegue
`rilascia_gate()`, cioè altrettante `signal` su `SEM_GATE_START`; ogni
figlio ne consuma una e lavora fino a fine giornata.

Alla chiusura il responsabile invia `SIGUSR1` a tutto il process group e
attende di nuovo una `signal` per figlio.

Poiché a barriera raggiunta nessun figlio sta lavorando, il responsabile
può leggere lo stato delle stazioni per contare i piatti avanzati e
svuotare le code di messaggi, così che ogni giornata inizi pulita.

Un operatore (o il cassiere) smette di
leggere dalla propria coda non appena nota il flag di fine giornata,
all'inizio dell'iterazione successiva del proprio ciclo di servizio. Se
ci sono ancora richieste accodate in quel momento, gli utenti che le hanno inviate restano
bloccati in un `msgrcv()` che attende proprio quella risposta: nessun altro processo può mai
rispondergli, quindi resterebbero in attesa per sempre e il responsabile
non raggiungerebbe mai la barriera. Per questo, prima di rilasciare la
postazione, operatore e cassiere svuotano la propria coda in modo non
bloccante rispondendo con un esito di "non riuscito"
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
