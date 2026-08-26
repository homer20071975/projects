# STM32C542 — dati del silicio

> **Provenienza dei dati.** Il datasheet PDF non è stato accessibile da questa
> sessione (il proxy di rete blocca `st.com` e `mouser.com`), quindi i valori
> qui sotto vengono da fonti secondarie: pagine prodotto ST, schede distributori
> e documentazione Zephyr. **Vanno riconfermati sul PDF ufficiale**
> (`stm32c542cc_ds.pdf`) prima di congelare la mappa di memoria.

---

## Core e memorie

| Voce | Valore |
|---|---|
| Core | Arm Cortex‑M33, fino a 144 MHz |
| FPU | Single precision |
| Altro | Set DSP completo, MPU |
| Flash | **256 KB, dual‑bank, con ECC** |
| SRAM | **64 KB, con ECC** |
| OTP utente | **4.5 KB** |

Il dual‑bank è confermato: la scelta §5 (A/B) regge.

L'OTP da 4.5 KB è abbondante per quello che ci serve — 32 byte per l'hash della
root key più i bit del contatore anti‑rollback.

---

## Periferiche di sicurezza

| Periferica | Presente | Note |
|---|---|---|
| AES | ✅ | Acceleratore hardware |
| HASH | ✅ | SHA‑1 / SHA‑224 / SHA‑256 |
| TRNG | ✅ | Generatore di numeri casuali vero |
| **PKA** | ❌ | **Assente.** Disponibile solo sulla variante STM32C5A3 |
| RDP / WRP / HDP | ✅ | Readout, write e hide protection su flash e SRAM |

### La conseguenza: niente PKA

Il PKA è l'acceleratore per l'aritmetica su curva ellittica. Senza di lui la
**verifica ECDSA P‑256 deve essere fatta in software**: l'AES e lo HASH non
servono a nulla per la firma asimmetrica.

Questo invalida la premessa della decisione §3 (§ = `00-decisions.md`), che
sceglieva gli acceleratori hardware proprio per accelerare la verifica.

Cosa resta comunque utile dell'hardware:
- **HASH** per lo SHA‑256 sull'immagine, che è il grosso del lavoro al boot
  (centinaia di KB da digerire) — questo sì è accelerato;
- **TRNG** se in seguito servirà un challenge per lo `0x27` SecurityAccess UDS;
- **AES** solo se in futuro si aggiungerà la cifratura dell'immagine (§4).

---

## Connettività

| Voce | Valore |
|---|---|
| FDCAN | fino a 2 interfacce |
| Package | da 20 a 64 pin |

⚠️ Il pezzo `STM32C542KC` è, secondo la nomenclatura ST, il **package a 32 pin**
(`K`) con 256 KB di flash (`C`). Sui package piccoli il numero di FDCAN
disponibili può essere ridotto: **da verificare sulla tabella pinout** che il
32 pin esponga almeno un FDCAN.

---

## Budget di flash per il dual-bank

256 KB totali, due banchi da 128 KB.

Proposta di ripartizione:

```
Bank 1  0x0800_0000  ┌──────────────────────┐
                     │ Bootloader    32 KB  │
        0x0800_8000  ├──────────────────────┤
                     │ Slot A        96 KB  │
        0x0802_0000  ├──────────────────────┤  ← inizio Bank 2
                     │ Slot B        96 KB  │
        0x0803_8000  ├──────────────────────┤
                     │ Metadati      32 KB  │
        0x0804_0000  └──────────────────────┘
```

**Il vincolo che ne esce: l'applicazione deve stare in circa 96 KB.**

È il numero da validare per primo — se l'applicativo previsto non ci sta, il
dual‑bank A/B non è praticabile su questo pezzo e si deve ripiegare su single
slot con staging area.

Il bootloader a 32 KB è plausibile ma non largo: deve contenere ECDSA software
(micro‑ecc sta in circa 6 KB), driver flash, FDCAN, ISO‑TP e un sottoinsieme
UDS. Se stringe, si può spostare il confine a 48 KB portando gli slot a 88 KB.

---

## Da riconfermare sul PDF ufficiale

- [ ] Flash 256 KB e dual‑bank sul `STM32C542KCT6` specifico
- [ ] Assenza del PKA (è il punto che cambia le decisioni: va verificato bene)
- [ ] Dimensione e granularità dell'OTP, e come si scrive
- [ ] Numero di FDCAN sul package a 32 pin
- [ ] Dimensione della pagina di flash, per la granularità di WRP e HDP
