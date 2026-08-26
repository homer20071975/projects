#!/usr/bin/env python3
"""
Collaudo per mutazione di src/isotp.c.

    python3 tests/mutate_isotp.py     # dalla radice del progetto

Introduce una alla volta delle rotture deliberate nel protocollo e verifica
che i test se ne accorgano. Un test che resta verde su codice rotto non sta
verificando nulla.

Serve a qualcosa: alla prima esecuzione la mutazione "STmin ignorato" non
veniva rilevata, perche' i test si limitavano a controllare che il messaggio
arrivasse — cosa che accade anche se il mittente ignora la separazione minima
imposta dal ricevente. Da li' sono nati test_st_min_rispettato e
test_st_min_zero_non_rallenta, che misurano le distanze fra i frame.

Non e' parte della suite ordinaria: si lancia a mano dopo aver toccato il
protocollo o i suoi test.
"""

import subprocess

MUT = [
 ("link->tx_sn = (uint8_t)((link->tx_sn + 1u) & 0x0Fu);",
  "link->tx_sn = (uint8_t)((link->tx_sn + 2u) & 0x0Fu);", "sequenza salta di due"),
 ("link->tx_next_frame = t + st_min_to_ms(link->tx_st_min);",
  "link->tx_next_frame = t;", "STmin ignorato"),
 ("if (total > link->rx_buf_len) {", "if (0) {", "overflow non controllato"),
 ("if ((d[0] & 0x0Fu) != link->rx_sn) {", "if (0) {", "sequenza non controllata"),
 ("return (int32_t)(now_ms - deadline) >= 0;", "return 0;", "timeout mai scattano"),
 ("if (chunk > remaining) {", "if (0) {", "clamp ultimo blocco rimosso"),
 ("if (status == FC_OVERFLOW) {", "if (0) {", "FC Overflow ignorato"),
 ("payload[0] = (uint8_t)(PCI_FC | status);", "payload[0] = (uint8_t)(PCI_FC);", "stato del FC sempre CTS"),
 ("if (r != ISOTP_OK) {", "if (0) {", "esito dell'invio ignorato"),
 ("link->rx_deadline = now(link) + ISOTP_N_CR_MS;", "link->rx_deadline = now(link);", "N_Cr azzerato"),
]
P='src/isotp.c'
orig=open(P).read()
uncaught=[]
for old,new,name in MUT:
    if old not in orig:
        print("  %-32s -> MUTAZIONE NON APPLICABILE" % name); continue
    open(P,'w').write(orig.replace(old,new,1))
    c=subprocess.run(["gcc","-std=c11","-w","-O1","-Iinc","src/isotp.c","src/can_dlen.c",
                      "tests/host/test_isotp.c","-o","/tmp/mut"],capture_output=True)
    if c.returncode!=0:
        print("  %-32s -> non compila (mutazione scartata)" % name)
    else:
        r=subprocess.run(["/tmp/mut"],capture_output=True,text=True)
        line=r.stdout.strip().splitlines()[-1] if r.stdout.strip() else "?"
        caught = r.returncode!=0
        print("  %-32s -> %s  [%s]" % (name, line, "rilevata" if caught else "NON RILEVATA"))
        if not caught: uncaught.append(name)
open(P,'w').write(orig)
print()
print("mutazioni non rilevate:", uncaught if uncaught else "nessuna")
