#!/usr/bin/env python3
"""
Tool di firma delle immagini firmware.

Specifica del formato: docs/02-image-format.md

    ./sign_image.py keygen  -o signing_key.pem
    ./sign_image.py pubkey  -k signing_key.pem
    ./sign_image.py sign    -i app.bin -k signing_key.pem \
                            --sec-version 3 --fw-version 1.4.2.117 \
                            --product-id 0x1 -o app_signed.bin
    ./sign_image.py info    -i app_signed.bin
    ./sign_image.py verify  -i app_signed.bin -k signing_key.pem

⚠️ In produzione la chiave privata non deve stare in un file: vedi il rischio
R2 di docs/01-threat-model.md.
"""

import argparse
import hashlib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import sbl_crypto
import sbl_format as fmt


def _hex(data):
    return data.hex()


def cmd_keygen(args):
    if os.path.exists(args.output) and not args.force:
        sys.exit(f"errore: {args.output} esiste già. Usa --force per "
                 f"sovrascriverlo, ma assicurati di non star buttando via "
                 f"l'unica copia di una chiave in uso.")
    key = sbl_crypto.generate_key()
    sbl_crypto.save_private_key(key, args.output)
    os.chmod(args.output, 0o600)
    print(f"chiave privata scritta in {args.output}")
    _print_pubkey(key)


def _print_pubkey(key):
    raw = sbl_crypto.public_key_raw(key)
    print(f"\nchiave pubblica (64 byte, va in flash nel bootloader):")
    print(f"  {_hex(raw)}")
    print(f"\nSHA-256 della chiave pubblica (32 byte, va in OTP):")
    print(f"  {_hex(hashlib.sha256(raw).digest())}")


def cmd_pubkey(args):
    _print_pubkey(sbl_crypto.load_private_key(args.key))


def cmd_sign(args):
    with open(args.input, "rb") as fh:
        payload = fh.read()

    try:
        header = fmt.build(
            payload=payload,
            load_address=args.load_addr,
            security_version=args.sec_version,
            firmware_version=fmt.parse_fw_version(args.fw_version),
            product_id=args.product_id,
        )
    except ValueError as exc:
        # Meglio fermarsi qui che produrre un'immagine che nessun
        # dispositivo accetterà.
        sys.exit(f"errore: {exc}")

    key = sbl_crypto.load_private_key(args.key)
    header.signature = sbl_crypto.sign(key, header.signed_region())

    with open(args.output, "wb") as fh:
        fh.write(header.pack())
        fh.write(payload)

    used = len(payload)
    print(f"immagine firmata scritta in {args.output}")
    print(f"  payload           {used} byte "
          f"({100.0 * used / fmt.MAX_PAYLOAD:.1f}% dell'area, "
          f"{fmt.MAX_PAYLOAD - used} byte liberi)")
    print(f"  load_address      0x{header.load_address:08X}")
    print(f"  entry_vtor        0x{header.entry_vtor:08X}")
    print(f"  security_version  {header.security_version}")
    print(f"  firmware_version  {args.fw_version}")
    print(f"  product_id        0x{header.product_id:08X}")


def cmd_info(args):
    with open(args.input, "rb") as fh:
        image = fh.read()
    h = fmt.Header.unpack(image)
    magic_ok = "ok" if h.magic == fmt.MAGIC else "NON VALIDO"
    print(f"magic             0x{h.magic:08X}  ({magic_ok})")
    print(f"header_version    {h.header_version}")
    print(f"header_size       {h.header_size}")
    print(f"image_size        {h.image_size} byte")
    print(f"load_address      0x{h.load_address:08X}")
    print(f"entry_vtor        0x{h.entry_vtor:08X}")
    print(f"security_version  {h.security_version}")
    print(f"firmware_version  {fmt.format_fw_version(h.firmware_version)}")
    print(f"product_id        0x{h.product_id:08X}")
    print(f"payload_sha256    {_hex(h.payload_sha256)}")
    print(f"signature         {_hex(h.signature[:16])}...")
    print(f"dimensione file   {len(image)} byte "
          f"(attesa {fmt.HEADER_SIZE + h.image_size})")


def cmd_verify(args):
    with open(args.input, "rb") as fh:
        image = fh.read()

    if args.key:
        pubkey = sbl_crypto.public_key_raw(sbl_crypto.load_private_key(args.key))
    else:
        pubkey = bytes.fromhex(args.pubkey)

    otp_hash = (bytes.fromhex(args.otp_hash) if args.otp_hash
                else hashlib.sha256(pubkey).digest())

    header = fmt.Header.unpack(image)
    product_id = (args.product_id if args.product_id is not None
                  else header.product_id)

    try:
        fmt.verify(
            image=image,
            root_pubkey=pubkey,
            otp_pubkey_hash=otp_hash,
            otp_security_version=args.otp_counter,
            device_product_id=product_id,
            verify_signature=sbl_crypto.verify,
        )
    except fmt.VerifyError as exc:
        print(f"RIFIUTATA — {exc}")
        return 1

    print("ACCETTATA — tutti e dodici i passi superati")
    return 0


def _auto_int(text):
    return int(text, 0)


def main():
    p = argparse.ArgumentParser(
        description="Firma e verifica immagini firmware per il secure "
                    "bootloader STM32C542KCT.")
    sub = p.add_subparsers(dest="cmd", required=True)

    g = sub.add_parser("keygen", help="genera una chiave di firma P-256")
    g.add_argument("-o", "--output", default="signing_key.pem")
    g.add_argument("--force", action="store_true",
                   help="sovrascrive una chiave esistente")
    g.set_defaults(func=cmd_keygen)

    g = sub.add_parser("pubkey",
                       help="stampa la chiave pubblica e il suo hash per OTP")
    g.add_argument("-k", "--key", required=True)
    g.set_defaults(func=cmd_pubkey)

    g = sub.add_parser("sign", help="firma un binario applicativo")
    g.add_argument("-i", "--input", required=True, help="binario grezzo")
    g.add_argument("-k", "--key", required=True)
    g.add_argument("-o", "--output", required=True)
    g.add_argument("--load-addr", type=_auto_int, default=fmt.EXEC_BASE)
    g.add_argument("--sec-version", type=_auto_int, required=True,
                   help="contatore anti-rollback, monotono")
    g.add_argument("--fw-version", default="0.0.0.0", help="MM.mm.pp.bb")
    g.add_argument("--product-id", type=_auto_int, required=True)
    g.set_defaults(func=cmd_sign)

    g = sub.add_parser("info", help="mostra l'header di un'immagine")
    g.add_argument("-i", "--input", required=True)
    g.set_defaults(func=cmd_info)

    g = sub.add_parser("verify",
                       help="verifica un'immagine con i dodici passi")
    g.add_argument("-i", "--input", required=True)
    g.add_argument("-k", "--key", help="chiave privata da cui ricavare la pubblica")
    g.add_argument("--pubkey", help="chiave pubblica raw in esadecimale")
    g.add_argument("--otp-hash", help="hash in OTP; default: quello della chiave")
    g.add_argument("--otp-counter", type=_auto_int, default=0)
    g.add_argument("--product-id", type=_auto_int,
                   help="default: quello dell'immagine")
    g.set_defaults(func=cmd_verify)

    args = p.parse_args()
    if args.cmd == "verify" and not args.key and not args.pubkey:
        p.error("verify richiede --key oppure --pubkey")
    return args.func(args) or 0


if __name__ == "__main__":
    sys.exit(main())
