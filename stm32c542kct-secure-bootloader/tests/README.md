# Test

```
python3 tests/test_image_format.py
```

31 test, nessuna dipendenza oltre a quelle di `tools/`.

## Cosa coprono

`test_image_format.py` percorre la tabella dei vettori di test di
[`../docs/02-image-format.md`](../docs/02-image-format.md) e verifica non solo
che un'immagine malformata venga rifiutata, ma **a quale passo**.

Il passo conta. Un'immagine con `image_size` gonfiato deve cadere al passo 4,
sui controlli di limite, non al passo 8 sulla firma: se cadesse al passo 8
vorrebbe dire che il bootloader ha già letto fuori area. Un test che
controllasse solo "rifiutata" lascerebbe passare quel bug.

`CHeaderConsistencyTest` rilegge i `#define` da `inc/image_header.h` e
`inc/memory_map.h` e li confronta con le costanti Python. Se divergono, il tool
firmerebbe su un layout e il bootloader ne verificherebbe un altro, e il guasto
si manifesterebbe solo sul target.

## Cosa manca ancora

- **Vettori ufficiali ECDSA P‑256** (NIST CAVP, Wycheproof). Vanno scaricati e
  aggiunti: qui si verifica che la catena regga, non che l'implementazione
  ECDSA sia corretta sui casi limite.
- **Test di coerenza sul target**: gli stessi vettori eseguiti contro
  X‑CUBE‑CRYPTOLIB, per dimostrare che il backend del target e quello host
  danno lo stesso risultato. È la mitigazione prevista dalla nota sui test in
  `00-decisions.md`, e senza di essa il backend software resta non collegato a
  ciò che gira davvero.
