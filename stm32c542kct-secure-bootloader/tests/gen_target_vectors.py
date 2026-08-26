#!/usr/bin/env python3
"""
Genera il corpus di vettori come sorgenti C, da compilare nel self-test che
gira sul target.

    python3 tests/gen_target_vectors.py [-o tests/target]

Produce vectors_data.h e vectors_data.c. Il corpus è lo stesso di
tests/vectors.py — quello già verificato su host contro OpenSSL — solo con
payload piccoli, perché qui finisce dentro un binario.

Rigenerare dopo ogni modifica al corpus: i file prodotti sono versionati
perché il target va compilato anche da chi non ha Python a portata di mano.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tools"))

import sbl_crypto
import vectors

PAYLOAD = bytes(range(64))


def c_bytes(data, indent="    "):
    out = []
    for i in range(0, len(data), 12):
        row = ", ".join("0x%02X" % b for b in data[i:i + 12])
        out.append(indent + row + ",")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--output-dir",
                    default=os.path.join(os.path.dirname(
                        os.path.abspath(__file__)), "target"))
    args = ap.parse_args()

    key = sbl_crypto.generate_key()
    cases = vectors.build_cases(key, payload=PAYLOAD, compact=True)

    os.makedirs(args.output_dir, exist_ok=True)
    banner = ("/* Generato da tests/gen_target_vectors.py — non modificare "
              "a mano. */\n")

    header = os.path.join(args.output_dir, "vectors_data.h")
    with open(header, "w", encoding="utf-8") as fh:
        fh.write(banner)
        fh.write("""
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
""")

    source = os.path.join(args.output_dir, "vectors_data.c")
    with open(source, "w", encoding="utf-8") as fh:
        fh.write(banner)
        fh.write('\n#include "vectors_data.h"\n\n')

        keys = {}
        for i, c in enumerate(cases):
            for kind, blob in (("pub", c.pubkey), ("otp", c.otp_hash)):
                if blob not in keys:
                    label = "k%s_%d" % (kind, len(keys))
                    keys[blob] = label
                    fh.write("static const uint8_t %s[] = {\n%s\n};\n\n"
                             % (label, c_bytes(blob)))

        for i, c in enumerate(cases):
            fh.write("static const uint8_t img_%d[] = {\n%s\n};\n\n"
                     % (i, c_bytes(c.image)))

        fh.write("const sbl_test_vector_t sbl_test_vectors[] = {\n")
        for i, c in enumerate(cases):
            fh.write(
                '    { "%s", img_%d, sizeof(img_%d), 0x%08Xu, 0x%08Xu, %du, '
                '0x%08Xu, %s, %s, %d },\n'
                % (c.name, i, i, c.area_base, c.area_size, c.otp_secver,
                   c.product_id, keys[c.pubkey], keys[c.otp_hash],
                   c.expected))
        fh.write("};\n\n")
        fh.write("const size_t sbl_test_vector_count =\n"
                 "    sizeof(sbl_test_vectors) / sizeof(sbl_test_vectors[0]);\n")

    total = sum(len(c.image) for c in cases)
    print("%d vettori, %d byte di immagini" % (len(cases), total))
    print("scritti %s e %s" % (header, source))


if __name__ == "__main__":
    main()
