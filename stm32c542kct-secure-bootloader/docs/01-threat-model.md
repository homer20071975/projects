# Threat model

A cosa serve questo documento: elencare cosa stiamo proteggendo, da chi, e
dire onestamente **cosa questo bootloader non protegge**. I rischi accettati
in fondo sono importanti quanto le mitigazioni.

Riferimenti alle decisioni: [`00-decisions.md`](00-decisions.md).

---

## Beni da proteggere

| | Bene | Dove sta | Perché conta |
|---|---|---|---|
| **A1** | Chiave privata di firma | Fuori dal dispositivo, file offline (§9) | Chi ce l'ha può firmare firmware che il dispositivo accetterà |
| **A2** | Chiave pubblica di root | Flash del bootloader, hash in OTP (§8) | Sostituirla significa scegliere chi firma |
| **A3** | Contatore anti‑rollback | OTP (§11) | Azzerarlo riapre le vulnerabilità già corrette |
| **A4** | Codice del bootloader | Flash `0x0800_0000` | È la radice di fiducia: se cade, cade tutto |
| **A5** | Firmware applicativo | Area di esecuzione | Proprietà intellettuale |
| **A6** | Descrittore di stato | Regione metadati | Determina cosa viene eseguito |

---

## Attaccanti considerati

| | Chi | Cosa può fare |
|---|---|---|
| **T1** | Accesso al bus CAN | Parlare UDS col dispositivo, ascoltare gli aggiornamenti in transito |
| **T2** | Accesso fisico al pezzo | SWD, sonde, dissaldatura, glitch su alimentazione e clock |
| **T3** | Insider o supply chain | Arrivare al file della chiave privata, o alla catena di build |
| **T4** | Accesso al pacchetto di update | Il file distribuito, senza accesso al dispositivo |

Fuori perimetro: attacchi che richiedono decapsulazione del chip e analisi
invasiva a livello di silicio.

---

## Minacce e mitigazioni

### M1 — Esecuzione di firmware non autorizzato
*T1, T4.* Un attaccante manda un'immagine propria e il dispositivo la esegue.

**Mitigato.** Firma ECDSA P‑256 verificata a ogni boot prima del salto (§2).
Senza la chiave privata non si produce una firma valida.

### M2 — Rollback a una versione vulnerabile
*T1.* Reinstallare una vecchia versione firmata, con vulnerabilità note.

**Mitigato.** Contatore monotono in OTP (§11): il bootloader rifiuta immagini
con `security_version` inferiore. In OTP non si torna indietro.

### M3 — Sostituzione della chiave pubblica di root
*T2.* Riscrivere A2 in flash per far accettare firme proprie.

**Mitigato.** L'hash SHA‑256 della chiave sta in OTP e viene verificato a ogni
boot (§8). Una chiave sostituita non supera il confronto.

### M4 — Modifica del bootloader
*T2.* Riscrivere A4 per disattivare la verifica.

**Parzialmente mitigato.** WRP sull'area del bootloader e HDP sulla parte
sensibile. ⚠️ **Dipende interamente dalla corretta configurazione in
produzione**: un dispositivo uscito di fabbrica senza protezioni è
indifendibile. Va inserito nel collaudo di fine linea.

### M5 — Lettura del firmware via debug
*T2.* Collegarsi in SWD e dumpare la flash.

**Da configurare.** RDP a livello adeguato in produzione. ⚠️ **Non ancora
deciso quale livello**: il Level 2 è irreversibile e impedisce ogni analisi di
guasto sui resi, il Level 1 lascia una via di rientro cancellando la flash.
Da decidere prima della produzione.

### M6 — Aggiornamento non autorizzato via UDS
*T1.* Chiunque sul bus avvia una sessione di download.

**⚠️ Non ancora deciso.** Il servizio `0x27` SecurityAccess è lo strumento
previsto, ma non è stato scelto se usarlo né con quale schema. Da notare che
un seed/key debole è peggio di niente, perché dà falsa sicurezza. Il TRNG è
disponibile per generare il challenge (§3).

### M7 — Interruzione di alimentazione durante la scrittura
*T1, o semplice sfortuna.* Il dispositivo resta con flash a metà.

**Mitigato per design.** Il bootloader non si autoaggiorna, quindi resta sempre
integro e il dispositivo è sempre recuperabile via CAN. Il descrittore A6 va
scritto in doppia copia con contatore di sequenza e CRC, così che ne resti
sempre una valida.

### M8 — Immagine scritta nello slot sbagliato
*T1, o errore procedurale.* Solo nello schema A/B a due build (§12).

**Mitigato.** L'indirizzo di destinazione è dentro l'area coperta dalla firma:
il bootloader rifiuta un'immagine destinata all'altro slot.

### M9 — Applicazione compromessa che attacca il bootloader
*T1 via un bug nell'applicativo.* Codice che gira come applicazione e prova a
riscrivere la flash del bootloader o a leggere le chiavi.

**Parzialmente mitigato.** WRP impedisce la scrittura. Ma senza TrustZone
(§7) **non c'è isolamento a runtime**: l'applicazione può leggere l'intera
flash, chiave pubblica compresa. Non è un disastro (la chiave è pubblica), ma
non c'è nessuna barriera architetturale.

### M10 — Fault injection al momento del salto
*T2.* Un glitch che salta il controllo del risultato della verifica.

**Non mitigato in questa fase.** Attenuabile con doppia verifica del risultato,
valori di ritorno non booleani e ritardi casuali. Da valutare in base a quanto
vale il bene protetto: sono contromisure che costano codice e complessità.

### M11 — Usura della flash per aggiornamenti ripetuti
*T1.* Un attaccante sul bus lancia aggiornamenti in continuazione.

**Da mitigare.** Limite ai tentativi consecutivi, più il SecurityAccess di M6.
Rilevante soprattutto se si sceglie il layout con copia (2× scritture per
aggiornamento).

---

## Rischi accettati

Sono conseguenze dirette di decisioni prese consapevolmente. Vanno rilette
prima della produzione, perché è lì che il costo cambia.

### R1 — Il firmware è leggibile in transito
L'immagine viaggia in chiaro sul CAN (§4). Chi ascolta il bus durante un
aggiornamento ottiene il firmware completo e può farne reverse engineering.

*Se un domani conta:* aggiungere AES‑256‑GCM, per cui l'acceleratore AES c'è
già. Richiede un campo nell'header e una chiave di decifratura sul dispositivo.

### R2 — Una chiave privata compromessa non è recuperabile
Chiave singola senza rotazione (§10), custodita in un file offline (§9). Se
esce, ogni dispositivo in campo accetterà per sempre firmware dell'attaccante.
Non esiste procedura di recupero.

*È il rischio più grave del progetto.* La mitigazione non è tecnica ma
procedurale: dove sta il file, chi può accedervi, come si firma. Da
riconsiderare — insieme a HSM e slot di revoca — prima della produzione di
volume.

### R3 — Nessun isolamento a runtime
Senza TrustZone (§7), un'applicazione compromessa vede tutta la memoria. La
protezione del bootloader si regge solo su WRP e HDP.

### R4 — La sicurezza dipende dal collaudo di fine linea
RDP, WRP, HDP e la scrittura dell'OTP sono passi di produzione. Un dispositivo
che esce senza è completamente indifeso, e non c'è modo di accorgersene dal
comportamento normale.

*Da tradurre in un requisito verificabile del collaudo,* non in una nota nella
documentazione.

---

## Decisioni che questo documento lascia aperte

- [ ] Livello di RDP in produzione (M5)
- [ ] Usare `0x27` SecurityAccess, e con quale schema (M6)
- [ ] Contromisure ai fault injection: quali, se ne servono (M10)
- [ ] Limite ai tentativi di aggiornamento consecutivi (M11)
