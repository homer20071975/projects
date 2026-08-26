# Decisioni di progetto

Scelte fatte il 2026-08-26. Ogni voce riporta la decisione, il motivo e le
conseguenze che ne derivano per l'implementazione.

---

## 1. Toolchain — STM32CubeIDE

Generatore CubeMX e debugger integrati, si parte subito.

**Conseguenze:**
- Il progetto è XML: la build non è riproducibile in CI senza lavoro extra.
- I test unitari su host non sono immediati. Vedi la nota in §11.

---

## 2. Firma — ECDSA P-256 + SHA-256

Standard de facto per il secure boot su Cortex-M. Chiave pubblica 64 byte
(coordinate X,Y non compresse), firma 64 byte (r,s).

**Conseguenze:**
- L'header immagine riserva 64 byte per la firma e 64 per la chiave pubblica.
- La verifica richiede aritmetica su curva: accelerata dal PKA se presente (§3).

---

## 3. Crypto — X-CUBE-CRYPTOLIB di ST, ECDSA in software

> **Storia della decisione.** Inizialmente si erano scelti gli acceleratori
> hardware per la verifica ECDSA. Il `STM32C542` però **non ha il PKA** (solo
> la variante `C5A3` ce l'ha), quindi la verifica su curva va fatta in
> software. Vedi [`04-silicon-facts.md`](04-silicon-facts.md).

Libreria ST, gratuita sotto licenza **SLA0048**, integrata con l'ecosistema
STM32Cube e quindi coerente con la scelta di CubeIDE (§1).

**⚠️ Da verificare per primo:** che X-CUBE-CRYPTOLIB **supporti già la serie
C5**, uscita a marzo 2026. Non è stato possibile confermarlo. Se il supporto
non c'è, il ripiego è micro-ecc (circa 6 KB, solo ECC) o Mbed TLS ridotta.

**Uso previsto dell'hardware disponibile:**
- **HASH** per lo SHA-256 sull'immagine — è il lavoro più pesante al boot in
  byte processati, e questo sì è accelerato.
- **TRNG** se servirà un challenge per lo `0x27` SecurityAccess UDS.
- **AES** inutilizzato finché l'immagine resta in chiaro (§4).

**Conseguenza sui test.** La cryptolib ST non compila su PC, quindi la verifica
firma resta non testabile fuori dal target. La mitigazione descritta in fondo a
questo documento diventa quindi necessaria, non opzionale: interfaccia
`inc/crypto.h` con un backend software di sola verifica usato **esclusivamente
nei test su host**, e un test di coerenza sul target che confronta i due
risultati sugli stessi vettori.

## 4. Confidenzialità — nessuna, immagine in chiaro

L'immagine è firmata ma non cifrata. Autenticità e integrità sono garantite,
che è il requisito del secure boot.

**Conseguenze:**
- Chi intercetta un aggiornamento sul bus CAN può leggere e fare reverse
  engineering del firmware.
- Nessuna chiave simmetrica da gestire in produzione: un problema in meno.
- Reversibile in seguito: aggiungere AES-GCM richiede un campo nell'header e
  una chiave di decifratura, senza toccare la catena di firma.

---

## 5. Layout — dual-bank A/B

Due slot applicativi completi, swap al boot, rollback immediato se la nuova
immagine non si avvia.

**✅ Confermato:** 256 KB di flash **dual-bank**. Il layout A/B è praticabile.

**Ripartizione fissata:** bootloader 48 KB, slot A e B da **80 KB ciascuno**,
48 KB di metadati e spare. Ogni banco è 128 KB e bootloader più slot A devono
starci insieme, da cui gli 80 KB e non 88. Indirizzi in
[`03-memory-map.md`](03-memory-map.md).

**Vincolo per l'applicazione: 80 KB.** Da validare.

**✅ Risolto** l'indirizzo di esecuzione: vedi §12.

**Conseguenze:**
- Serve un descrittore persistente che indichi lo slot attivo e lo stato
  (`pending` / `confirmed` / `failed`).
- L'applicazione deve confermare il proprio avvio, altrimenti al reset
  successivo il bootloader torna allo slot precedente.

---

## 6. Canale di update — CAN / CAN-FD, UDS su ISO-TP

Servizi diagnostici standard: `0x34` RequestDownload, `0x36` TransferData,
`0x37` RequestTransferExit, con ISO 15765-2 come strato di trasporto.

**Conseguenze:**
- Interoperabile con i tool diagnostici già esistenti.
- Serve uno stack ISO-TP nel bootloader (segmentazione, flow control).
- Da definire: gli identificatori CAN, il timing dei flow control e se serve
  il servizio `0x27` SecurityAccess prima di autorizzare il download.

---

## 7. TrustZone — disabilitata, tutto Secure

Sviluppo e debug molto più semplici, e il secure boot funziona ugualmente.

**Conseguenze:**
- Nessun isolamento a runtime: un bug nell'applicazione può raggiungere la
  flash del bootloader e la chiave pubblica. La protezione va affidata a
  WRP e HDP, configurate correttamente in produzione.
- Migrare a TrustZone in seguito non è gratis: va messo in conto se il
  requisito cambia.

---

## 8. Chiave pubblica di root — hash in OTP, chiave in flash

In OTP finiscono 32 byte: l'hash SHA-256 della chiave pubblica. La chiave vera
(64 byte) sta nella flash del bootloader e al boot viene verificata contro
l'hash prima di essere usata.

**Conseguenze:**
- Consumo di OTP contenuto: 32 byte invece di 64.
- Il boot fa un hash in più prima di ogni verifica di firma.
- Permette di correggere una chiave mal provisionata finché l'OTP non è
  bloccato.

---

## 9. Chiave privata di firma — file offline su macchina dedicata

Chiave cifrata su una macchina non connessa alla rete.

**⚠️ Nota esplicita:** questo **non** è uno schema adatto alla produzione di
volume. Combinato con la scelta §10 (nessuna rotazione), significa che una
compromissione della chiave privata non ha percorso di recupero: i dispositivi
in campo continuerebbero ad accettare firmware firmato dall'attaccante.
Da rivalutare prima della messa in produzione.

**Conseguenze:**
- Procedura di firma manuale, da documentare in `tools/`.
- Backup della chiave: se si perde, nessun aggiornamento è più possibile.

---

## 10. Rotazione e revoca — nessuna, chiave singola

Una chiave pubblica, una verifica, nessuna gestione di stato.

**Conseguenze:**
- Implementazione molto più semplice.
- Nessun recupero possibile in caso di compromissione. Vedi §9.

---

## 11. Anti-rollback — contatore monotono in OTP

Un contatore che sale e non scende mai. Nemmeno un attaccante con accesso in
scrittura alla flash può riportarlo indietro.

**Conseguenze:**
- **✅ OTP confermato: 4.5 KB**, abbondante. Anche riservando 256 byte al
  contatore in codifica unaria si hanno 2048 incrementi possibili.
- Resta da decidere quanti byte assegnargli.
- La codifica tipica è unaria (un bit per incremento) perché l'OTP passa solo
  da 1 a 0. Con N bit hai N incrementi.
- L'header immagine porta un campo `security_version` confrontato col contatore.

---

## 12. Indirizzo di esecuzione — due build, una per slot

Nessuno swap, né hardware né software. Ogni release produce due artefatti,
linkati e firmati separatamente per `0x0800_C000` e `0x0802_0000`.

Scartato il **bank swap hardware**: avrebbe dato una sola build a indirizzo
fisso duplicando il bootloader nei due banchi, ma dipende dal `SWAP_BANK`, che
sul C5 non è confermato, e costa una scrittura di option byte per aggiornamento.

Scartato lo **swap fisico** alla MCUboot: 80 KB copiati a ogni update, logorio
della flash e una macchina a stati resistente al power loss di cui non c'è
bisogno.

**Conseguenze:**
- Due artefatti da costruire, firmare e tracciare per release. La procedura
  deve garantire che vengano dallo stesso sorgente.
- L'header firmato porta l'indirizzo o l'identificativo dello slot di
  destinazione, e il bootloader rifiuta un'immagine destinata all'altro slot.
  È la mitigazione al rischio principale di questo schema.
- Il tool host interroga il dispositivo con UDS `0x22` per sapere quale slot è
  libero, e manda solo l'immagine giusta: 80 KB sul CAN, non 160.
- Header di **512 byte** prima della vector table, per tenerla allineata come
  richiede `VTOR`.

Dettagli in [`03-memory-map.md`](03-memory-map.md).

## 13. Layout — ⚠️ IN RIESAME: A/B oppure esecuzione + staging

L'assetto attuale resta l'A/B di §5 e §12, ma è in valutazione un'alternativa:
**bootloader + una sola area di esecuzione + un'area di staging**, con copia
staging → esecuzione dopo la verifica.

| | A/B (§5, §12) | Esecuzione + staging |
|---|---|---|
| Spazio per l'applicazione | 80 KB | ~100 KB |
| Artefatti per release | due, firmati separatamente | uno |
| Attivazione | istantanea | copia di ~100 KB |
| Rollback alla versione precedente | sì | no |
| Scritture flash per aggiornamento | 1× | 2× |
| Rischio di mattonare | nullo | nullo |

Con l'A/B gli slot scendono a 80 KB perché bootloader e slot A devono stare
insieme nei 128 KB del banco 1. Lo schema con staging non ha quel vincolo, da
cui i ~20 KB in più per l'applicazione.

**⚠️ Attenzione tecnica sullo staging:** sugli STM32 dual‑bank non si può
eseguire codice da un banco mentre lo si cancella o programma. O la routine di
copia gira dalla SRAM, oppure il layout va disposto in modo da scrivere sempre
nell'altro banco (bootloader e staging nel banco 1, esecuzione nel banco 2, al
prezzo di un bootloader da 24 KB). Da confermare sul reference manual del C5.

**Il dato che decide:** in campo, un dispositivo la cui applicazione non parte
resta interrogabile via CAN? Se sì, il rollback vale poco e i 20 KB in più
contano di più. Se il modulo sta dietro un gateway che dialoga solo con
l'applicativo funzionante, il rollback vale molto.

**Verifica in corso da parte del committente.** Fino ad allora resta valido
l'A/B.

## Nota trasversale sui test

Le scelte §1 (CubeIDE) e §3 (cryptolib ST) insieme comportano che il
codice di verifica firma non è eseguibile su PC, quindi non è testabile con i
vettori ufficiali NIST/Wycheproof fuori dal target.

È il punto più delicato di questo assetto: la verifica della firma è il cuore
del secure boot, ed è esattamente il codice che non vuoi che abbia bug.

**Mitigazione prevista:** isolare la crypto dietro un'interfaccia sottile
(`inc/crypto.h`) con due implementazioni — una HW per il target e una software
per i test su PC — e far girare i vettori di test contro quella software, più
un test di coerenza sul target che verifica che le due diano lo stesso
risultato sugli stessi vettori.
