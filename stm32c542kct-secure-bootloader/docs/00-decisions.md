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

## 3. Crypto — acceleratori hardware (PKA / AES / HASH / RNG)

Verifica più rapida al boot e meno flash occupata dal bootloader.

**⚠️ Da confermare a datasheet:** che il `STM32C542KCT` abbia davvero PKA e
HASH. Se il PKA manca, questa scelta va rivista — il piano B è micro-ecc
(footprint minimo) o Mbed TLS ridotta.

**Conseguenze:**
- Il codice di verifica è legato a questo silicio: niente esecuzione su PC.
- Va comunque isolato dietro un'interfaccia astratta (`crypto_verify()`) per
  poterlo sostituire con un'implementazione software nei test. Vedi §11.

---

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

**⚠️ Da confermare a datasheet:** Flash totale e supporto dual-bank del pezzo.
Servono due slot applicativi completi più il bootloader: se la flash non basta
si ripiega su single slot + staging area.

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
- Il numero di incrementi è finito e va dimensionato ora: quanti
  aggiornamenti di sicurezza prevedi nella vita del prodotto?
- La codifica tipica è unaria (un bit per incremento) perché l'OTP passa solo
  da 1 a 0. Con N bit hai N incrementi.
- L'header immagine porta un campo `security_version` confrontato col contatore.

---

## Nota trasversale sui test

Le scelte §1 (CubeIDE) e §3 (crypto in hardware) insieme comportano che il
codice di verifica firma non è eseguibile su PC, quindi non è testabile con i
vettori ufficiali NIST/Wycheproof fuori dal target.

È il punto più delicato di questo assetto: la verifica della firma è il cuore
del secure boot, ed è esattamente il codice che non vuoi che abbia bug.

**Mitigazione prevista:** isolare la crypto dietro un'interfaccia sottile
(`inc/crypto.h`) con due implementazioni — una HW per il target e una software
per i test su PC — e far girare i vettori di test contro quella software, più
un test di coerenza sul target che verifica che le due diano lo stesso
risultato sugli stessi vettori.
