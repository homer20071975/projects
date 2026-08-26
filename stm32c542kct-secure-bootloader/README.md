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
| Crypto | Acceleratori hardware (PKA / AES / HASH / RNG) |
| Confidenzialità | Nessuna — immagine in chiaro, solo firmata |
| Layout | Dual‑bank A/B |
| Canale di update | CAN / CAN‑FD, UDS su ISO‑TP |
| TrustZone | Disabilitata, tutto Secure |
| Root key | Hash SHA‑256 in OTP, chiave in flash |
| Chiave privata | File offline su macchina dedicata |
| Rotazione chiavi | Nessuna, chiave singola |
| Anti‑rollback | Contatore monotono in OTP |

Tre punti restano da chiarire prima di scrivere codice, e sono segnati con ⚠️
nel documento delle decisioni:

1. **Il pezzo ha il PKA?** Se manca, la scelta sulla crypto va rivista.
2. **La Flash basta per il dual‑bank?** Servono due slot applicativi completi.
3. **Quanti incrementi anti‑rollback** dimensionare in OTP.

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
- [ ] Confermare a datasheet i tre punti aperti qui sopra
- [ ] Scrivere il threat model (`docs/01-threat-model.md`)
- [ ] Definire il formato dell'header immagine (`docs/02-image-format.md`)
- [ ] Definire la mappa di memoria (`docs/03-memory-map.md`)
- [ ] Impostare la toolchain e un build "hello world" che lampeggia un LED
