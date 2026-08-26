"""
Corpus di vettori di test per il formato immagine.

Unica fonte dei casi: li usano sia i test contro il riferimento Python
(test_image_format.py) sia il confronto differenziale contro
l'implementazione C (test_differential.py). Aggiungere un caso qui lo fa
entrare automaticamente in entrambi.

I casi coprono la tabella "Vettori di test" di docs/02-image-format.md.
"""

import hashlib
import os
import sys
from collections import namedtuple

sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tools"))

import sbl_crypto
import sbl_format as fmt

Case = namedtuple("Case", "name image area_base area_size otp_secver "
                          "product_id pubkey otp_hash expected")

DEFAULT_PAYLOAD = b"\xA5" * 4096


def make_image(key, payload=DEFAULT_PAYLOAD, load_address=fmt.EXEC_BASE,
               security_version=5, firmware_version=0x01020304,
               product_id=0x1, entry_vtor=None):
    hdr = fmt.build(payload, load_address, security_version,
                    firmware_version, product_id, entry_vtor)
    hdr.signature = sbl_crypto.sign(key, hdr.signed_region())
    return hdr.pack() + payload


def tamper(image, offset, value=None):
    """Cambia un byte senza rifirmare."""
    data = bytearray(image)
    data[offset] = value if value is not None else data[offset] ^ 0xFF
    return bytes(data)


def patch_u32(image, offset, value):
    data = bytearray(image)
    data[offset:offset + 4] = value.to_bytes(4, "little")
    return bytes(data)


def build_cases(key):
    """Genera il corpus. `key` è la chiave privata di root."""
    pub = sbl_crypto.public_key_raw(key)
    otp = hashlib.sha256(pub).digest()
    good = make_image(key)

    def case(name, image, expected, otp_secver=0, product_id=0x1,
             pubkey=None, otp_hash=None, area_base=fmt.EXEC_BASE,
             area_size=fmt.EXEC_SIZE):
        return Case(name, image, area_base, area_size, otp_secver,
                    product_id, pubkey or pub, otp_hash or otp, expected)

    cases = [
        # --- accettate ---
        case("valida", good, fmt.OK),
        case("secver_uguale_al_contatore",
             make_image(key, security_version=7), fmt.OK, otp_secver=7),
        case("secver_maggiore_del_contatore",
             make_image(key, security_version=9), fmt.OK, otp_secver=3),
        case("payload_di_dimensione_massima",
             make_image(key, payload=b"\x5A" * fmt.MAX_PAYLOAD), fmt.OK),
        case("payload_di_un_solo_byte",
             make_image(key, payload=b"\x01"), fmt.OK),

        # --- struttura: passi 1-3 ---
        case("magic_corrotto", tamper(good, 0x000), fmt.ERR_MAGIC),
        case("area_cancellata", b"\xFF" * 8192, fmt.ERR_MAGIC),
        case("tutta_a_zero", b"\x00" * 8192, fmt.ERR_MAGIC),
        case("immagine_troncata", good[:100], fmt.ERR_MAGIC),
        # header_size vale 512 = 0x0200 little-endian: il byte basso e' gia'
        # zero, quindi per alterarlo va toccato quello alto.
        case("header_version_ignota", tamper(good, 0x004, 0x99),
             fmt.ERR_HEADER_VERSION),
        case("header_size_errato", tamper(good, 0x007, 0x00),
             fmt.ERR_HEADER_SIZE),

        # --- limiti: passi 4-6, prima di ogni lettura dimensionata ---
        case("image_size_oltre_capienza",
             patch_u32(good, 0x008, fmt.MAX_PAYLOAD + 1), fmt.ERR_IMAGE_SIZE),
        case("image_size_massimo",
             patch_u32(good, 0x008, 0xFFFFFFFF), fmt.ERR_IMAGE_SIZE),
        case("image_size_zero",
             patch_u32(good, 0x008, 0), fmt.ERR_IMAGE_SIZE),
        case("image_size_oltre_i_byte_disponibili",
             patch_u32(good, 0x008, 60000), fmt.ERR_IMAGE_SIZE),
        case("load_address_dello_staging",
             patch_u32(good, 0x00C, fmt.STAGE_BASE), fmt.ERR_LOAD_ADDRESS),
        case("entry_vtor_non_allineato",
             patch_u32(good, 0x010, fmt.EXEC_BASE + 0x100),
             fmt.ERR_ENTRY_VTOR),
        case("entry_vtor_prima_dell_area",
             patch_u32(good, 0x010, 0x08000000), fmt.ERR_ENTRY_VTOR),
        case("entry_vtor_dopo_l_area",
             patch_u32(good, 0x010, fmt.EXEC_BASE + fmt.EXEC_SIZE),
             fmt.ERR_ENTRY_VTOR),

        # --- radice di fiducia: passo 7 ---
        case("chiave_sostituita_in_flash", good, fmt.ERR_ROOT_KEY,
             pubkey=sbl_crypto.public_key_raw(sbl_crypto.generate_key())),

        # --- firma: passo 8 ---
        case("firma_alterata", tamper(good, 0x1C0), fmt.ERR_SIGNATURE),
        case("firma_azzerata",
             good[:0x1C0] + b"\x00" * 64 + good[0x200:], fmt.ERR_SIGNATURE),
        case("campo_riservato_alterato", tamper(good, 0x040),
             fmt.ERR_SIGNATURE),
        case("payload_hash_alterato", tamper(good, 0x020),
             fmt.ERR_SIGNATURE),
        case("secver_alzata_senza_rifirmare",
             patch_u32(make_image(key, security_version=1), 0x014, 999),
             fmt.ERR_SIGNATURE),
        case("product_id_alterato_senza_rifirmare",
             patch_u32(good, 0x01C, 0x99), fmt.ERR_SIGNATURE),
        case("firmware_version_alterata", patch_u32(good, 0x018, 0xDEADBEEF),
             fmt.ERR_SIGNATURE),

        # --- politica: passi 9-10 ---
        case("rollback_a_versione_precedente",
             make_image(key, security_version=2), fmt.ERR_SECURITY_VERSION,
             otp_secver=5),
        case("prodotto_diverso", good, fmt.ERR_PRODUCT_ID, product_id=0x2),

        # --- payload: passo 11 ---
        case("payload_primo_byte_alterato",
             tamper(good, fmt.HEADER_SIZE), fmt.ERR_PAYLOAD_HASH),
        case("payload_byte_centrale_alterato",
             tamper(good, fmt.HEADER_SIZE + 2000), fmt.ERR_PAYLOAD_HASH),
        case("payload_ultimo_byte_alterato",
             tamper(good, len(good) - 1), fmt.ERR_PAYLOAD_HASH),
    ]

    # Firma valida ma prodotta da una chiave che non e' la root: la firma e'
    # ineccepibile in se', ma non risale alla radice di fiducia.
    altra = sbl_crypto.generate_key()
    hdr = fmt.build(DEFAULT_PAYLOAD, fmt.EXEC_BASE, 5, 0x01020304, 0x1)
    hdr.signature = sbl_crypto.sign(altra, hdr.signed_region())
    cases.append(case("firma_di_un_altra_chiave",
                      hdr.pack() + DEFAULT_PAYLOAD, fmt.ERR_SIGNATURE))

    return cases


def write_corpus(cases, directory):
    """Scrive i vettori e il manifest letto da tests/host/run_vectors."""
    os.makedirs(directory, exist_ok=True)
    lines = ["# nome file area_base area_size otp_secver product_id "
             "pubkey otp_hash atteso"]
    for i, c in enumerate(cases):
        filename = "%03d_%s.bin" % (i, c.name)
        with open(os.path.join(directory, filename), "wb") as fh:
            fh.write(c.image)
        lines.append(" ".join([
            c.name, filename, "%x" % c.area_base, "%x" % c.area_size,
            "%d" % c.otp_secver, "%x" % c.product_id,
            c.pubkey.hex(), c.otp_hash.hex(), "%d" % c.expected,
        ]))
    manifest = os.path.join(directory, "manifest.txt")
    with open(manifest, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")
    return manifest
