# Trasporto: FDCAN e ISO‑TP

Due strati, con statuti molto diversi.

| | File | Stato |
|---|---|---|
| **ISO‑TP** | `src/isotp.c` | ✅ Collaudato su host, 262 verifiche |
| **Driver FDCAN** | `src/can_stm32.c` | ⚠️ Scritto, mai compilato |

La separazione è quella di `inc/can.h`: il protocollo parla col bus attraverso
puntatori a funzione, quindi gira identico sul target e sotto i test.

---

## ISO‑TP — ISO 15765‑2

Segmenta messaggi più lunghi di un frame e li riassembla dall'altra parte.

| Frame | Cosa fa |
|---|---|
| Single Frame | Il messaggio entra tutto in un frame |
| First Frame | Primo pezzo, con la lunghezza totale |
| Consecutive Frame | Pezzi successivi, numerati |
| Flow Control | Il ricevente detta il ritmo al mittente |

### Cosa è implementato

- Single Frame classico (fino a 7 byte) **e** la forma estesa del CAN FD, che
  copre i messaggi fino a `tx_dlen − 2` in un frame solo
- First Frame con lunghezza a 12 bit e con lunghezza **estesa a 32 bit** oltre
  i 4095 byte
- Block Size e STmin, in entrambe le direzioni
- Timeout `N_Bs` (attesa del Flow Control) e `N_Cr` (attesa dei Consecutive)
- Flow Control con Wait e con Overflow
- Riempimento dei frame corti, opzionale
- Lunghezze discrete del CAN FD: un payload da 9 byte viaggia in un frame da
  12 con tre byte di riempimento

### Sulla dimensione dei messaggi

**L'immagine da 100 KB non viaggia in un unico messaggio ISO‑TP**, e non
potrebbe: non entrerebbe nei 64 KB di SRAM.

UDS la spezza in più richieste `0x36` TransferData, ciascuna delle quali è un
messaggio ISO‑TP di pochi kilobyte. Il buffer di ricezione va dimensionato su
quello.

### Cosa non è implementato

- Indirizzamento esteso e mixed addressing: si assume normal addressing, un
  identificatore per direzione
- `N_As`, `N_Ar`, `N_Br`, `N_Cs`: i timeout lato trasmissione verso il driver.
  Con l'invio non bloccante di `sbl_can_send` non servono, ma se il collaudo
  in rete mostrasse blocchi vanno aggiunti
- Le separazioni sotto il millisecondo di STmin (`0xF1`–`0xF9`) diventano
  1 ms. È più lento del minimo richiesto, mai più veloce: non viola il vincolo
  dell'altro capo

---

## Il collaudo, e perché è fatto così

`tests/host/test_isotp.c` mette due collegamenti uno di fronte all'altro su un
bus simulato: 262 verifiche fra round trip a lunghezze che attraversano tutte
le forme del protocollo e casi storti — sequenza fuori ordine, timeout in
entrambe le direzioni, messaggio più grande del buffer, code hardware piene.

### Il collaudo per mutazione

`tests/mutate_isotp.py` rompe il protocollo di proposito, una cosa alla volta,
e verifica che i test se ne accorgano.

Non è un esercizio di stile: **alla prima esecuzione una mutazione è passata
inosservata.** Azzerando il calcolo della separazione fra frame — cioè
ignorando lo STmin imposto dal ricevente — l'intera suite restava verde,
perché i test si limitavano a controllare che il messaggio arrivasse. E
arrivava: un mittente che spara i frame senza pause consegna comunque.

Da lì sono nati `test_st_min_rispettato`, che misura le distanze effettive fra
i Consecutive Frame, e `test_st_min_zero_non_rallenta`. Ora le dieci mutazioni
sono tutte rilevate.

Va rilanciato a mano dopo ogni modifica al protocollo o ai suoi test.

---

## Driver FDCAN — ⚠️ mai compilato

`src/can_stm32.c` è scritto senza accesso all'HAL del C5 né a un target.

### Da verificare, in ordine

1. **Il package a 32 pin espone un FDCAN?** Il datasheet dice "fino a 2" per
   la famiglia, ma sui package piccoli il numero può essere ridotto. Se non lo
   espone, salta la decisione §6 e va rivisto il canale di aggiornamento.
2. **I parametri di temporizzazione del bus.** Quelli nel file sono segnaposto
   per 500 kbit/s nominali e 2 Mbit/s in fase dati con clock da 40 MHz. ⚠️ Non
   vanno indovinati: si calcolano dal clock effettivo, e il punto di
   campionamento va concordato con gli altri nodi. **Un bus con parametri
   sbagliati non funziona "un po' peggio": non funziona.**
3. **La tabella di conversione fra byte utili e codice DLC.** Sbagliarla non
   dà un errore: dà frame di lunghezza sbagliata, difetto che si manifesta
   solo su certi messaggi.
4. Nomi delle API e delle costanti HAL per la serie C5.
5. **Gli identificatori CAN** del prodotto, ancora da fissare.

### Cosa resta da decidere

- Identificatori per richiesta e risposta
- Velocità nominale e in fase dati
- Se serve `0x27` SecurityAccess prima di autorizzare il download (M6 del
  threat model, ancora aperta)
