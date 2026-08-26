# Tool host

Richiedono Python 3.8+ e `cryptography`:

```
pip install -r tools/requirements.txt
```

| File | |
|---|---|
| `sign_image.py` | CLI: `keygen`, `pubkey`, `sign`, `info`, `verify` |
| `sbl_format.py` | Formato dell'header e **verificatore di riferimento** |
| `sbl_crypto.py` | Wrapper ECDSA P‑256 su `cryptography` |

## Uso

```
./sign_image.py keygen -o signing_key.pem
./sign_image.py sign -i app.bin -k signing_key.pem \
    --sec-version 3 --fw-version 1.4.2.117 --product-id 0x1 \
    -o app_signed.bin
./sign_image.py verify -i app_signed.bin -k signing_key.pem
```

`keygen` stampa la chiave pubblica da mettere in flash nel bootloader e il suo
SHA‑256 da scrivere in OTP.

## Il verificatore di riferimento

`sbl_format.verify()` implementa i dodici passi di
[`../docs/02-image-format.md`](../docs/02-image-format.md) nello stesso ordine
e con gli stessi codici di errore dell'implementazione C.

Serve a due cose: far girare i test su PC, dove il codice del target non
arriva (§3 e la nota sui test in `00-decisions.md`), e fare da specifica
eseguibile — se il C e questo Python divergono su un caso, uno dei due ha un
bug e il test lo dice.

## ⚠️ La chiave privata

`keygen` scrive un file con permessi `600`. È adeguato per lo sviluppo, **non
per la produzione**: vedi il rischio R2 in
[`../docs/01-threat-model.md`](../docs/01-threat-model.md). Chiave unica, senza
rotazione, in un file: se esce, non c'è recupero.
