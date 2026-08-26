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

## Decisioni ancora da prendere

Prima di scrivere codice servono alcune scelte. Sono raccolte in
[`docs/00-open-questions.md`](docs/00-open-questions.md):

1. **Toolchain**: STM32CubeIDE, CMake + arm-none-eabi-gcc, o Makefile puro?
2. **Crypto**: usare l'acceleratore hardware del C5, la libreria ST, o
   una libreria portabile (es. Mbed TLS ridotta / micro-ecc / tinycrypt)?
3. **Algoritmo di firma**: ECDSA P‑256 + SHA‑256 (consigliato) oppure
   Ed25519 / RSA‑2048?
4. **Layout memoria**: dual‑bank A/B oppure single slot + staging area?
5. **Canale di update**: UART, USB DFU, CAN, o SD/SPI‑flash esterna?
6. **RTOS o bare‑metal** nell'applicazione (impatta lo startup Non‑Secure).

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

- [ ] Compilare `docs/00-open-questions.md` con le scelte
- [ ] Scrivere il threat model (`docs/01-threat-model.md`)
- [ ] Definire il formato dell'header immagine (`docs/02-image-format.md`)
- [ ] Definire la mappa di memoria (`docs/03-memory-map.md`)
- [ ] Impostare la toolchain e un build "hello world" che lampeggia un LED
