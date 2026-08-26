/*
 * can.h — interfaccia verso il controllore CAN.
 *
 * Stessa logica di inc/crypto.h: la parte legata al silicio sta dietro
 * un'interfaccia sottile, così il protocollo che ci gira sopra (ISO-TP, UDS)
 * resta compilabile ed eseguibile su PC.
 */

#ifndef SBL_CAN_H
#define SBL_CAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SBL_CAN_OK        0
#define SBL_CAN_FAIL    (-1)
#define SBL_CAN_BUSY    (-2)   /* code di trasmissione piena, riprovare */

/* Un frame CAN FD porta al massimo 64 byte di dati. */
#define SBL_CAN_MAX_DLEN 64u

typedef struct {
    uint32_t id;                       /* identificatore, 11 o 29 bit */
    uint8_t  len;                      /* byte utili, 0..64           */
    uint8_t  data[SBL_CAN_MAX_DLEN];
} sbl_can_frame_t;

/*
 * Accende la periferica e installa un filtro sull'identificatore atteso.
 * Da chiamare una volta prima di ogni altra funzione.
 */
int sbl_can_init(uint32_t rx_id);

/*
 * Trasmette un frame.
 *
 * Restituisce SBL_CAN_BUSY se non c'è posto nelle code hardware: chi chiama
 * deve riprovare, non considerarlo un errore.
 */
int sbl_can_send(const sbl_can_frame_t *frame);

/*
 * Preleva un frame ricevuto, se ce n'è uno.
 *
 * Restituisce SBL_CAN_OK se `frame` è stato riempito, SBL_CAN_BUSY se non
 * c'era nulla da leggere.
 */
int sbl_can_recv(sbl_can_frame_t *frame);

/* Millisecondi da un'origine arbitraria: serve solo per le differenze. */
uint32_t sbl_can_now_ms(void);

/*
 * Lunghezze di frame ammesse dal CAN FD. Sopra gli 8 byte i valori sono
 * discreti, quindi un payload di 9 byte viaggia in un frame da 12 con tre
 * byte di riempimento.
 */
uint8_t sbl_can_round_dlen(uint8_t len);

#endif /* SBL_CAN_H */
