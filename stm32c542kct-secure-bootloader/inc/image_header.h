/*
 * image_header.h — formato dell'immagine firmware firmata.
 *
 * Specifica: docs/02-image-format.md
 *
 * Un'immagine è un header di 512 byte seguito dal payload, cioè il binario
 * dell'applicazione a partire dalla sua vector table.
 *
 * La firma ECDSA P-256 copre i primi 448 byte dell'header e sta fuori da
 * essi, in coda. Il payload è legato alla firma tramite payload_sha256, che
 * si trova dentro la regione firmata.
 */

#ifndef SBL_IMAGE_HEADER_H
#define SBL_IMAGE_HEADER_H

#include <stdint.h>
#include <stddef.h>

/* 'S' 'B' 'L' '1' in little-endian */
#define SBL_MAGIC               0x314C4253u

#define SBL_HEADER_VERSION      1u
#define SBL_HEADER_SIZE         512u
#define SBL_SIGNED_LEN          448u   /* regione coperta dalla firma */

#define SBL_SHA256_LEN          32u
#define SBL_SIGNATURE_LEN       64u    /* r || s, 32 byte ciascuno */
#define SBL_PUBKEY_LEN          64u    /* X || Y, coordinate non compresse */
#define SBL_RESERVED_LEN        384u

typedef struct {
    uint32_t magic;             /* 0x000  SBL_MAGIC                        */
    uint16_t header_version;    /* 0x004  versione del formato             */
    uint16_t header_size;       /* 0x006  SBL_HEADER_SIZE                  */
    uint32_t image_size;        /* 0x008  byte di payload, header escluso  */
    uint32_t load_address;      /* 0x00C  dove il payload deve girare      */
    uint32_t entry_vtor;        /* 0x010  vector table dell'applicazione   */
    uint32_t security_version;  /* 0x014  anti-rollback, monotono          */
    uint32_t firmware_version;  /* 0x018  informativa: MM.mm.pp.bb         */
    uint32_t product_id;        /* 0x01C  identifica il prodotto           */
    uint8_t  payload_sha256[SBL_SHA256_LEN];   /* 0x020                    */
    uint8_t  reserved[SBL_RESERVED_LEN];       /* 0x040  a 0xFF            */
    uint8_t  signature[SBL_SIGNATURE_LEN];     /* 0x1C0  fuori dalla firma */
} sbl_image_header_t;

/*
 * Queste asserzioni non sono decorative. Un padding inatteso del compilatore
 * romperebbe silenziosamente la compatibilità fra il tool di firma e il
 * bootloader: le immagini verrebbero firmate su un layout e verificate su un
 * altro, e il guasto si manifesterebbe solo a runtime sul target.
 */
_Static_assert(sizeof(sbl_image_header_t) == SBL_HEADER_SIZE,
               "l'header deve essere esattamente 512 byte");
_Static_assert(offsetof(sbl_image_header_t, payload_sha256) == 0x020u,
               "payload_sha256 fuori posto");
_Static_assert(offsetof(sbl_image_header_t, reserved) == 0x040u,
               "reserved fuori posto");
_Static_assert(offsetof(sbl_image_header_t, signature) == SBL_SIGNED_LEN,
               "la firma deve iniziare dove finisce la regione firmata");

/*
 * Esiti della verifica.
 *
 * SBL_OK non vale zero di proposito. Un singolo glitch che azzera un registro
 * o salta un'istruzione produce con ogni probabilità 0, non questo valore:
 * confrontare contro una costante improbabile alza il costo di un attacco a
 * fault injection senza costare nulla. È una mitigazione parziale della M10
 * del threat model, non una difesa completa.
 *
 * Gli altri valori seguono la numerazione dei passi di verifica in
 * docs/02-image-format.md, per poter risalire dal codice di errore al punto
 * esatto in cui l'immagine è stata rifiutata.
 */
typedef enum {
    SBL_OK                   = 0x5A3CC3A5,

    SBL_ERR_MAGIC            = 1,  /* magic assente o corrotto             */
    SBL_ERR_HEADER_VERSION   = 2,  /* versione del formato non supportata  */
    SBL_ERR_HEADER_SIZE      = 3,  /* header_size != 512                   */
    SBL_ERR_IMAGE_SIZE       = 4,  /* payload nullo o oltre la capienza    */
    SBL_ERR_LOAD_ADDRESS     = 5,  /* non è l'area attesa                  */
    SBL_ERR_ENTRY_VTOR       = 6,  /* fuori area o non allineato a 512     */
    SBL_ERR_ROOT_KEY         = 7,  /* chiave in flash != hash in OTP       */
    SBL_ERR_SIGNATURE        = 8,  /* firma ECDSA non valida               */
    SBL_ERR_SECURITY_VERSION = 9,  /* rollback a una versione precedente   */
    SBL_ERR_PRODUCT_ID       = 10, /* immagine di un altro prodotto        */
    SBL_ERR_PAYLOAD_HASH     = 11  /* payload alterato                     */
} sbl_verify_result_t;

#endif /* SBL_IMAGE_HEADER_H */
