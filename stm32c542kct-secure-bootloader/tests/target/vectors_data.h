/* Generato da tests/gen_target_vectors.py — non modificare a mano. */

#ifndef SBL_VECTORS_DATA_H
#define SBL_VECTORS_DATA_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char    *name;
    const uint8_t *image;
    size_t         image_len;
    uint32_t       area_base;
    uint32_t       area_size;
    uint32_t       otp_security_version;
    uint32_t       product_id;
    const uint8_t *pubkey;      /* 64 byte */
    const uint8_t *otp_hash;    /* 32 byte */
    int32_t        expected;
} sbl_test_vector_t;

extern const sbl_test_vector_t sbl_test_vectors[];
extern const size_t sbl_test_vector_count;

#endif /* SBL_VECTORS_DATA_H */
