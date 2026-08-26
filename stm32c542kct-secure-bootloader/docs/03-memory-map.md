# Mappa di memoria

Target: **STM32C542KCT** — 256 KB di flash dual‑bank, 64 KB di SRAM, 4.5 KB di OTP.

> I valori del silicio vengono da [`04-silicon-facts.md`](04-silicon-facts.md) e
> sono ancora **da riconfermare sul PDF ufficiale** del datasheet.

---

## Ripartizione della flash

Ogni banco è 128 KB. Bootloader e slot A stanno entrambi nel banco 1, quindi la
loro somma non può superare i 128 KB.

```
                     ┌──────────────────────────────┐
BANCO 1  0x0800_0000 │ Bootloader            48 KB  │
                     │   .. 0x0800_BFFF             │
         0x0800_C000 ├──────────────────────────────┤
                     │ Slot A                80 KB  │
                     │   .. 0x0801_FFFF             │
                     ╞══════════════════════════════╡
BANCO 2  0x0802_0000 │ Slot B                80 KB  │
                     │   .. 0x0803_3FFF             │
         0x0803_4000 ├──────────────────────────────┤
                     │ Metadati e spare      48 KB  │
                     │   .. 0x0803_FFFF             │
                     └──────────────────────────────┘
```

| Regione | Base | Fine | Dimensione |
|---|---|---|---|
| Bootloader | `0x0800_0000` | `0x0800_BFFF` | 48 KB |
| Slot A | `0x0800_C000` | `0x0801_FFFF` | 80 KB |
| Slot B | `0x0802_0000` | `0x0803_3FFF` | 80 KB |
| Metadati e spare | `0x0803_4000` | `0x0803_FFFF` | 48 KB |

**Vincolo per l'applicazione: 80 KB.**

---

## Contenuto della regione bootloader

| Contenuto | Note |
|---|---|
| Vector table e codice | Punto di reset del dispositivo |
| Chiave pubblica di root | 64 byte, verificata contro l'hash in OTP a ogni boot |
| Crypto | X‑CUBE‑CRYPTOLIB, ECDSA P‑256 in software |
| Driver | Flash, FDCAN, ISO‑TP, sottoinsieme UDS |

Protetta in scrittura con **WRP** e, per la parte sensibile, con **HDP**.

---

## Contenuto della regione metadati

| Contenuto | Note |
|---|---|
| Descrittore A/B | Slot attivo e stato: `pending` / `confirmed` / `failed` |
| Contatore tentativi | Quante volte si è provato ad avviare lo slot `pending` |
| Spare | Margine per crescere: log di update, contatori diagnostici |

⚠️ Il descrittore va scritto in modo resistente a un power loss a metà
scrittura: due copie alternate con un contatore di sequenza e un CRC, in modo
che ne resti sempre una valida.

⚠️ **Dimensione della pagina di flash ancora ignota** — determina la
granularità di cancellazione, quindi quanto spazio serve realmente al
descrittore, e la granularità di WRP e HDP. Da leggere sul reference manual.

---

## OTP — 4.5 KB disponibili

| Contenuto | Dimensione | Note |
|---|---|---|
| Hash SHA‑256 della root key | 32 byte | Radice di fiducia, scritta in produzione |
| Contatore anti‑rollback | da definire | Codifica unaria: un bit per incremento |

Con 256 byte assegnati al contatore si ottengono 2048 incrementi, largamente
sufficienti. Resta da fissare il numero definitivo.

---

## ⚠️ Nodo aperto: da quale indirizzo gira l'applicazione?

Slot A e slot B stanno a indirizzi diversi (`0x0800_C000` e `0x0802_0000`). Un
firmware compilato per un indirizzo non gira all'altro senza accorgimenti.
Questo è **il punto di design più importante ancora da chiudere**, perché
condiziona il formato immagine, il tool di firma e la procedura di build.

Le tre strade possibili:

### 1. Due build distinte, una per slot
Il pacchetto di aggiornamento contiene l'immagine linkata per lo slot di
destinazione, e il bootloader la instrada di conseguenza.

*Pro:* nessuna copia di flash, swap istantaneo, bootloader semplice.
*Contro:* due artefatti da costruire, firmare e tracciare per ogni release;
il tool di update deve sapere in quale slot sta andando.

### 2. Swap fisico del contenuto (modello MCUboot)
L'applicazione è sempre linkata all'indirizzo dello slot A. Il bootloader copia
fisicamente le immagini per portare quella nuova nello slot A.

*Pro:* una sola build, un solo artefatto da firmare.
*Contro:* lo swap richiede tempo e cicli di scrittura flash, e va reso atomico
rispetto al power loss. Serve un'area di scratch.

### 3. Bank swap hardware
Usare il bit di option byte che rimappa i due banchi.

*Contro:* il bootloader risiede **dentro** il banco 1, quindi verrebbe
rimappato anche lui. Va verificato sul reference manual se il C5 offre un
meccanismo utilizzabile in questa configurazione. Probabilmente non praticabile
senza spostare il bootloader fuori dai banchi commutabili.

**Decisione rimandata**, ma va presa prima di scrivere il formato immagine.
