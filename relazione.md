# Relazione Sistemi Operativi 2025-2026

Il progetto simula il funzionamento del servizio mensa ‘Oasi del Golfo’. 

## Composizione

### Processi

- `responsabile_mensa`: crea le risorse IPC, legge il menu del giorno, crea gli
  operatori e gli utenti; scandisce le giornate, decide ogni mattina
  l'assegnazione operatore-stazione, rifornisce le stazioni, legge e
  stampa le statistiche, decide la terminazione e dealloca le risorse IPC.
- `operatore`: un unico eseguibile in grado di servire una qualsiasi
  delle 4 stazioni (primi, secondi-contorni, dolce-caffè, cassa): quale
  delle quattro viene deciso dal responsabile ogni
  mattina e comunicato via memoria condivisa.
- `utente`: un utente della mensa.
- `add_users`, `communication_disorder`: eseguibili della
  versione completa, invocabili da linea di comando a simulazione
  avviata.

### Risorse IPC

**Memoria condivisa**:

- `StatoStazioni`: menu del giorno e porzioni disponibili per ciascun
  piatto, postazioni totali/libere per stazione, operatori assegnati e
  in pausa, utenti in coda, numero di figli attesi alle barriere.
- `Statistiche`: tutti i contatori giornalieri e cumulativi.

**Semafori**:

- 5 mutex: statistiche e stato di ciascuna delle 4 stazioni.
- 4 semafori contatori per le postazioni delle stazioni.
- `SEM_POSTAZIONI_TICKET`: lettori di ticket.
- `SEM_TAVOLI`: posti a sedere.
- `SEM_GATE_START` / `SEM_BARRIERA_READY`: sincronizzazione di inizio e fine giornata.
- `SEM_MUTEX_ATTESA`: mutex sui contatori degli utenti in coda.
- `SEM_CASSA_ATTIVA`: flag di funzionamento dei pagamenti.

**Code di messaggi**: una per ciascuna delle 4 stazioni, una per la
comunicazione fra membri di un gruppo e capogruppo, e una per le
richieste di `./bin/add_users` al responsabile.

### Tempo simulato

Tutti i tempi di configurazione vengono convertiti da `simtime.c`, così i tempi di servizio, le pause, il consumo del pasto, il refill e la durata
del guasto restano confrontabili fra loro e con la durata della giornata.

## Scelte progettuali

### Assegnazione operatore-stazione

Operatore e cassiere sono un unico eseguibile in grado di servire una qualsiasi delle 4
stazioni: la stazione è  decisa dal responsabile ogni mattina e
pubblicata in memoria condivisa.
Ad ogni giornata, prima di rilasciare il cancello (`SEM_GATE_START`), il
responsabile ricalcola l'assegnazione, ogni stazione riceve subito un
operatore. I restanti vengono assegnati privilegiando la stazione con
il tempo medio di servizio per operatore più alto. 

### Pause degli operatori

Ogni operatore, dopo aver terminato di servire il cliente corrente,
può decidere di mettersi in pausa. La pausa è concessa solo se risulta occupata più di una postazione: così la stazione non resta mai vuota. L'operatore rilascia allora la postazione, aggiorna le statistiche, dorme e infine ricompete per una postazione. 
Un operatore che non trova postazioni libere resta in attesa passiva sul
semaforo delle postazioni: si sveglierà quando un collega andrà in pausa
o quando la giornata finirà.

### Utenti con e senza ticket

Il tipo di utente è deciso una sola volta alla creazione del processo. La
fila per la lettura del ticket è modellata come una stazione di
servizio: l'utente con ticket vi si accoda in
attesa passiva prima di entrare nella mensa. 
L'utente senza ticket salta questa fila: paga il prezzo pieno ma entra
subito in mensa.

### Gruppi che pranzano insieme 

Il responsabile estrae, per ogni gruppo, una dimensione casuale (limitata) e crea prima il capogruppo, poi i membri
passando loro il pid del capogruppo: l'associazione utenti-gruppo è
quindi determinata alla creazione dei processi.
Ogni membro, terminato il giro delle stazioni, invia il proprio ordine
al capogruppo sulla coda dei gruppi e attende. 
Il capogruppo compone un'unica richiesta e la invia alla cassa; il cassiere
calcola l'importo di ciascun membro e risponde singolarmente a ognuno.
Solo allora i membri si siedono a mangiare: il pagamento avviene quindi
dopo che tutti si sono riforniti.

### Scelta della coda e piatti esauriti 

L'utente compone il menu secondo le probabilità di configurazione, poi fra le stazioni che deve
ancora visitare, sceglie ogni volta quella con la coda più corta. 
La valutazione è ripetuta dopo ogni piatto ottenuto.
Se al momento del servizio il piatto scelto è esaurito, l'operatore ne
sostituisce automaticamente un altro dello stesso tipo; 
se sono finiti tutti i piatti di quel tipo l'utente ripiega sull'altro tipo di piatto
principale, e se anche quello è esaurito desiste ed esce senza passare
in cassa. Dolce e caffè si prendono per ultimi e in un'unica visita alla
stazione coffee.

### Refill delle porzioni 

Le stazioni primi e secondi sono inizializzate ogni mattina. 
Il responsabile fa avanzare la giornata a blocchi di 10 minuti
simulati e ad ogni blocco valuta il rifornimento. 
Le porzioni rimaste a fine giornata sono conteggiate
come "piatti avanzati".

### Communication disorder

`SEM_CASSA_ATTIVA` vale 1 quando i pagamenti automatici funzionano. Il
processo `communication_disorder` esegue una `wait` che lo porta a 0,
dorme e infine lo riporta a 1. I
cassieri eseguono una coppia `wait`/`signal` su questo semaforo prima di
ogni incasso: se il semaforo è a 0 restano sospesi in attesa passiva e
riprendono automaticamente al ripristino, mentre gli utenti continuano
ad accodarsi alla cassa.

### Aggiunta dinamica di utenti

`add_users` non crea processi direttamente, invia una richiesta su una coda di messaggi
dedicata ed esce. È il responsabile, che la controlla ad ogni blocco di 10 minuti simulati, a fare `fork()`+`execve()` dei nuovi utenti.

### Sincronizzazione delle giornate e terminazione

All'avvio il responsabile attende su `SEM_BARRIERA_READY` un segnale da
ciascuno dei figli: la simulazione comincia solo quando tutti hanno
completato l'inizializzazione. Ogni mattina il responsabile esegue
`rilascia_gate()`, cioè altrettante `signal` su `SEM_GATE_START`.

Alla chiusura il responsabile invia `SIGUSR1` a tutto il process group e
attende di nuovo una `signal` per figlio.

Poiché a barriera raggiunta nessun figlio sta lavorando, il responsabile può a questo punto
svuotare le code di messaggi, così che ogni giornata inizi pulita.

Un operatore o il cassiere smette di
leggere dalla propria coda non appena nota il flag di fine giornata,
all'inizio dell'iterazione successiva del proprio ciclo di servizio. 
Prima di rilasciare la
postazione, operatore e cassiere svuotano la propria coda rispondendo con un esito di "non riuscito"
a chiunque sia rimasto in sospeso: quegli utenti rinunciano al piatto o
al pasto esattamente come chi trova la coda già chiusa, e vengono
conteggiati fra i non serviti.

La simulazione termina:

- per **timeout**, al raggiungimento di `SIM_DURATION` giorni.
- per **overload**, quando gli utenti ancora in coda alla chiusura
  superano `OVERLOAD_THRESHOLD`.

In entrambi i casi il responsabile invia `SIGTERM` al process group,
sblocca i figli eventualmente fermi al cancello, li attende con
`waitpid()`, stampa la causa di terminazione e le statistiche finali e
infine rimuove tutti i segmenti, i semafori e le code create.
