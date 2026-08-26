# Test

```
./tests/run_all.sh          # dalla radice del progetto
```

Serve `gcc`, `libcrypto` (OpenSSL) e il pacchetto in `tools/requirements.txt`.

| File | |
|---|---|
| `vectors.py` | **Il corpus**: 33 vettori, unica fonte per entrambi i test |
| `test_image_format.py` | Il corpus contro il riferimento Python |
| `test_differential.py` | Il corpus contro l'implementazione C |
| `host/` | Banco di prova: backend OpenSSL e runner |

## Il confronto differenziale

`test_differential.py` compila **`src/verify.c`, il codice vero del
bootloader**, non una riscrittura, mettendogli sotto un backend OpenSSL al
posto di X‑CUBE‑CRYPTOLIB. Poi gli dà lo stesso corpus del riferimento Python
e pretende che i due diano lo stesso codice di esito su ogni vettore.

È la mitigazione prevista dalla nota sui test in `00-decisions.md`: la
cryptolib ST non compila su PC, e senza l'interfaccia `inc/crypto.h` con un
backend alternativo il codice di verifica non sarebbe testabile fuori dal
target — proprio il codice che non vuoi che abbia bug.

Ha già ripagato il suo costo: alla prima esecuzione ha trovato che il C
respingeva un `image_size` oltre i byte leggibili al passo 4, mentre il Python
tirava avanti fino al passo 8. Il C aveva ragione, e il riferimento è stato
corretto.

## Perché i test controllano il passo, non solo l'esito

Un'immagine con `image_size` gonfiato deve cadere al passo 4, sui controlli di
limite, non al passo 8 sulla firma. Se cadesse sulla firma vorrebbe dire che il
bootloader ha già letto oltre la fine dell'area. Un test che verificasse solo
"rifiutata" lascerebbe passare esattamente quel bug.

`test_il_corpus_copre_tutti_i_codici` verifica che ogni passo della specifica
abbia almeno un vettore che lo fa scattare.

## Cosa manca ancora

- **Vettori ufficiali ECDSA P‑256** (NIST CAVP, Wycheproof). Qui si verifica
  che la catena regga e che C e Python concordino, non che l'ECDSA sia
  corretto sui casi limite.
- **Esecuzione sul target.** Il confronto differenziale gira su PC con
  OpenSSL: dimostra che la *logica* è la stessa, non che X‑CUBE‑CRYPTOLIB si
  comporti come OpenSSL. Lo stesso corpus va fatto girare sul pezzo.
