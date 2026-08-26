/*
 * isotp.h — trasporto ISO 15765-2 (ISO-TP) su CAN / CAN FD.
 *
 * Segmenta messaggi più lunghi di un frame e li riassembla dall'altra parte.
 * È lo strato su cui poggia UDS (§6 di docs/00-decisions.md).
 *
 * Sulla dimensione dei messaggi: l'immagine da 100 KB **non** viaggia in un
 * unico messaggio ISO-TP. UDS la spezza in più richieste TransferData (0x36),
 * ciascuna delle quali è un messaggio ISO-TP di pochi kilobyte. Il buffer di
 * ricezione va dimensionato su quello, non sull'immagine intera — che tra
 * l'altro non entrerebbe nei 64 KB di SRAM.
 *
 * L'implementazione non tocca l'hardware: parla con inc/can.h attraverso
 * puntatori a funzione, e per questo gira anche su PC.
 */

#ifndef SBL_ISOTP_H
#define SBL_ISOTP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "can.h"

typedef enum {
    ISOTP_OK = 0,
    ISOTP_IDLE,             /* niente in corso                            */
    ISOTP_IN_PROGRESS,      /* trasferimento avviato                      */
    ISOTP_DONE,             /* messaggio completo disponibile             */
    ISOTP_ERR_TIMEOUT,      /* N_Bs o N_Cr scaduto                        */
    ISOTP_ERR_SEQUENCE,     /* numero di sequenza fuori ordine            */
    ISOTP_ERR_OVERFLOW,     /* messaggio più lungo del buffer             */
    ISOTP_ERR_PROTOCOL,     /* frame malformato o inatteso                */
    ISOTP_ERR_ABORTED,      /* l'altro capo ha risposto FC con Overflow   */
    ISOTP_ERR_BUS,          /* il livello CAN ha fallito                  */
    ISOTP_ERR_PARAM         /* argomenti non validi                       */
} isotp_result_t;

/* Timeout della norma, in millisecondi. */
#define ISOTP_N_BS_MS   1000u   /* attesa del Flow Control                */
#define ISOTP_N_CR_MS   1000u   /* attesa del Consecutive Frame           */

typedef struct {
    uint32_t tx_id;         /* identificatore usato in trasmissione       */
    uint32_t rx_id;         /* identificatore atteso in ricezione         */
    uint8_t  tx_dlen;       /* byte per frame in trasmissione, 8..64      */
    uint8_t  block_size;    /* BS annunciato: 0 = nessun blocco           */
    uint8_t  st_min;        /* STmin annunciato, codifica della norma     */
    uint8_t  pad_byte;      /* riempimento dei frame corti                */
    bool     pad_frames;    /* riempire sempre fino alla lunghezza piena  */
} isotp_config_t;

/* Callback verso il livello CAN. Iniettate per poter testare su host. */
typedef struct {
    int      (*send)(void *ctx, const sbl_can_frame_t *frame);
    uint32_t (*now_ms)(void *ctx);
    void      *ctx;
} isotp_bus_t;

typedef enum {
    ISOTP_TX_IDLE = 0,
    ISOTP_TX_WAIT_FC,
    ISOTP_TX_SENDING
} isotp_tx_state_t;

typedef enum {
    ISOTP_RX_IDLE = 0,
    ISOTP_RX_RECEIVING
} isotp_rx_state_t;

typedef struct {
    isotp_config_t   cfg;
    isotp_bus_t      bus;

    /* trasmissione */
    isotp_tx_state_t tx_state;
    const uint8_t   *tx_data;
    uint32_t         tx_len;
    uint32_t         tx_sent;
    uint8_t          tx_sn;
    uint8_t          tx_block_left;   /* frame rimasti nel blocco corrente */
    uint8_t          tx_st_min;       /* STmin imposto dall'altro capo     */
    uint32_t         tx_deadline;
    uint32_t         tx_next_frame;
    isotp_result_t   tx_result;

    /* ricezione */
    isotp_rx_state_t rx_state;
    uint8_t         *rx_buf;
    uint32_t         rx_buf_len;
    uint32_t         rx_expected;
    uint32_t         rx_received;
    uint8_t          rx_sn;
    uint8_t          rx_block_left;
    uint32_t         rx_deadline;
    isotp_result_t   rx_result;
} isotp_link_t;

/*
 * Prepara un collegamento. `rx_buf` accoglie i messaggi in arrivo e deve
 * restare valido per tutta la vita del collegamento.
 */
isotp_result_t isotp_init(isotp_link_t *link, const isotp_config_t *cfg,
                          const isotp_bus_t *bus,
                          uint8_t *rx_buf, uint32_t rx_buf_len);

/*
 * Avvia l'invio di un messaggio. `data` deve restare valido finché
 * isotp_tx_state() non torna a ISOTP_TX_IDLE.
 *
 * Un messaggio che entra in un solo frame parte subito; gli altri richiedono
 * chiamate successive a isotp_poll().
 */
isotp_result_t isotp_send(isotp_link_t *link, const uint8_t *data,
                          uint32_t len);

/* Consegna al protocollo un frame arrivato dal bus. */
isotp_result_t isotp_on_frame(isotp_link_t *link,
                              const sbl_can_frame_t *frame);

/*
 * Fa avanzare il collegamento: trasmette i frame dovuti e verifica i timeout.
 * Va chiamata di frequente, anche quando non c'è nulla da fare.
 */
isotp_result_t isotp_poll(isotp_link_t *link);

/*
 * Se è arrivato un messaggio completo, ne restituisce lunghezza e puntatore e
 * riarma la ricezione. Altrimenti restituisce ISOTP_IDLE.
 */
isotp_result_t isotp_take(isotp_link_t *link, const uint8_t **data,
                          uint32_t *len);

isotp_tx_state_t isotp_tx_state(const isotp_link_t *link);
isotp_result_t   isotp_tx_result(const isotp_link_t *link);

#endif /* SBL_ISOTP_H */
