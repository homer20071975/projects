/*
 * crypto_stm32.c — backend crittografico per il target.
 *
 * Implementa inc/crypto.h con:
 *   - l'acceleratore HASH per lo SHA-256;
 *   - X-CUBE-CRYPTOLIB (API cmox) per la verifica ECDSA P-256, che sul C542
 *     è necessariamente software perché il pezzo non ha il PKA.
 *
 * ============================================================================
 * ⚠️  QUESTO FILE NON È MAI STATO COMPILATO NÉ ESEGUITO.
 *
 * È stato scritto senza accesso agli header di X-CUBE-CRYPTOLIB, all'HAL del
 * C5 e a un target reale. I nomi delle API, le costanti e la dimensione del
 * buffer di lavoro sono assunzioni ragionate, non verificate.
 *
 * Prima di fidarsene vanno controllati, uno per uno, i punti marcati
 * "DA VERIFICARE" qui sotto, contro:
 *   - UM2724, manuale utente di X-CUBE-CRYPTOLIB;
 *   - il reference manual del C5, capitolo HASH;
 *   - le release notes della cryptolib, per il supporto alla serie C5 — che
 *     al momento della scrittura NON risulta confermato (§3 di
 *     docs/00-decisions.md). Se il supporto non c'è, il piano B è micro-ecc
 *     o Mbed TLS ridotta, e questo file va riscritto.
 *
 * La prova che il backend è corretto non è la lettura di questo codice: è far
 * passare tests/target/selftest.c sul pezzo, che esegue lo stesso corpus di
 * vettori già verificato su host contro OpenSSL.
 * ============================================================================
 */

#include <string.h>

#include "crypto.h"

/* DA VERIFICARE: nomi e percorsi degli header. */
#include "cmox_crypto.h"
#include "stm32c5xx_hal.h"

/*
 * DA VERIFICARE: dimensione del buffer di lavoro per la verifica su
 * SECP256R1. La cryptolib espone una macro; se il nome differisce, va usata
 * quella giusta e non un numero scelto a occhio — un buffer troppo piccolo
 * non dà un errore pulito, dà comportamento indefinito.
 */
#ifndef CMOX_ECC_SECP256R1_VERIFY_BUF_LEN
#define CMOX_ECC_SECP256R1_VERIFY_BUF_LEN 2000u
#endif

#define SHA256_TIMEOUT_MS 1000u

static HASH_HandleTypeDef hhash;
static bool crypto_ready = false;

/*
 * Buffer di lavoro della cryptolib. Statico e non sullo stack: due kilobyte
 * di stack in un bootloader sono una pessima idea, e qui la vita del buffer
 * coincide con quella della singola verifica.
 */
static uint32_t ecc_workspace[CMOX_ECC_SECP256R1_VERIFY_BUF_LEN / 4u + 1u];

int sbl_crypto_init(void)
{
    /* DA VERIFICARE: firma e valore di ritorno di cmox_initialize. */
    if (cmox_initialize(NULL) != CMOX_INIT_SUCCESS) {
        return SBL_CRYPTO_FAIL;
    }

    hhash.Instance = HASH;
    hhash.Init.DataType = HASH_DATATYPE_8B;
    /* DA VERIFICARE: sul C5 l'algoritmo potrebbe andare scelto qui invece
     * che dalla funzione di Start. */
    if (HAL_HASH_Init(&hhash) != HAL_OK) {
        return SBL_CRYPTO_FAIL;
    }

    crypto_ready = true;
    return SBL_CRYPTO_OK;
}

int sbl_sha256(const uint8_t *data, size_t len, uint8_t out[32])
{
    if (!crypto_ready || (data == NULL) || (out == NULL)) {
        return SBL_CRYPTO_FAIL;
    }
    /*
     * L'acceleratore prende la lunghezza in un registro a 32 bit. Sul target
     * len non supera i 100 KB dell'area, ma il controllo resta perché questa
     * funzione è generica e il costo è nullo.
     */
    if (len > 0xFFFFFFFFu) {
        return SBL_CRYPTO_FAIL;
    }

    /*
     * DA VERIFICARE: nome esatto della funzione sul C5. Sulle famiglie con
     * SHA-256 nell'acceleratore è HAL_HASHEx_SHA256_Start; su altre l'API
     * unificata più recente è HAL_HASH_Start con l'algoritmo scelto in Init.
     *
     * `data` punta alla flash mappata in memoria, quindi i ~100 KB del
     * payload si danno in pasto direttamente, senza copie intermedie.
     */
    if (HAL_HASHEx_SHA256_Start(&hhash, (uint8_t *)(uintptr_t)data,
                                (uint32_t)len, out,
                                SHA256_TIMEOUT_MS) != HAL_OK) {
        return SBL_CRYPTO_FAIL;
    }
    return SBL_CRYPTO_OK;
}

bool sbl_ecdsa_p256_verify(const uint8_t pubkey[64],
                           const uint8_t *msg, size_t len,
                           const uint8_t signature[64])
{
    cmox_ecc_handle_t ctx;
    uint32_t fault_check = CMOX_ECC_AUTH_FAIL;
    cmox_ecc_retval_t retval;
    uint8_t digest[32];
    bool accepted = false;

    if (!crypto_ready || (pubkey == NULL) || (msg == NULL)
            || (signature == NULL)) {
        return false;
    }

    /*
     * cmox_ecdsa_verify vuole il digest, non il messaggio. Lo calcoliamo qui
     * con l'acceleratore: sono 448 byte, quindi il costo è trascurabile, ma
     * tenere il contratto di sbl_ecdsa_p256_verify a "messaggio" evita che il
     * chiamante debba sapere quale hash usa la firma.
     */
    if (sbl_sha256(msg, len, digest) != SBL_CRYPTO_OK) {
        return false;
    }

    /* DA VERIFICARE: nome della costante delle funzioni matematiche. */
    cmox_ecc_construct(&ctx, CMOX_ECC256_MATH_FUNCS,
                       ecc_workspace, sizeof(ecc_workspace));

    /*
     * DA VERIFICARE: ordine dei parametri e formato atteso della chiave.
     * Per SECP256R1 la cryptolib vuole X || Y non compresse, 64 byte, che è
     * esattamente il formato in cui la chiave sta in flash (§8).
     */
    retval = cmox_ecdsa_verify(&ctx,
                               CMOX_ECC_CURVE_SECP256R1,
                               pubkey, 64u,
                               digest, sizeof(digest),
                               signature, 64u,
                               &fault_check);

    /*
     * Doppio controllo contro il fault injection.
     *
     * La cryptolib restituisce l'esito due volte, per due strade diverse: nel
     * valore di ritorno e in fault_check. Un singolo glitch che forza uno dei
     * due difficilmente forza anche l'altro, quindi pretenderli entrambi alza
     * il costo di un attacco. È una mitigazione parziale della M10 del threat
     * model, non una difesa completa.
     *
     * Il valore di ritorno viene inizializzato a "fallito": se la chiamata non
     * lo tocca affatto — perché non è mai partita — l'esito resta negativo.
     */
    accepted = (retval == CMOX_ECC_AUTH_SUCCESS)
               && (fault_check == CMOX_ECC_AUTH_SUCCESS);

    cmox_ecc_cleanup(&ctx);

    /* Il digest non è segreto, ma non c'è motivo di lasciarlo in RAM. */
    memset(digest, 0, sizeof(digest));

    return accepted;
}
