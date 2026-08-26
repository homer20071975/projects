/*
 * verify.c — i dodici passi di docs/02-image-format.md.
 *
 * L'ordine dei controlli non è arbitrario e non va riordinato per comodità:
 * i limiti vengono prima di qualunque lettura dimensionata da campi
 * dell'header, e la chiave viene autenticata prima di essere usata. Le
 * motivazioni sono nei commenti ai singoli passi.
 */

#include <string.h>

#include "verify.h"
#include "crypto.h"

/*
 * Confronto che non esce in anticipo.
 *
 * Un memcmp normale termina al primo byte diverso, e il tempo che impiega
 * racconta quanti byte iniziali coincidevano. Su un confronto di hash non è
 * lo scenario più temibile, ma costa zero evitarlo.
 *
 * Restituisce 0 se i buffer coincidono.
 */
static uint8_t ct_diff(const uint8_t *a, const uint8_t *b, size_t n)
{
    uint8_t diff = 0u;
    for (size_t i = 0u; i < n; i++) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return diff;
}

sbl_verify_result_t sbl_verify_image(const uint8_t *image, size_t image_len,
                                     const sbl_verify_ctx_t *ctx)
{
    sbl_image_header_t hdr;
    uint8_t digest[SBL_SHA256_LEN];
    uint32_t max_payload;

    if ((image == NULL) || (ctx == NULL)) {
        return SBL_ERR_MAGIC;
    }
    if (ctx->area_size <= SBL_HEADER_SIZE) {
        return SBL_ERR_IMAGE_SIZE;
    }
    if (image_len < SBL_HEADER_SIZE) {
        return SBL_ERR_MAGIC;
    }

    /*
     * Copia in un'istantanea locale: i campi vengono letti più volte e
     * lavorare su una copia toglie di mezzo ogni dubbio su allineamento e
     * aliasing. Costa 512 byte di stack.
     */
    memcpy(&hdr, image, SBL_HEADER_SIZE);

    /* --- 1-3: struttura --- */

    if (hdr.magic != SBL_MAGIC) {
        return SBL_ERR_MAGIC;
    }
    if (hdr.header_version != SBL_HEADER_VERSION) {
        return SBL_ERR_HEADER_VERSION;
    }
    if (hdr.header_size != SBL_HEADER_SIZE) {
        return SBL_ERR_HEADER_SIZE;
    }

    /*
     * --- 4-6: limiti ---
     *
     * Vengono prima del passo 11, che legge image_size byte. Senza questi
     * controlli un image_size gonfiato farebbe leggere fuori dall'area: è il
     * modo classico in cui un bootloader si fa male da solo.
     */

    max_payload = ctx->area_size - SBL_HEADER_SIZE;
    if ((hdr.image_size == 0u) || (hdr.image_size > max_payload)) {
        return SBL_ERR_IMAGE_SIZE;
    }
    if ((size_t)hdr.image_size > (image_len - SBL_HEADER_SIZE)) {
        return SBL_ERR_IMAGE_SIZE;
    }
    if (hdr.load_address != ctx->area_base) {
        return SBL_ERR_LOAD_ADDRESS;
    }
    if ((hdr.entry_vtor < ctx->area_base) ||
        (hdr.entry_vtor >= (ctx->area_base + ctx->area_size))) {
        return SBL_ERR_ENTRY_VTOR;
    }
    if ((hdr.entry_vtor & 0x1FFu) != 0u) {
        /* VTOR vuole la vector table allineata a 512 byte. */
        return SBL_ERR_ENTRY_VTOR;
    }

    /*
     * --- 7: radice di fiducia ---
     *
     * La chiave sta in flash, che un attaccante con accesso fisico può
     * riscrivere. L'ancora è il suo hash in OTP. Verificare una firma con una
     * chiave non autenticata non dimostrerebbe nulla.
     */

    if (sbl_sha256(ctx->root_pubkey, SBL_PUBKEY_LEN, digest) != SBL_CRYPTO_OK) {
        return SBL_ERR_ROOT_KEY;
    }
    if (ct_diff(digest, ctx->otp_pubkey_hash, SBL_SHA256_LEN) != 0u) {
        return SBL_ERR_ROOT_KEY;
    }

    /*
     * --- 8: firma ---
     *
     * Copre i primi 448 byte dell'header, che si leggono direttamente
     * dall'immagine. Il payload è legato tramite payload_sha256, che sta
     * dentro quella regione.
     */

    if (!sbl_ecdsa_p256_verify(ctx->root_pubkey, image, SBL_SIGNED_LEN,
                               hdr.signature)) {
        return SBL_ERR_SIGNATURE;
    }

    /*
     * --- 9-10: politica ---
     *
     * Solo ora questi campi sono attendibili: fino al passo 8 nessuno aveva
     * dimostrato che non fossero stati manomessi.
     */

    if (hdr.security_version < ctx->otp_security_version) {
        return SBL_ERR_SECURITY_VERSION;
    }
    if (hdr.product_id != ctx->product_id) {
        return SBL_ERR_PRODUCT_ID;
    }

    /* --- 11: payload, il passo più costoso, per ultimo --- */

    if (sbl_sha256(image + SBL_HEADER_SIZE, hdr.image_size, digest)
            != SBL_CRYPTO_OK) {
        return SBL_ERR_PAYLOAD_HASH;
    }
    if (ct_diff(digest, hdr.payload_sha256, SBL_SHA256_LEN) != 0u) {
        return SBL_ERR_PAYLOAD_HASH;
    }

    /* --- 12: sta al chiamante impostare VTOR e saltare --- */

    return SBL_OK;
}
