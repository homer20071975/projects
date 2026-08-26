/*
 * can_stm32.c — driver FDCAN per il target.
 *
 * Realizza inc/can.h con la periferica FDCAN del C542.
 *
 * ============================================================================
 * ⚠️  QUESTO FILE NON È MAI STATO COMPILATO NÉ ESEGUITO.
 *
 * Scritto senza accesso all'HAL del C5 né a un target reale. Nomi delle API,
 * costanti e soprattutto i **parametri di temporizzazione del bus** sono
 * segnaposto, non valori validati.
 *
 * I punti marcati "DA VERIFICARE" vanno controllati contro il reference
 * manual del C5, capitolo FDCAN, e contro l'HAL della serie.
 *
 * ⚠️ Prima ancora: **da confermare che il package a 32 pin del
 * STM32C542KCT esponga un FDCAN.** Il datasheet dice "fino a 2 FDCAN" per la
 * famiglia, ma sui package piccoli il numero può essere ridotto. Se il pezzo
 * scelto non lo espone, salta la decisione §6 e va rivisto il canale di
 * aggiornamento. Vedi docs/04-silicon-facts.md.
 *
 * A differenza di src/isotp.c, che gira sopra questo file ed è collaudato su
 * host, qui non c'è modo di provare nulla senza hardware.
 * ============================================================================
 */

#include <string.h>

#include "can.h"

/* DA VERIFICARE: nome dell'header dell'HAL per la serie C5. */
#include "stm32c5xx_hal.h"

static FDCAN_HandleTypeDef hfdcan;
static bool can_ready = false;

/*
 * Mappa fra byte utili e codice DLC del CAN FD.
 *
 * Il registro non contiene la lunghezza ma un codice: fino a 8 byte i due
 * coincidono, sopra no. Sbagliare questa tabella non dà un errore, dà frame
 * di lunghezza sbagliata — un difetto che si manifesta solo su certi
 * messaggi.
 *
 * DA VERIFICARE: nomi delle costanti FDCAN_DLC_BYTES_* nell'HAL del C5.
 */
static const struct { uint8_t len; uint32_t dlc; } dlc_map[] = {
    {  0u, FDCAN_DLC_BYTES_0  }, {  1u, FDCAN_DLC_BYTES_1  },
    {  2u, FDCAN_DLC_BYTES_2  }, {  3u, FDCAN_DLC_BYTES_3  },
    {  4u, FDCAN_DLC_BYTES_4  }, {  5u, FDCAN_DLC_BYTES_5  },
    {  6u, FDCAN_DLC_BYTES_6  }, {  7u, FDCAN_DLC_BYTES_7  },
    {  8u, FDCAN_DLC_BYTES_8  }, { 12u, FDCAN_DLC_BYTES_12 },
    { 16u, FDCAN_DLC_BYTES_16 }, { 20u, FDCAN_DLC_BYTES_20 },
    { 24u, FDCAN_DLC_BYTES_24 }, { 32u, FDCAN_DLC_BYTES_32 },
    { 48u, FDCAN_DLC_BYTES_48 }, { 64u, FDCAN_DLC_BYTES_64 },
};

#define DLC_MAP_LEN (sizeof(dlc_map) / sizeof(dlc_map[0]))

static uint32_t len_to_dlc(uint8_t len)
{
    for (size_t i = 0u; i < DLC_MAP_LEN; i++) {
        if (dlc_map[i].len == len) {
            return dlc_map[i].dlc;
        }
    }
    return FDCAN_DLC_BYTES_0;
}

static uint8_t dlc_to_len(uint32_t dlc)
{
    for (size_t i = 0u; i < DLC_MAP_LEN; i++) {
        if (dlc_map[i].dlc == dlc) {
            return dlc_map[i].len;
        }
    }
    return 0u;
}

/*
 * DA VERIFICARE: tutti i valori qui sotto.
 *
 * Dipendono dal clock del kernel FDCAN e dalla velocità di bus scelta per il
 * prodotto, che non è ancora stata fissata. I numeri sono un segnaposto per
 * 500 kbit/s nominali e 2 Mbit/s in fase dati, con un clock da 40 MHz.
 *
 * Non vanno indovinati: si calcolano dal clock effettivo, e il punto di
 * campionamento va concordato con gli altri nodi della rete. Un bus con
 * parametri sbagliati non funziona "un po' peggio": non funziona.
 */
#define CAN_NOMINAL_PRESCALER   1u
#define CAN_NOMINAL_SJW         8u
#define CAN_NOMINAL_TSEG1      63u
#define CAN_NOMINAL_TSEG2      16u
#define CAN_DATA_PRESCALER      1u
#define CAN_DATA_SJW            4u
#define CAN_DATA_TSEG1         15u
#define CAN_DATA_TSEG2          4u

int sbl_can_init(uint32_t rx_id)
{
    FDCAN_FilterTypeDef filter;

    hfdcan.Instance = FDCAN1;
    hfdcan.Init.ClockDivider = FDCAN_CLOCK_DIV1;
    hfdcan.Init.FrameFormat = FDCAN_FRAME_FD_BRS;
    hfdcan.Init.Mode = FDCAN_MODE_NORMAL;
    hfdcan.Init.AutoRetransmission = ENABLE;
    hfdcan.Init.TransmitPause = DISABLE;
    hfdcan.Init.ProtocolException = DISABLE;
    hfdcan.Init.NominalPrescaler = CAN_NOMINAL_PRESCALER;
    hfdcan.Init.NominalSyncJumpWidth = CAN_NOMINAL_SJW;
    hfdcan.Init.NominalTimeSeg1 = CAN_NOMINAL_TSEG1;
    hfdcan.Init.NominalTimeSeg2 = CAN_NOMINAL_TSEG2;
    hfdcan.Init.DataPrescaler = CAN_DATA_PRESCALER;
    hfdcan.Init.DataSyncJumpWidth = CAN_DATA_SJW;
    hfdcan.Init.DataTimeSeg1 = CAN_DATA_TSEG1;
    hfdcan.Init.DataTimeSeg2 = CAN_DATA_TSEG2;
    hfdcan.Init.StdFiltersNbr = 1u;
    hfdcan.Init.ExtFiltersNbr = 0u;
    hfdcan.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;

    if (HAL_FDCAN_Init(&hfdcan) != HAL_OK) {
        return SBL_CAN_FAIL;
    }

    /*
     * Un solo filtro, sull'identificatore che ci riguarda. Il bootloader non
     * ha motivo di vedere il resto del traffico di rete, e meno frame
     * arrivano meno lavoro fa il polling.
     */
    filter.IdType = FDCAN_STANDARD_ID;
    filter.FilterIndex = 0u;
    filter.FilterType = FDCAN_FILTER_MASK;
    filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    filter.FilterID1 = rx_id;
    filter.FilterID2 = 0x7FFu;   /* maschera: confronto esatto */

    if (HAL_FDCAN_ConfigFilter(&hfdcan, &filter) != HAL_OK) {
        return SBL_CAN_FAIL;
    }
    if (HAL_FDCAN_ConfigGlobalFilter(&hfdcan, FDCAN_REJECT, FDCAN_REJECT,
                                     FDCAN_REJECT_REMOTE,
                                     FDCAN_REJECT_REMOTE) != HAL_OK) {
        return SBL_CAN_FAIL;
    }
    if (HAL_FDCAN_Start(&hfdcan) != HAL_OK) {
        return SBL_CAN_FAIL;
    }

    can_ready = true;
    return SBL_CAN_OK;
}

int sbl_can_send(const sbl_can_frame_t *frame)
{
    FDCAN_TxHeaderTypeDef header;

    if (!can_ready || (frame == NULL) || (frame->len > SBL_CAN_MAX_DLEN)) {
        return SBL_CAN_FAIL;
    }

    /*
     * Coda piena: non è un errore. Il chiamante — isotp_poll() — riproverà al
     * giro successivo senza perdere lo stato del trasferimento.
     */
    if (HAL_FDCAN_GetTxFifoFreeLevel(&hfdcan) == 0u) {
        return SBL_CAN_BUSY;
    }

    header.Identifier = frame->id;
    header.IdType = FDCAN_STANDARD_ID;
    header.TxFrameType = FDCAN_DATA_FRAME;
    header.DataLength = len_to_dlc(frame->len);
    header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    header.BitRateSwitch = FDCAN_BRS_ON;
    header.FDFormat = FDCAN_FD_CAN;
    header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    header.MessageMarker = 0u;

    if (HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan, &header,
                                      (uint8_t *)(uintptr_t)frame->data)
            != HAL_OK) {
        return SBL_CAN_BUSY;
    }
    return SBL_CAN_OK;
}

int sbl_can_recv(sbl_can_frame_t *frame)
{
    FDCAN_RxHeaderTypeDef header;

    if (!can_ready || (frame == NULL)) {
        return SBL_CAN_FAIL;
    }
    if (HAL_FDCAN_GetRxFifoFillLevel(&hfdcan, FDCAN_RX_FIFO0) == 0u) {
        return SBL_CAN_BUSY;
    }
    if (HAL_FDCAN_GetRxMessage(&hfdcan, FDCAN_RX_FIFO0, &header,
                               frame->data) != HAL_OK) {
        return SBL_CAN_FAIL;
    }

    frame->id = header.Identifier;
    frame->len = dlc_to_len(header.DataLength);
    return SBL_CAN_OK;
}

uint32_t sbl_can_now_ms(void)
{
    return HAL_GetTick();
}
