# Formato dell'immagine firmware

Un'immagine è un **header di 512 byte** seguito dal **payload**, cioè il
binario dell'applicazione a partire dalla sua vector table.

```
+---------------------------+  ← base dell'area (0x0800_C000 in esecuzione)
|  Header        512 byte   |
+---------------------------+  ← 0x0800_C200, vector table dell'applicazione
|  Payload    image_size B  |
+---------------------------+
```

Perché 512 byte: `VTOR` vuole la vector table allineata alla potenza di due
immediatamente superiore alla sua dimensione. Con un centinaio di interrupt la
tabella sta in 464 byte, quindi serve un allineamento a 512. Un header di 512
byte lo garantisce e lascia spazio per crescere.

---

## Struttura dell'header

Tutti i campi sono **little‑endian**, coerentemente con il Cortex‑M33.

| Offset | Dim. | Campo | Descrizione |
|---|---|---|---|
| `0x000` | 4 | `magic` | `'S' 'B' 'L' '1'` — `0x314C4253` |
| `0x004` | 2 | `header_version` | Versione di questo formato. Attualmente `1` |
| `0x006` | 2 | `header_size` | `512`. Presente per poter crescere |
| `0x008` | 4 | `image_size` | Dimensione del payload in byte, header escluso |
| `0x00C` | 4 | `load_address` | Indirizzo a cui il payload va scritto |
| `0x010` | 4 | `entry_vtor` | Indirizzo della vector table dell'applicazione |
| `0x014` | 4 | `security_version` | Contatore anti‑rollback, monotono |
| `0x018` | 4 | `firmware_version` | Informativa: `MM.mm.pp.bb`, un byte per campo |
| `0x01C` | 4 | `product_id` | Identifica il prodotto |
| `0x020` | 32 | `payload_sha256` | SHA‑256 del payload |
| `0x040` | 384 | `reserved` | A `0xFF`. Riservato a estensioni future |
| `0x1C0` | 64 | `signature` | Firma ECDSA P‑256, `r ‖ s`, 32 byte ciascuno |

Totale: 512 byte.

### Cosa copre la firma

**La regione firmata è `0x000`–`0x1BF`, cioè i primi 448 byte dell'header.**

```
digest    = SHA-256( header[0x000 .. 0x1BF] )
signature = ECDSA-P256-Sign( chiave privata, digest )
```

La firma sta **fuori** dalla regione che copre, in coda all'header. Questo
permette di firmare l'header per intero — campi riservati compresi — senza il
trucco di azzerare il campo firma prima di calcolare il digest.

Il payload non viene firmato direttamente: è legato alla firma attraverso
`payload_sha256`, che sta dentro la regione firmata. Alterare un solo byte del
payload cambia l'hash, che non corrisponderà più a quello firmato.

Il vantaggio pratico è che la verifica ECDSA lavora sempre su 448 byte, mentre
lo SHA‑256 sui ~100 KB del payload può usare l'acceleratore HASH.

---

## Sequenza di verifica

L'ordine conta: i controlli economici e quelli sui limiti vengono prima di
quelli costosi, e prima di qualunque lettura dimensionata da campi
dell'header.

```
 1. magic == 'SBL1'                              → altrimenti: nessuna immagine
 2. header_version supportata
 3. header_size == 512
 4. image_size > 0  &&  image_size <= capienza dell'area
 5. load_address == base attesa per quest'area
 6. entry_vtor dentro l'area, allineato a 512
 7. hash della chiave pubblica di root == hash in OTP
 8. ECDSA-Verify( chiave pubblica, header[0..448), signature )
 9. security_version >= contatore anti-rollback in OTP
10. product_id == quello del dispositivo
11. SHA-256( payload ) == payload_sha256
12. → VTOR = entry_vtor, salto
```

Note su singoli passi:

- **4 e 5 prima di 11.** Il passo 11 legge `image_size` byte: se non fosse
  già stato validato contro la capienza dell'area, un `image_size` gonfiato
  farebbe leggere fuori dai limiti. È il classico punto in cui un bootloader
  si fa male da solo.
- **7 prima di 8.** Verificare la firma con una chiave non autenticata non
  dimostra nulla. La radice di fiducia è l'hash in OTP, non la chiave in flash.
- **9 dopo 8.** Il `security_version` è attendibile solo dopo che la firma
  ha dimostrato che nessuno l'ha toccato.
- **11 per ultimo** perché è il più costoso: ~100 KB da digerire.

---

## Verifica al boot e verifica in download

La stessa sequenza gira in due momenti diversi, con una differenza:

| | Al boot | Sul download nello staging |
|---|---|---|
| Area | Esecuzione | Staging |
| `load_address` atteso | `0x0800_C000` | `0x0800_C000` |
| Esito negativo | Non salta, resta in modalità update | Rifiuta l'immagine, non tocca l'esecuzione |

Nota su `load_address`: nell'immagine in staging il campo indica comunque
l'indirizzo **di esecuzione**, non quello dello staging, perché descrive dove
l'immagine dovrà girare. Il bootloader lo verifica contro l'area di esecuzione
in entrambi i casi.

---

## Il tool di firma

Vive in `tools/`. Riceve il binario dell'applicazione e produce l'immagine
completa:

```
sign_image.py  --input      app.bin
               --key        signing_key.pem
               --load-addr  0x0800C000
               --sec-version 3
               --fw-version  1.4.2.117
               --product-id  0x00000001
               --output     app_signed.bin
```

Passi: calcola `payload_sha256`, compila i campi, calcola il digest sui primi
448 byte, firma con ECDSA P‑256, scrive header e payload concatenati.

**Il tool deve rifiutarsi di firmare** se `image_size` supera la capienza
dell'area: meglio un errore in fase di build che un'immagine che nessun
dispositivo accetterà.

---

## Vettori di test

In `tests/` servono, come minimo:

| Caso | Esito atteso |
|---|---|
| Immagine valida | Accettata |
| `magic` corrotto | Rifiutata al passo 1 |
| Un byte del payload alterato | Rifiutata al passo 11 |
| Un byte dell'header alterato | Rifiutata al passo 8 |
| Firma alterata | Rifiutata al passo 8 |
| Firma valida ma di un'altra chiave | Rifiutata al passo 8 |
| `security_version` inferiore al contatore | Rifiutata al passo 9 |
| `image_size` oltre la capienza dell'area | Rifiutata al passo 4 |
| `image_size` = `0xFFFFFFFF` | Rifiutata al passo 4, senza letture fuori area |
| `product_id` di un altro prodotto | Rifiutata al passo 10 |
| Area cancellata, tutta a `0xFF` | Rifiutata al passo 1 |

Più i vettori ufficiali NIST/Wycheproof per ECDSA P‑256, che vanno eseguiti
sul backend software su host — vedi la nota sui test in
[`00-decisions.md`](00-decisions.md).

---

## Definizione C

Da mettere in `inc/image_header.h`:

```c
#define SBL_MAGIC           0x314C4253u  /* 'SBL1' little-endian */
#define SBL_HEADER_SIZE     512u
#define SBL_SIGNED_LEN      448u         /* regione coperta dalla firma */

typedef struct {
    uint32_t magic;
    uint16_t header_version;
    uint16_t header_size;
    uint32_t image_size;
    uint32_t load_address;
    uint32_t entry_vtor;
    uint32_t security_version;
    uint32_t firmware_version;
    uint32_t product_id;
    uint8_t  payload_sha256[32];
    uint8_t  reserved[384];
    uint8_t  signature[64];
} sbl_image_header_t;

_Static_assert(sizeof(sbl_image_header_t) == SBL_HEADER_SIZE,
               "header deve essere esattamente 512 byte");
_Static_assert(offsetof(sbl_image_header_t, signature) == SBL_SIGNED_LEN,
               "la firma deve iniziare dove finisce la regione firmata");
```

Le due `_Static_assert` non sono decorative: proteggono da un padding
inatteso del compilatore, che romperebbe silenziosamente la compatibilità
fra tool di firma e bootloader.
