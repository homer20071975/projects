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

## 5. Layout — bootloader + esecuzione + staging

> **Storia della decisione.** Si era partiti con il dual-bank A/B a due slot.
> È stato sostituito dopo aver confermato che **in campo il bootloader resta
> raggiungibile via CAN** anche con l'applicazione in crash: cade così il
> valore principale dell'A/B, il rollback automatico alla versione precedente.

Un bootloader, una sola area di esecuzione, un'area di staging per il download
e la validazione.

**Ripartizione:** bootloader 48 KB, esecuzione 100 KB, staging 100 KB,
metadati 8 KB. Indirizzi in [`03-memory-map.md`](03-memory-map.md).

**Cosa si guadagna rispetto all'A/B:**
- **100 KB per l'applicazione invece di 80.** Con l'A/B bootloader e slot A
  dovevano stare insieme nei 128 KB del banco 1; qui quel vincolo non c'è.
- **Una sola build a indirizzo fisso** invece di due artefatti linkati e
  firmati separatamente. Cade tutta la decisione §12.

**Cosa si perde:**
- **Il rollback alla versione precedente.** La copia sovrascrive il vecchio
  firmware. Se la nuova immagine è firmata correttamente ma va in crash a
  runtime, non c'è niente a cui tornare.
- **Doppia scrittura di flash** per aggiornamento: prima staging, poi copia.

**La rete di sicurezza che sostituisce il rollback:** dopo N tentativi di
avvio non confermati il bootloader smette di provare e **resta in modalità
update in ascolto sul CAN**. Regge perché il bootloader non si autoaggiorna e
resta sempre integro. ⚠️ **Se il presupposto della raggiungibilità in campo
dovesse cambiare, questa decisione va rivista.**

**⚠️ Vincolo tecnico sulla copia:** sugli STM32 dual-bank non si preleva
codice — né si leggono dati — da un banco in corso di programmazione. La
routine di copia gira dalla SRAM e bufferizza in RAM, serializzando lettura e
scrittura. Dettagli in [`03-memory-map.md`](03-memory-map.md).

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

## 12. Indirizzo di esecuzione — ~~due build, una per slot~~ SUPERATA

Decisione **annullata** dalla §5 nella sua forma attuale. Con una sola area di
esecuzione l'applicazione ha un indirizzo fisso — vector table a
`0x0800_C200` — e serve **una sola build**.

Resta valido un solo elemento di quella decisione: il campo `load_address`
nell'header firmato, che il bootloader confronta con l'area attesa. Non serve
più a distinguere due slot, ma continua a impedire che un'immagine costruita
per un layout diverso venga accettata. Vedi [`02-image-format.md`](02-image-format.md).

## 13. Riesame del layout — chiuso

Il riesame è concluso: si passa a esecuzione + staging. La §5 è stata
riscritta di conseguenza, la §12 è decaduta.

**Il dato che ha deciso:** in campo il bootloader resta raggiungibile via CAN
anche quando l'applicazione non parte. Un aggiornamento andato male si risolve
mandandone un altro, quindi il rollback automatico non vale i 20 KB che costa.

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
