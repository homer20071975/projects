# Backend crittografico per il target

`src/crypto_stm32.c` realizza `inc/crypto.h` sul C542, usando l'acceleratore
HASH per lo SHA‑256 e X‑CUBE‑CRYPTOLIB (API cmox) per la verifica ECDSA
P‑256, che è software perché il pezzo non ha il PKA (§3, e
[`04-silicon-facts.md`](04-silicon-facts.md)).

---

## ⚠️ Stato: scritto, mai compilato

Il file è stato scritto senza accesso agli header della cryptolib, all'HAL del
C5 e a un target reale. **I nomi delle API, le costanti e la dimensione del
buffer di lavoro sono assunzioni ragionate, non verificate.**

Va trattato come una bozza da validare, non come codice funzionante.

### Cosa verificare, in ordine

1. **La cryptolib supporta la serie C5?** È la domanda che viene prima di
   tutte: la serie è uscita a marzo 2026 e il supporto non risulta confermato.
   Se manca, il file va riscritto su micro‑ecc o Mbed TLS ridotta.
2. **Nomi degli header** — `cmox_crypto.h`, `stm32c5xx_hal.h`.
3. **`cmox_initialize`** — firma e valore di ritorno.
4. **`CMOX_ECC_SECP256R1_VERIFY_BUF_LEN`** — il nome vero della macro per la
   dimensione del buffer di lavoro. ⚠️ Un buffer sottodimensionato non dà un
   errore pulito: dà comportamento indefinito. È il punto più pericoloso del
   file.
5. **`cmox_ecc_construct` / `cmox_ecdsa_verify` / `cmox_ecc_cleanup`** —
   ordine dei parametri, formato atteso della chiave pubblica (si assume
   X ‖ Y non compresse, 64 byte, come sta in flash) e della firma (r ‖ s).
6. **`HAL_HASHEx_SHA256_Start`** — sul C5 l'API potrebbe essere quella
   unificata più recente, con l'algoritmo scelto in `Init` e `HAL_HASH_Start`
   come punto d'ingresso.
7. **Vincoli dell'acceleratore HASH** su allineamento e lunghezza
   dell'ingresso, sul reference manual.

---

## Come si dimostra che funziona

Non leggendo il codice: **facendo passare `tests/target/selftest.c` sul
pezzo**.

Il self‑test esegue lo stesso corpus di vettori già verificato su host contro
OpenSSL — 32 casi che coprono tutti i passi della specifica — e confronta gli
esiti con quelli attesi. Se il backend del target si comporta come quello
host, passa; se diverge, dice su quale vettore.

```
# su host, con il backend OpenSSL: già verificato
make -C tests/target && ./tests/target/selftest
    self-test: 32 superati, 0 falliti su 32

# su target: stessi file, sostituendo il backend
#   ../host/crypto_openssl.c  →  ../../src/crypto_stm32.c
```

Il self‑test non fa parte del firmware di produzione: si compila in un binario
a sé. `SBL_SELFTEST_LOG` va reindirizzato su UART, ITM o un buffer in RAM da
leggere col debugger.

---

## Due scelte di sicurezza nel backend

### Doppio controllo contro il fault injection

`cmox_ecdsa_verify` restituisce l'esito due volte, per due strade diverse: nel
valore di ritorno e nel parametro `fault_check`. Il backend pretende che
**entrambi** dicano successo.

Un singolo glitch che forza uno dei due difficilmente forza anche l'altro.
`fault_check` è inizializzato a "fallito", così se la chiamata non parte
affatto l'esito resta negativo.

È una mitigazione parziale della M10 del threat model, non una difesa
completa: contro un attaccante attrezzato servono anche ritardi casuali e
ridondanza sul flusso di controllo.

### Il digest si calcola nel backend

`sbl_ecdsa_p256_verify` prende il **messaggio**, non il digest, benché cmox
voglia il digest. Il backend calcola lo SHA‑256 dei 448 byte al suo interno.

Costa una manciata di microsecondi e tiene il chiamante — `src/verify.c` —
all'oscuro di quale hash usi la firma. Se un domani si passasse a un'altra
curva o a un altro digest, cambierebbe solo il backend.
