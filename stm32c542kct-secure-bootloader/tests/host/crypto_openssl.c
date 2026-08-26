/*
 * crypto_openssl.c — backend crittografico per i test su host.
 *
 * Implementa inc/crypto.h con OpenSSL. NON fa parte del firmware: sul target
 * le stesse funzioni sono realizzate con X-CUBE-CRYPTOLIB e l'acceleratore
 * HASH.
 *
 * Esiste per far girare src/verify.c — il codice vero del bootloader, non una
 * riscrittura — su PC, e confrontarlo con il riferimento Python sugli stessi
 * vettori.
 */

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/param_build.h>
#include <openssl/sha.h>

#include "crypto.h"

#define P256_COORD_LEN 32

int sbl_sha256(const uint8_t *data, size_t len, uint8_t out[32])
{
    unsigned int outlen = 0u;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    int ok = 0;

    if (ctx == NULL) {
        return SBL_CRYPTO_FAIL;
    }
    ok = EVP_DigestInit_ex(ctx, EVP_sha256(), NULL)
         && EVP_DigestUpdate(ctx, data, len)
         && EVP_DigestFinal_ex(ctx, out, &outlen);
    EVP_MD_CTX_free(ctx);

    return (ok && (outlen == 32u)) ? SBL_CRYPTO_OK : SBL_CRYPTO_FAIL;
}

/* Da r || s alla codifica DER che si aspetta EVP_DigestVerify. */
static int raw_sig_to_der(const uint8_t signature[64],
                          uint8_t **der, int *der_len)
{
    ECDSA_SIG *sig = ECDSA_SIG_new();
    BIGNUM *r = BN_bin2bn(signature, P256_COORD_LEN, NULL);
    BIGNUM *s = BN_bin2bn(signature + P256_COORD_LEN, P256_COORD_LEN, NULL);
    int ok = 0;

    if ((sig != NULL) && (r != NULL) && (s != NULL)
            && ECDSA_SIG_set0(sig, r, s)) {
        /* set0 prende possesso di r e s */
        *der = NULL;
        *der_len = i2d_ECDSA_SIG(sig, der);
        ok = (*der_len > 0);
    } else {
        BN_free(r);
        BN_free(s);
    }
    ECDSA_SIG_free(sig);
    return ok;
}

static EVP_PKEY *pubkey_from_raw(const uint8_t pubkey[64])
{
    uint8_t point[1 + 2 * P256_COORD_LEN];
    OSSL_PARAM_BLD *bld = OSSL_PARAM_BLD_new();
    OSSL_PARAM *params = NULL;
    EVP_PKEY_CTX *ctx = NULL;
    EVP_PKEY *key = NULL;

    point[0] = 0x04u; /* coordinate non compresse */
    memcpy(point + 1, pubkey, 2u * P256_COORD_LEN);

    if (bld == NULL) {
        return NULL;
    }
    if (OSSL_PARAM_BLD_push_utf8_string(bld, "group", "prime256v1", 0)
            && OSSL_PARAM_BLD_push_octet_string(bld, "pub", point,
                                                sizeof(point))) {
        params = OSSL_PARAM_BLD_to_param(bld);
    }
    OSSL_PARAM_BLD_free(bld);

    if (params != NULL) {
        ctx = EVP_PKEY_CTX_new_from_name(NULL, "EC", NULL);
        if ((ctx != NULL) && (EVP_PKEY_fromdata_init(ctx) > 0)) {
            if (EVP_PKEY_fromdata(ctx, &key, EVP_PKEY_PUBLIC_KEY, params) <= 0) {
                key = NULL;
            }
        }
        EVP_PKEY_CTX_free(ctx);
        OSSL_PARAM_free(params);
    }
    return key;
}

bool sbl_ecdsa_p256_verify(const uint8_t pubkey[64],
                           const uint8_t *msg, size_t len,
                           const uint8_t signature[64])
{
    uint8_t *der = NULL;
    int der_len = 0;
    EVP_PKEY *key = NULL;
    EVP_MD_CTX *ctx = NULL;
    bool ok = false;

    if (!raw_sig_to_der(signature, &der, &der_len)) {
        return false;
    }

    key = pubkey_from_raw(pubkey);
    if (key != NULL) {
        ctx = EVP_MD_CTX_new();
        if ((ctx != NULL)
                && (EVP_DigestVerifyInit(ctx, NULL, EVP_sha256(), NULL, key) == 1)) {
            ok = (EVP_DigestVerify(ctx, der, (size_t)der_len, msg, len) == 1);
        }
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(key);
    }
    OPENSSL_free(der);
    return ok;
}
