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

## Indirizzo di esecuzione dell'applicazione — risolto

Slot A e slot B stanno a indirizzi diversi (`0x0800_C000` e `0x0802_0000`), e un
firmware linkato per uno non gira all'altro.

**Decisione: due build distinte, una per slot. Nessuno swap.**

Scartate le due alternative:

- **Bank swap hardware** (`SWAP_BANK`) avrebbe dato una sola build a indirizzo
  fisso, duplicando il bootloader all'inizio dei due banchi. Scartata perché
  dipende da una funzione del silicio non confermata sul C5, e perché ogni
  aggiornamento costerebbe una scrittura di option byte.
- **Swap fisico del contenuto** alla MCUboot avrebbe copiato 80 KB a ogni
  aggiornamento: logorio della flash, tempi lunghi e una macchina a stati
  resistente al power loss che è la parte dove si annidano i bug.

### Come funziona

Due linker script e due artefatti per ogni release:

| Artefatto | Linkato a | Firmato |
|---|---|---|
| `app_slot_a.bin` | `0x0800_C000` | sì, separatamente |
| `app_slot_b.bin` | `0x0802_0000` | sì, separatamente |

**Flusso di aggiornamento:**

1. Il tool host chiede al dispositivo quale slot è inattivo — UDS `0x22`
   ReadDataByIdentifier, con un DID dedicato.
2. Manda l'immagine corrispondente a quello slot, non entrambe: sul CAN
   viaggiano 80 KB, non 160.
3. Trasferimento con `0x34` / `0x36` / `0x37`.
4. Il bootloader scrive nello slot inattivo, verifica firma e versione, e
   marca lo slot come `pending`.
5. Al reset tenta l'avvio dello slot `pending`; l'applicazione conferma il
   proprio avvio e lo stato passa a `confirmed`.
6. Se la conferma non arriva entro N tentativi, si torna allo slot precedente.

### Il rischio, e come si chiude

Il rischio dello schema a due build è mandare l'immagine sbagliata: un binario
linkato per lo slot A scritto nello slot B non parte, e il dispositivo resta
sul vecchio firmware o peggio.

**Mitigazione: l'indirizzo di destinazione va dentro l'header firmato.** Il
bootloader confronta il campo con lo slot in cui sta scrivendo e rifiuta
l'immagine se non corrispondono. Essendo dentro l'area coperta dalla firma, il
campo non è manipolabile.

### Conseguenze per il resto del progetto

- Il tool di firma in `tools/` produce e firma due artefatti per release, e la
  procedura deve garantire che le due immagini vengano dallo stesso sorgente.
- Il formato immagine (`02-image-format.md`) deve prevedere il campo con
  l'indirizzo o l'identificativo di slot di destinazione.
- Il bootloader imposta `VTOR` alla base della vector table dello slot scelto
  prima di saltarci.
- L'header precede la vector table dell'applicazione. La sua dimensione va
  scelta in modo da lasciare la tabella allineata come richiede `VTOR`:
  **512 byte** è una scelta pulita e con margine.
- Il descrittore A/B vive nella regione metadati del banco 2, che con questa
  scelta resta libera come previsto.
