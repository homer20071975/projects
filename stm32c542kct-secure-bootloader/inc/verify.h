/*
 * verify.h — verifica di un'immagine firmware.
 *
 * Specifica: docs/02-image-format.md
 * Riferimento eseguibile: tools/sbl_format.py, funzione verify()
 */

#ifndef SBL_VERIFY_H
#define SBL_VERIFY_H

#include <stddef.h>
#include <stdint.h>

#include "image_header.h"

/*
 * Contesto della verifica: tutto ciò che viene dal dispositivo, non
 * dall'immagine. Passarlo esplicitamente invece di leggerlo da variabili
 * globali rende la funzione testabile su host.
 */
typedef struct {
    uint32_t       area_base;        /* base dell'area verificata          */
    uint32_t       area_size;        /* capienza dell'area                 */
    const uint8_t *root_pubkey;      /* 64 byte, dalla flash del bootloader*/
    const uint8_t *otp_pubkey_hash;  /* 32 byte, dall'OTP                  */
    uint32_t       otp_security_version; /* contatore anti-rollback in OTP */
    uint32_t       product_id;       /* identità del dispositivo           */
} sbl_verify_ctx_t;

/*
 * Esegue i dodici passi della specifica, in ordine, fermandosi al primo che
 * fallisce.
 *
 *   image      inizio dell'area, header compreso
 *   image_len  byte leggibili a partire da `image`
 *
 * Restituisce SBL_OK, oppure il codice del passo fallito.
 */
sbl_verify_result_t sbl_verify_image(const uint8_t *image, size_t image_len,
                                     const sbl_verify_ctx_t *ctx);

#endif /* SBL_VERIFY_H */
