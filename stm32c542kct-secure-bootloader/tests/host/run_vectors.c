/*
 * run_vectors.c — esegue sbl_verify_image() sui vettori generati da
 * tests/gen_vectors.py e stampa l'esito di ciascuno.
 *
 * Il confronto con il riferimento Python lo fa tests/test_differential.py:
 * qui ci si limita a produrre, per ogni vettore, il codice restituito dal
 * codice C vero del bootloader.
 *
 * Uso:  run_vectors <manifest> <directory dei vettori>
 * Out:  una riga per vettore, "<nome> <codice>"
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "verify.h"

#define MAX_IMAGE (128u * 1024u)
#define HEX_MAX   256

static size_t unhex(const char *hex, uint8_t *out, size_t out_max)
{
    size_t n = strlen(hex) / 2u;
    if (n > out_max) {
        return 0u;
    }
    for (size_t i = 0u; i < n; i++) {
        unsigned int byte = 0u;
        if (sscanf(hex + 2u * i, "%2x", &byte) != 1) {
            return 0u;
        }
        out[i] = (uint8_t)byte;
    }
    return n;
}

static size_t load(const char *path, uint8_t *buf, size_t buf_max)
{
    FILE *fh = fopen(path, "rb");
    size_t n;
    if (fh == NULL) {
        return 0u;
    }
    n = fread(buf, 1u, buf_max, fh);
    fclose(fh);
    return n;
}

int main(int argc, char **argv)
{
    static uint8_t image[MAX_IMAGE];
    char line[1024];
    char name[128], file[128], pubkey_hex[HEX_MAX], otphash_hex[HEX_MAX];
    unsigned long area_base, area_size, otp_secver, product_id;
    long expected;
    FILE *manifest;
    int failures = 0;

    if (argc != 3) {
        fprintf(stderr, "uso: %s <manifest> <dir>\n", argv[0]);
        return 2;
    }

    manifest = fopen(argv[1], "r");
    if (manifest == NULL) {
        fprintf(stderr, "manifest non leggibile: %s\n", argv[1]);
        return 2;
    }

    while (fgets(line, sizeof(line), manifest) != NULL) {
        uint8_t pubkey[64], otphash[32];
        char path[512];
        sbl_verify_ctx_t ctx;
        sbl_verify_result_t result;
        size_t image_len;

        if ((line[0] == '#') || (line[0] == '\n')) {
            continue;
        }
        if (sscanf(line, "%127s %127s %lx %lx %lu %lx %255s %255s %ld",
                   name, file, &area_base, &area_size, &otp_secver,
                   &product_id, pubkey_hex, otphash_hex, &expected) != 9) {
            fprintf(stderr, "riga non interpretabile: %s", line);
            failures++;
            continue;
        }

        if ((unhex(pubkey_hex, pubkey, sizeof(pubkey)) != sizeof(pubkey))
                || (unhex(otphash_hex, otphash, sizeof(otphash))
                    != sizeof(otphash))) {
            fprintf(stderr, "chiave o hash malformati in %s\n", name);
            failures++;
            continue;
        }

        snprintf(path, sizeof(path), "%s/%s", argv[2], file);
        image_len = load(path, image, sizeof(image));
        if (image_len == 0u) {
            fprintf(stderr, "vettore non leggibile: %s\n", path);
            failures++;
            continue;
        }

        ctx.area_base = (uint32_t)area_base;
        ctx.area_size = (uint32_t)area_size;
        ctx.root_pubkey = pubkey;
        ctx.otp_pubkey_hash = otphash;
        ctx.otp_security_version = (uint32_t)otp_secver;
        ctx.product_id = (uint32_t)product_id;

        result = sbl_verify_image(image, image_len, &ctx);
        printf("%s %ld\n", name, (long)result);
    }

    fclose(manifest);
    return failures ? 1 : 0;
}
