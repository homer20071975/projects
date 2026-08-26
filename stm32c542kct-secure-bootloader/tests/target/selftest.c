/*
 * selftest.c — esegue il corpus di vettori sul target.
 *
 * Perché esiste: il confronto differenziale di tests/test_differential.py
 * gira su PC con OpenSSL, quindi dimostra che la *logica* di src/verify.c è
 * quella giusta — non che X-CUBE-CRYPTOLIB si comporti come OpenSSL. Questo
 * self-test chiude quel buco: stesso corpus, stessi esiti attesi, ma eseguito
 * sul pezzo con il backend vero.
 *
 * È il collaudo che rende credibile src/crypto_stm32.c, che nessuno ha mai
 * compilato.
 *
 * Non fa parte del firmware di produzione: si compila in un binario a sé.
 *
 * Compila anche su host, con il backend OpenSSL dei test, e lì è già stato
 * eseguito con successo: vedi tests/target/Makefile.
 */

#include <stdio.h>

#include "verify.h"
#include "crypto.h"
#include "vectors_data.h"

/*
 * Su target sostituire con la propria uscita diagnostica: UART, ITM, o un
 * buffer in RAM da leggere col debugger.
 */
#ifndef SBL_SELFTEST_LOG
#define SBL_SELFTEST_LOG(...) printf(__VA_ARGS__)
#endif

int sbl_selftest_run(void)
{
    size_t passed = 0u;
    size_t failed = 0u;

    if (sbl_crypto_init() != SBL_CRYPTO_OK) {
        SBL_SELFTEST_LOG("FALLITO: il backend crittografico non parte\n");
        return -1;
    }

    for (size_t i = 0u; i < sbl_test_vector_count; i++) {
        const sbl_test_vector_t *v = &sbl_test_vectors[i];
        sbl_verify_ctx_t ctx;
        sbl_verify_result_t got;

        ctx.area_base = v->area_base;
        ctx.area_size = v->area_size;
        ctx.root_pubkey = v->pubkey;
        ctx.otp_pubkey_hash = v->otp_hash;
        ctx.otp_security_version = v->otp_security_version;
        ctx.product_id = v->product_id;

        got = sbl_verify_image(v->image, v->image_len, &ctx);

        if ((int32_t)got == v->expected) {
            passed++;
        } else {
            failed++;
            SBL_SELFTEST_LOG("  FALLITO  %-40s atteso %ld, ottenuto %ld\n",
                             v->name, (long)v->expected, (long)got);
        }
    }

    SBL_SELFTEST_LOG("self-test: %zu superati, %zu falliti su %zu\n",
                     passed, failed, sbl_test_vector_count);
    return (failed == 0u) ? 0 : -1;
}

#ifndef SBL_SELFTEST_NO_MAIN
int main(void)
{
    return (sbl_selftest_run() == 0) ? 0 : 1;
}
#endif
