# Decisioni aperte

Da compilare prima di iniziare l'implementazione. Ogni riga: scegliere una
opzione, cancellare le altre, annotare il motivo.

---

## 1. Toolchain e build system

| Opzione | Pro | Contro |
|---|---|---|
| STM32CubeIDE | generatore CubeMX integrato, debug pronto | build non riproducibile in CI, progetto XML |
| CMake + arm-none-eabi-gcc | riproducibile, CI-friendly, testabile su host | setup iniziale più lungo |
| Makefile puro | semplice, zero dipendenze | scala male con i test host |

**Scelta:** _da decidere_
**Motivo:**

---

## 2. Libreria crypto

| Opzione | Note |
|---|---|
| Acceleratore HW (PKA/AES/HASH) | più veloce, meno flash, ma legato al silicio |
| Libreria ST (X-CUBE-CRYPTOLIB) | supportata, licenza da verificare |
| Mbed TLS (subset) | portabile, testabile su host, footprint medio |
| micro-ecc / tinycrypt | footprint minimo, solo ECC |

**Scelta:** _da decidere_
**Motivo:**

---

## 3. Algoritmo di firma

- [ ] ECDSA P-256 + SHA-256 — *consigliato*: chiavi piccole, HW accelerabile
- [ ] Ed25519 + SHA-512 — veloce, ma di solito senza accelerazione HW
- [ ] RSA-2048/3072 + SHA-256 — verifica veloce, chiave pubblica grande

**Scelta:** _da decidere_

---

## 4. Confidenzialità dell'immagine

- [ ] Nessuna (immagine in chiaro, solo firmata)
- [ ] AES-256-GCM (cifratura + autenticazione in un passo)
- [ ] AES-256-CTR + HMAC-SHA256

**Scelta:** _da decidere_
**Dove sta la chiave:** _da decidere_ (OTP / OBK / derivata da DHUK?)

---

## 5. Layout di aggiornamento

- [ ] **Dual-bank A/B**: due slot completi, swap atomico, rollback immediato
- [ ] **Single slot + staging**: uno slot attivo + area di download, copia al boot
- [ ] **Staging su flash esterna** (SPI/QSPI)

**Scelta:** _da decidere_
**Vincolo:** dipende dalla Flash totale del `STM32C542KCT` — **da verificare a datasheet**

---

## 6. Canale di aggiornamento

- [ ] UART (protocollo custom o Y-Modem)
- [ ] USB DFU
- [ ] CAN / CAN-FD (UDS 0x34/0x36/0x37?)
- [ ] SD card / flash esterna

**Scelta:** _da decidere_

---

## 7. TrustZone

- [ ] Abilitata: bootloader Secure, applicazione Non-Secure
- [ ] Disabilitata: tutto Secure (più semplice, meno isolamento)

**Scelta:** _da decidere_

---

## 8. Gestione chiavi

- Dove risiede la chiave pubblica di root? _da decidere_
- Come si revoca / ruota una chiave? _da decidere_
- Chi custodisce la chiave privata di firma (HSM? file offline?) _da decidere_
- Serve una catena di certificati o basta una chiave singola? _da decidere_
