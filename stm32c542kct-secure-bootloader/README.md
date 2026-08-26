# STM32C542KCT — Secure Bootloader

Bootloader sicuro per microcontrollore **STM32C542KCT** (serie STM32C5, core Arm Cortex‑M33 con TrustZone).

> ⚠️ **Stato: scaffolding iniziale.** Questa cartella contiene per ora solo la struttura del
> progetto e le note di design. Nessun codice funzionante è ancora presente.

---

## Obiettivi

Realizzare un bootloader che garantisca:

| Obiettivo | Descrizione |
|---|---|
| **Secure Boot** | Verifica della firma dell'immagine applicativa prima del salto |
| **Root of Trust** | Chiave pubblica immutabile (OTP / option bytes / RSS) |
| **Secure Firmware Update** | Aggiornamento autenticato, con anti‑rollback |
| **Anti‑rollback** | Contatore di versione monotono, non decrementabile |
| **Dual‑bank / A‑B** | Aggiornamento con fallback in caso di immagine corrotta |
| **Confidenzialità** | Immagine cifrata (AES‑GCM / AES‑CTR + MAC) — opzionale |
| **Isolamento** | TrustZone: bootloader in Secure, applicazione in Non‑Secure |
| **Debug lock** | RDP a livello adeguato in produzione |

---

## Struttura della cartella

```
stm32c542kct-secure-bootloader/
├── README.md          questo file
├── docs/              specifiche, threat model, formato immagine
├── inc/               header pubblici del bootloader
├── src/               sorgenti C
├── linker/            linker script e mappa di memoria
├── tools/             tool host (firma, packaging, key management)
└── tests/             test unitari / vettori di test crypto
```

---

## Scelte fatte

Le decisioni tecniche sono fissate in [`docs/00-decisions.md`](docs/00-decisions.md),
con motivazioni e conseguenze:

| Ambito | Scelta |
|---|---|
| Toolchain | STM32CubeIDE |
| Firma | ECDSA P‑256 + SHA‑256 |
| Crypto | X‑CUBE‑CRYPTOLIB (ECDSA in software: niente PKA) |
| Confidenzialità | Nessuna — immagine in chiaro, solo firmata |
| Layout | Boot 48 KB + esecuzione 100 KB + staging 100 KB |
| Indirizzo app | Fisso, una sola build — vector table a `0x0800_C200` |
| Canale di update | CAN / CAN‑FD, UDS su ISO‑TP |
| TrustZone | Disabilitata, tutto Secure |
| Root key | Hash SHA‑256 in OTP, chiave in flash |
| Chiave privata | File offline su macchina dedicata |
| Rotazione chiavi | Nessuna, chiave singola |
| Anti‑rollback | Contatore monotono in OTP |

Restano da chiarire, segnati con ⚠️ nei documenti:

1. ⚠️ **X‑CUBE‑CRYPTOLIB supporta la serie C5?** Uscita a marzo 2026, non
   confermato. Piano B: micro‑ecc o Mbed TLS ridotta.
2. ⚠️ **L'applicazione sta in 100 KB?**
3. ⚠️ **Dimensione della pagina di flash.** Determina la granularità di WRP e
   HDP, e se gli 8 KB di metadati bastano per la doppia copia del descrittore.
4. ⚠️ **Quanti FDCAN** sul package a 32 pin.
5. ⚠️ **Livello di RDP** in produzione, e se usare `0x27` SecurityAccess.

Dettagli e ripartizione della flash in [`docs/04-silicon-facts.md`](docs/04-silicon-facts.md).

---

## Riferimenti da verificare sul datasheet

I valori esatti vanno confermati sul reference manual e datasheet ST del
pezzo specifico (`STM32C542KCT6`), in particolare:

- dimensione Flash e RAM, e supporto dual‑bank
- presenza e tipo di acceleratori crypto (AES / PKA / HASH / RNG)
- meccanismi di secure boot ST (RSS / OBK / HDPL, se applicabili)
- granularità delle protezioni WRP / HDP / RDP

---

## Prossimi passi

- [x] Fissare le decisioni tecniche (`docs/00-decisions.md`)
- [x] Raccogliere i dati del silicio (`docs/04-silicon-facts.md`)
- [ ] Riconfermare i dati sul PDF ufficiale del datasheet
- [x] Scrivere il threat model (`docs/01-threat-model.md`)
- [x] Definire il formato dell'header immagine (`docs/02-image-format.md`)
- [x] Definire la mappa di memoria (`docs/03-memory-map.md`)
- [ ] Scrivere `inc/image_header.h` e il tool di firma in `tools/`
- [ ] Impostare la toolchain e un build "hello world" che lampeggia un LED
