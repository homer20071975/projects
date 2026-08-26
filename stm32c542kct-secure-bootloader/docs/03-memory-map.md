# Mappa di memoria

Target: **STM32C542KCT** — 256 KB di flash dual‑bank, 64 KB di SRAM, 4.5 KB di OTP.

> I valori del silicio vengono da [`04-silicon-facts.md`](04-silicon-facts.md) e
> sono ancora **da riconfermare sul PDF ufficiale**.

Layout: **bootloader + area di esecuzione + area di staging** (§13 di
[`00-decisions.md`](00-decisions.md)).

---

## Ripartizione della flash

```
0x0800_0000  ┌────────────────────────────────┐
             │ Bootloader              48 KB  │
0x0800_BFFF  └────────────────────────────────┘
0x0800_C000  ┌────────────────────────────────┐
             │ Area di esecuzione     100 KB  │
             │   header 512 B + applicazione  │
0x0802_4FFF  └────────────────────────────────┘
0x0802_5000  ┌────────────────────────────────┐
             │ Area di staging        100 KB  │
0x0803_DFFF  └────────────────────────────────┘
0x0803_E000  ┌────────────────────────────────┐
             │ Metadati                 8 KB  │
0x0803_FFFF  └────────────────────────────────┘
```

| Regione | Base | Fine | Dimensione |
|---|---|---|---|
| Bootloader | `0x0800_0000` | `0x0800_BFFF` | 48 KB |
| Esecuzione | `0x0800_C000` | `0x0802_4FFF` | 100 KB |
| Staging | `0x0802_5000` | `0x0803_DFFF` | 100 KB |
| Metadati | `0x0803_E000` | `0x0803_FFFF` | 8 KB |

**Vincolo per l'applicazione: 100 KB meno i 512 byte di header, quindi 102 400
byte scarsi di codice e dati.**

L'applicazione è **una sola build a indirizzo fisso**: vector table a
`0x0800_C200`, subito dopo l'header. L'indirizzo è allineato a 512 byte, come
richiede `VTOR`.

---

## Il confine di banco cade dentro l'area di esecuzione

Il banco 2 inizia a `0x0802_0000`, che sta dentro l'area di esecuzione. Non è
un problema, ma vincola **come** si fa la copia.

Sugli STM32 dual‑bank non si può prelevare codice da un banco mentre lo si
cancella o programma. E anche i **dati** non si possono leggere da un banco in
corso di programmazione: copiare direttamente staging → esecuzione romperebbe
proprio dove le due aree condividono il banco 2.

**Soluzione: copia bufferizzata dalla SRAM.**

1. La routine di copia viene ricollocata in SRAM ed eseguita da lì.
2. Legge un blocco dallo staging in un buffer in RAM. In questo momento non
   c'è nessuna operazione di scrittura in corso, quindi la lettura è valida.
3. Cancella e programma la pagina di destinazione attingendo al buffer. Non
   serve leggere flash: né il codice (è in RAM) né i dati (sono nel buffer).

Serializzando lettura e scrittura il problema del read‑while‑write sparisce, e
il layout diventa libero da vincoli di banco. Costo: qualche centinaio di byte
di codice in SRAM più il buffer, trascurabili sui 64 KB disponibili.

⚠️ Da confermare sul reference manual del C5, ma è la regola generale su STM32.

---

## Contenuto della regione bootloader

| Contenuto | Note |
|---|---|
| Vector table e codice | Punto di reset del dispositivo |
| Chiave pubblica di root | 64 byte, verificata contro l'hash in OTP a ogni boot |
| Crypto | X‑CUBE‑CRYPTOLIB, ECDSA P‑256 in software |
| Driver | Flash, FDCAN, ISO‑TP, sottoinsieme UDS |
| Routine di copia | Ricollocata in SRAM all'occorrenza |

Protetta in scrittura con **WRP** e, per la parte sensibile, con **HDP**.

Il bootloader **non si autoaggiorna**: resta sempre integro, ed è questo che
rende il dispositivo irrecuperabile solo in caso di guasto hardware.

---

## Contenuto della regione metadati

| Contenuto | Note |
|---|---|
| Stato dell'immagine | `valid` / `pending` / `failed` |
| Contatore tentativi | Quante volte si è provato ad avviare senza conferma |
| Stato della copia | Per riprendere una copia interrotta dal power loss |
| Spare | Log di update, contatori diagnostici |

Scritto in **doppia copia alternata**, ciascuna con contatore di sequenza e
CRC, così che un'interruzione a metà scrittura ne lasci sempre una valida.

⚠️ **Gli 8 KB assegnati presuppongono una pagina di flash da 2 KB o meno**,
per averne almeno due a disposizione. Se la pagina fosse da 8 KB servirebbero
16 KB e i confini andrebbero spostati. **Da leggere sul reference manual**: la
dimensione della pagina determina anche la granularità di WRP e HDP.

---

## OTP — 4.5 KB disponibili

| Contenuto | Dimensione | Note |
|---|---|---|
| Hash SHA‑256 della root key | 32 byte | Radice di fiducia, scritta in produzione |
| Contatore anti‑rollback | da definire | Codifica unaria: un bit per incremento |

Con 256 byte assegnati al contatore si ottengono 2048 incrementi.

---

## Ciclo di aggiornamento

```
  download          verifica          copia            conferma
staging ←── CAN    firma + versione   staging→exec    l'app conferma
                   sullo staging      via SRAM        entro N boot
```

1. Il tool host apre la sessione UDS e trasferisce l'immagine nello **staging**
   con `0x34` / `0x36` / `0x37`.
2. Il bootloader verifica **sullo staging**: firma ECDSA, `security_version`
   contro il contatore OTP, coerenza di `product_id` e dimensioni.
3. Solo se la verifica passa, marca lo stato `pending` e copia staging →
   esecuzione, bufferizzando dalla SRAM.
4. Al reset verifica di nuovo l'area di esecuzione e salta.
5. L'applicazione conferma il proprio avvio; lo stato passa a `valid` e il
   contatore anti‑rollback in OTP viene incrementato.

### Cosa succede se qualcosa va storto

| Guasto | Comportamento |
|---|---|
| Firma non valida sullo staging | L'immagine viene rifiutata, l'area di esecuzione non viene toccata |
| Power loss durante la copia | Lo staging è ancora integro: al boot successivo la copia riprende |
| L'applicazione non conferma | Dopo N tentativi il bootloader **resta in modalità update** in attesa sul CAN |

L'ultimo caso è la rete di sicurezza che sostituisce il rollback: non essendoci
una versione precedente a cui tornare, il bootloader smette di provare e si
mette in ascolto, perché è stato confermato che **in campo resta raggiungibile
via CAN**. Se quel presupposto dovesse cambiare, va rivista la §13.
