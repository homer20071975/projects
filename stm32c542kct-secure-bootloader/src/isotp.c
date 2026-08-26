/*
 * isotp.c — ISO 15765-2 su CAN / CAN FD.
 *
 * Non tocca l'hardware: parla col bus attraverso i puntatori a funzione di
 * isotp_bus_t, quindi gira identico sul target e sotto i test su host.
 *
 * Tipi di frame (nibble alto del primo byte del PCI):
 *   0  Single Frame       messaggio che entra tutto in un frame
 *   1  First Frame        primo pezzo, seguito dai Consecutive
 *   2  Consecutive Frame  pezzi successivi, con numero di sequenza
 *   3  Flow Control       il ricevente detta il ritmo al mittente
 */

#include <string.h>

#include "isotp.h"

#define PCI_SF 0x00u
#define PCI_FF 0x10u
#define PCI_CF 0x20u
#define PCI_FC 0x30u
#define PCI_MASK 0x30u

#define FC_CTS      0x00u
#define FC_WAIT     0x01u
#define FC_OVERFLOW 0x02u

/* Oltre 4095 byte la lunghezza sta in un campo esteso a 32 bit. */
#define FF_DL_MAX_12BIT 4095u

static uint32_t now(const isotp_link_t *link)
{
    return link->bus.now_ms(link->bus.ctx);
}

static bool elapsed(uint32_t now_ms, uint32_t deadline)
{
    /* Differenza con segno: regge il ribaltamento del contatore. */
    return (int32_t)(now_ms - deadline) >= 0;
}

/*
 * STmin nella codifica della norma: 0x00-0x7F sono millisecondi, 0xF1-0xF9
 * sono da 100 a 900 microsecondi.
 *
 * Qui il tempo si misura in millisecondi, quindi le separazioni
 * sotto il millisecondo diventano 1 ms. È più lento del minimo richiesto, mai
 * più veloce: non viola il vincolo dell'altro capo.
 */
static uint32_t st_min_to_ms(uint8_t st_min)
{
    if (st_min <= 0x7Fu) {
        return st_min;
    }
    if ((st_min >= 0xF1u) && (st_min <= 0xF9u)) {
        return 1u;
    }
    /* Valori riservati: la norma impone di trattarli come il massimo. */
    return 0x7Fu;
}

static isotp_result_t emit(isotp_link_t *link, uint8_t *payload, uint8_t used)
{
    sbl_can_frame_t frame;
    uint8_t len = used;

    if (link->cfg.pad_frames) {
        len = link->cfg.tx_dlen;
    }
    len = sbl_can_round_dlen(len);
    if (len > link->cfg.tx_dlen) {
        len = link->cfg.tx_dlen;
    }

    frame.id = link->cfg.tx_id;
    frame.len = len;
    memcpy(frame.data, payload, used);
    if (len > used) {
        memset(frame.data + used, link->cfg.pad_byte, (size_t)(len - used));
    }

    return (link->bus.send(link->bus.ctx, &frame) == SBL_CAN_OK)
           ? ISOTP_OK : ISOTP_ERR_BUS;
}

static isotp_result_t send_fc(isotp_link_t *link, uint8_t status)
{
    uint8_t payload[3];

    payload[0] = (uint8_t)(PCI_FC | status);
    payload[1] = link->cfg.block_size;
    payload[2] = link->cfg.st_min;
    return emit(link, payload, 3u);
}

isotp_result_t isotp_init(isotp_link_t *link, const isotp_config_t *cfg,
                          const isotp_bus_t *bus,
                          uint8_t *rx_buf, uint32_t rx_buf_len)
{
    if ((link == NULL) || (cfg == NULL) || (bus == NULL)
            || (bus->send == NULL) || (bus->now_ms == NULL)
            || (rx_buf == NULL) || (rx_buf_len == 0u)) {
        return ISOTP_ERR_PARAM;
    }
    if ((cfg->tx_dlen < 8u) || (cfg->tx_dlen > SBL_CAN_MAX_DLEN)) {
        return ISOTP_ERR_PARAM;
    }

    memset(link, 0, sizeof(*link));
    link->cfg = *cfg;
    link->bus = *bus;
    link->rx_buf = rx_buf;
    link->rx_buf_len = rx_buf_len;
    link->tx_state = ISOTP_TX_IDLE;
    link->rx_state = ISOTP_RX_IDLE;
    link->tx_result = ISOTP_IDLE;
    link->rx_result = ISOTP_IDLE;
    return ISOTP_OK;
}

isotp_result_t isotp_send(isotp_link_t *link, const uint8_t *data,
                          uint32_t len)
{
    uint8_t payload[SBL_CAN_MAX_DLEN];
    uint8_t head;

    if ((link == NULL) || (data == NULL) || (len == 0u)) {
        return ISOTP_ERR_PARAM;
    }
    if (link->tx_state != ISOTP_TX_IDLE) {
        return ISOTP_ERR_PROTOCOL;
    }

    /* Single Frame classico: lunghezza nel nibble basso del primo byte. */
    if (len <= (uint32_t)(link->cfg.tx_dlen - 1u) && (len <= 7u)) {
        payload[0] = (uint8_t)(PCI_SF | (uint8_t)len);
        memcpy(payload + 1, data, len);
        link->tx_result = ISOTP_DONE;
        return emit(link, payload, (uint8_t)(1u + len));
    }

    /*
     * Single Frame esteso del CAN FD: primo byte a zero, lunghezza vera nel
     * secondo. Serve per i messaggi da 8 a tx_dlen-2 byte, che non entrano
     * nel nibble ma stanno comunque in un frame solo.
     */
    if (len <= (uint32_t)(link->cfg.tx_dlen - 2u)) {
        payload[0] = PCI_SF;
        payload[1] = (uint8_t)len;
        memcpy(payload + 2, data, len);
        link->tx_result = ISOTP_DONE;
        return emit(link, payload, (uint8_t)(2u + len));
    }

    /* First Frame, poi si aspetta il Flow Control. */
    if (len <= FF_DL_MAX_12BIT) {
        payload[0] = (uint8_t)(PCI_FF | (uint8_t)((len >> 8) & 0x0Fu));
        payload[1] = (uint8_t)(len & 0xFFu);
        head = 2u;
    } else {
        payload[0] = PCI_FF;
        payload[1] = 0x00u;
        payload[2] = (uint8_t)((len >> 24) & 0xFFu);
        payload[3] = (uint8_t)((len >> 16) & 0xFFu);
        payload[4] = (uint8_t)((len >> 8) & 0xFFu);
        payload[5] = (uint8_t)(len & 0xFFu);
        head = 6u;
    }

    {
        uint8_t chunk = (uint8_t)(link->cfg.tx_dlen - head);
        memcpy(payload + head, data, chunk);

        link->tx_data = data;
        link->tx_len = len;
        link->tx_sent = chunk;
        link->tx_sn = 1u;
        link->tx_state = ISOTP_TX_WAIT_FC;
        link->tx_result = ISOTP_IN_PROGRESS;
        link->tx_deadline = now(link) + ISOTP_N_BS_MS;

        return emit(link, payload, (uint8_t)(head + chunk));
    }
}

static isotp_result_t rx_begin(isotp_link_t *link, uint32_t total)
{
    if (total > link->rx_buf_len) {
        link->rx_state = ISOTP_RX_IDLE;
        link->rx_result = ISOTP_ERR_OVERFLOW;
        (void)send_fc(link, FC_OVERFLOW);
        return ISOTP_ERR_OVERFLOW;
    }
    link->rx_expected = total;
    link->rx_received = 0u;
    link->rx_sn = 1u;
    link->rx_block_left = link->cfg.block_size;
    link->rx_state = ISOTP_RX_RECEIVING;
    link->rx_result = ISOTP_IN_PROGRESS;
    return ISOTP_OK;
}

static isotp_result_t on_single(isotp_link_t *link, const uint8_t *d,
                                uint8_t len)
{
    uint32_t n = d[0] & 0x0Fu;
    uint8_t head = 1u;

    if (n == 0u) {
        /* Forma estesa del CAN FD. */
        if (len < 2u) {
            return ISOTP_ERR_PROTOCOL;
        }
        n = d[1];
        head = 2u;
        if (n == 0u) {
            return ISOTP_ERR_PROTOCOL;
        }
    }
    if ((uint32_t)head + n > (uint32_t)len) {
        return ISOTP_ERR_PROTOCOL;
    }
    if (n > link->rx_buf_len) {
        link->rx_result = ISOTP_ERR_OVERFLOW;
        return ISOTP_ERR_OVERFLOW;
    }

    memcpy(link->rx_buf, d + head, n);
    link->rx_received = n;
    link->rx_expected = n;
    link->rx_state = ISOTP_RX_IDLE;
    link->rx_result = ISOTP_DONE;
    return ISOTP_DONE;
}

static isotp_result_t on_first(isotp_link_t *link, const uint8_t *d,
                               uint8_t len)
{
    uint32_t total;
    uint8_t head;

    if (len < 8u) {
        /* Un First Frame più corto di 8 byte non ha senso. */
        return ISOTP_ERR_PROTOCOL;
    }

    total = (uint32_t)((d[0] & 0x0Fu) << 8) | d[1];
    if (total == 0u) {
        total = ((uint32_t)d[2] << 24) | ((uint32_t)d[3] << 16)
                | ((uint32_t)d[4] << 8) | (uint32_t)d[5];
        head = 6u;
        if (total <= FF_DL_MAX_12BIT) {
            /* La forma estesa è ammessa solo oltre i 4095 byte. */
            return ISOTP_ERR_PROTOCOL;
        }
    } else {
        head = 2u;
    }

    if (rx_begin(link, total) != ISOTP_OK) {
        return ISOTP_ERR_OVERFLOW;
    }

    {
        uint32_t chunk = (uint32_t)len - head;
        if (chunk > total) {
            chunk = total;
        }
        memcpy(link->rx_buf, d + head, chunk);
        link->rx_received = chunk;
    }

    link->rx_deadline = now(link) + ISOTP_N_CR_MS;
    return send_fc(link, FC_CTS) == ISOTP_OK ? ISOTP_IN_PROGRESS
                                             : ISOTP_ERR_BUS;
}

static isotp_result_t on_consecutive(isotp_link_t *link, const uint8_t *d,
                                     uint8_t len)
{
    uint32_t remaining;
    uint32_t chunk;

    if (link->rx_state != ISOTP_RX_RECEIVING) {
        /* Un Consecutive senza First: frame spaiato, si ignora. */
        return ISOTP_IDLE;
    }
    if ((d[0] & 0x0Fu) != link->rx_sn) {
        link->rx_state = ISOTP_RX_IDLE;
        link->rx_result = ISOTP_ERR_SEQUENCE;
        return ISOTP_ERR_SEQUENCE;
    }
    if (len < 2u) {
        link->rx_state = ISOTP_RX_IDLE;
        link->rx_result = ISOTP_ERR_PROTOCOL;
        return ISOTP_ERR_PROTOCOL;
    }

    link->rx_sn = (uint8_t)((link->rx_sn + 1u) & 0x0Fu);

    remaining = link->rx_expected - link->rx_received;
    chunk = (uint32_t)len - 1u;
    if (chunk > remaining) {
        chunk = remaining;
    }
    memcpy(link->rx_buf + link->rx_received, d + 1, chunk);
    link->rx_received += chunk;

    if (link->rx_received >= link->rx_expected) {
        link->rx_state = ISOTP_RX_IDLE;
        link->rx_result = ISOTP_DONE;
        return ISOTP_DONE;
    }

    link->rx_deadline = now(link) + ISOTP_N_CR_MS;

    /* Fine del blocco: si autorizza il mittente a proseguire. */
    if (link->cfg.block_size != 0u) {
        link->rx_block_left--;
        if (link->rx_block_left == 0u) {
            link->rx_block_left = link->cfg.block_size;
            return send_fc(link, FC_CTS) == ISOTP_OK ? ISOTP_IN_PROGRESS
                                                     : ISOTP_ERR_BUS;
        }
    }
    return ISOTP_IN_PROGRESS;
}

static isotp_result_t on_flow_control(isotp_link_t *link, const uint8_t *d,
                                      uint8_t len)
{
    uint8_t status;

    if (link->tx_state == ISOTP_TX_IDLE) {
        return ISOTP_IDLE;
    }
    if (len < 3u) {
        link->tx_state = ISOTP_TX_IDLE;
        link->tx_result = ISOTP_ERR_PROTOCOL;
        return ISOTP_ERR_PROTOCOL;
    }

    status = d[0] & 0x0Fu;
    if (status == FC_OVERFLOW) {
        /* Il ricevente non ha spazio: inutile insistere. */
        link->tx_state = ISOTP_TX_IDLE;
        link->tx_result = ISOTP_ERR_ABORTED;
        return ISOTP_ERR_ABORTED;
    }
    if (status == FC_WAIT) {
        /* Si resta in attesa, ma il timeout riparte da capo. */
        link->tx_deadline = now(link) + ISOTP_N_BS_MS;
        return ISOTP_IN_PROGRESS;
    }
    if (status != FC_CTS) {
        link->tx_state = ISOTP_TX_IDLE;
        link->tx_result = ISOTP_ERR_PROTOCOL;
        return ISOTP_ERR_PROTOCOL;
    }

    link->tx_block_left = d[1];
    link->tx_st_min = d[2];
    link->tx_state = ISOTP_TX_SENDING;
    link->tx_next_frame = now(link);
    return ISOTP_IN_PROGRESS;
}

isotp_result_t isotp_on_frame(isotp_link_t *link,
                              const sbl_can_frame_t *frame)
{
    if ((link == NULL) || (frame == NULL)) {
        return ISOTP_ERR_PARAM;
    }
    if (frame->id != link->cfg.rx_id) {
        return ISOTP_IDLE;   /* non è per noi */
    }
    if (frame->len < 1u) {
        return ISOTP_ERR_PROTOCOL;
    }

    switch (frame->data[0] & PCI_MASK) {
    case PCI_SF: return on_single(link, frame->data, frame->len);
    case PCI_FF: return on_first(link, frame->data, frame->len);
    case PCI_CF: return on_consecutive(link, frame->data, frame->len);
    case PCI_FC: return on_flow_control(link, frame->data, frame->len);
    default:     return ISOTP_ERR_PROTOCOL;
    }
}

isotp_result_t isotp_poll(isotp_link_t *link)
{
    uint32_t t;

    if (link == NULL) {
        return ISOTP_ERR_PARAM;
    }
    t = now(link);

    /* Timeout in ricezione: il mittente ha smesso di parlare a metà. */
    if ((link->rx_state == ISOTP_RX_RECEIVING) && elapsed(t, link->rx_deadline)) {
        link->rx_state = ISOTP_RX_IDLE;
        link->rx_result = ISOTP_ERR_TIMEOUT;
        return ISOTP_ERR_TIMEOUT;
    }

    /* Timeout in attesa del Flow Control. */
    if ((link->tx_state == ISOTP_TX_WAIT_FC) && elapsed(t, link->tx_deadline)) {
        link->tx_state = ISOTP_TX_IDLE;
        link->tx_result = ISOTP_ERR_TIMEOUT;
        return ISOTP_ERR_TIMEOUT;
    }

    if (link->tx_state != ISOTP_TX_SENDING) {
        return (link->tx_state == ISOTP_TX_IDLE) ? ISOTP_IDLE
                                                 : ISOTP_IN_PROGRESS;
    }

    /* Rispetto della separazione minima imposta dal ricevente. */
    if (!elapsed(t, link->tx_next_frame)) {
        return ISOTP_IN_PROGRESS;
    }

    {
        uint8_t payload[SBL_CAN_MAX_DLEN];
        uint32_t remaining = link->tx_len - link->tx_sent;
        uint32_t chunk = (uint32_t)link->cfg.tx_dlen - 1u;
        isotp_result_t r;

        if (chunk > remaining) {
            chunk = remaining;
        }
        payload[0] = (uint8_t)(PCI_CF | link->tx_sn);
        memcpy(payload + 1, link->tx_data + link->tx_sent, chunk);

        r = emit(link, payload, (uint8_t)(1u + chunk));
        if (r != ISOTP_OK) {
            /* Code piene: si riproverà al giro dopo, senza perdere stato. */
            return ISOTP_IN_PROGRESS;
        }

        link->tx_sent += chunk;
        link->tx_sn = (uint8_t)((link->tx_sn + 1u) & 0x0Fu);
        link->tx_next_frame = t + st_min_to_ms(link->tx_st_min);

        if (link->tx_sent >= link->tx_len) {
            link->tx_state = ISOTP_TX_IDLE;
            link->tx_result = ISOTP_DONE;
            return ISOTP_DONE;
        }

        /* Blocco esaurito: si torna ad aspettare il Flow Control. */
        if (link->tx_block_left != 0u) {
            link->tx_block_left--;
            if (link->tx_block_left == 0u) {
                link->tx_state = ISOTP_TX_WAIT_FC;
                link->tx_deadline = t + ISOTP_N_BS_MS;
            }
        }
        return ISOTP_IN_PROGRESS;
    }
}

isotp_result_t isotp_take(isotp_link_t *link, const uint8_t **data,
                          uint32_t *len)
{
    if ((link == NULL) || (data == NULL) || (len == NULL)) {
        return ISOTP_ERR_PARAM;
    }
    if (link->rx_result != ISOTP_DONE) {
        return (link->rx_result == ISOTP_IN_PROGRESS) ? ISOTP_IN_PROGRESS
                                                      : link->rx_result;
    }
    *data = link->rx_buf;
    *len = link->rx_received;
    link->rx_result = ISOTP_IDLE;
    return ISOTP_DONE;
}

isotp_tx_state_t isotp_tx_state(const isotp_link_t *link)
{
    return link->tx_state;
}

isotp_result_t isotp_tx_result(const isotp_link_t *link)
{
    return link->tx_result;
}
