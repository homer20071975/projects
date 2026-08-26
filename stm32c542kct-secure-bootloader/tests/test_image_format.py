#!/usr/bin/env python3
"""
Test del formato immagine e del verificatore di riferimento.

    python3 tests/test_image_format.py

Ogni caso della tabella "Vettori di test" di docs/02-image-format.md ha qui
il suo test, e verifica non solo che l'immagine venga rifiutata ma **a quale
passo**. Il passo conta: un'immagine con image_size gonfiato deve cadere al
passo 4, sui limiti, non al passo 8 sulla firma — altrimenti significa che il
bootloader ha letto fuori area prima di accorgersene.
"""

import os
import re
import sys
import unittest

_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(_ROOT, "tools"))

import sbl_crypto
import sbl_format as fmt


def make_image(key, payload=b"\xA5" * 4096, load_address=fmt.EXEC_BASE,
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


class ImageFormatTest(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.key = sbl_crypto.generate_key()
        cls.pubkey = sbl_crypto.public_key_raw(cls.key)
        cls.otp_hash = __import__("hashlib").sha256(cls.pubkey).digest()

    def check(self, image, otp_counter=0, product_id=0x1, **kw):
        return fmt.verify(
            image=image,
            root_pubkey=kw.get("pubkey", self.pubkey),
            otp_pubkey_hash=kw.get("otp_hash", self.otp_hash),
            otp_security_version=otp_counter,
            device_product_id=product_id,
            verify_signature=sbl_crypto.verify,
        )

    def expect_reject(self, image, step, **kw):
        with self.assertRaises(fmt.VerifyError) as ctx:
            self.check(image, **kw)
        self.assertEqual(ctx.exception.step, step,
                         f"rifiutata al passo {ctx.exception.step} "
                         f"invece che al {step}: {ctx.exception}")
        return ctx.exception

    # --- caso positivo -----------------------------------------------------

    def test_immagine_valida(self):
        self.assertEqual(self.check(make_image(self.key)), fmt.OK)

    def test_security_version_uguale_al_contatore(self):
        """Reinstallare la versione corrente è lecito: il confronto è >=."""
        img = make_image(self.key, security_version=7)
        self.assertEqual(self.check(img, otp_counter=7), fmt.OK)

    # --- struttura (passi 1-3) ---------------------------------------------

    def test_magic_corrotto(self):
        self.expect_reject(tamper(make_image(self.key), 0x000), step=1)

    def test_area_cancellata(self):
        self.expect_reject(b"\xFF" * 8192, step=1)

    def test_immagine_troncata(self):
        with self.assertRaises(fmt.VerifyError):
            self.check(make_image(self.key)[:100])

    def test_header_version_non_supportata(self):
        self.expect_reject(tamper(make_image(self.key), 0x004, 0x99), step=2)

    def test_header_size_errato(self):
        # header_size vale 512 = 0x0200 little-endian: il byte basso e' gia'
        # zero, quindi va toccato quello alto.
        self.expect_reject(tamper(make_image(self.key), 0x007, 0x00), step=3)

    # --- limiti (passi 4-6), prima di ogni lettura dimensionata ------------

    def test_image_size_oltre_la_capienza(self):
        img = bytearray(make_image(self.key))
        img[0x008:0x00C] = (fmt.MAX_PAYLOAD + 1).to_bytes(4, "little")
        self.expect_reject(bytes(img), step=4)

    def test_image_size_massimo(self):
        """0xFFFFFFFF deve cadere sui limiti, senza leggere fuori area."""
        img = bytearray(make_image(self.key))
        img[0x008:0x00C] = b"\xFF\xFF\xFF\xFF"
        self.expect_reject(bytes(img), step=4)

    def test_image_size_zero(self):
        img = bytearray(make_image(self.key))
        img[0x008:0x00C] = (0).to_bytes(4, "little")
        self.expect_reject(bytes(img), step=4)

    def test_load_address_di_un_altra_area(self):
        img = bytearray(make_image(self.key))
        img[0x00C:0x010] = fmt.STAGE_BASE.to_bytes(4, "little")
        self.expect_reject(bytes(img), step=5)

    def test_entry_vtor_non_allineato(self):
        img = bytearray(make_image(self.key))
        img[0x010:0x014] = (fmt.EXEC_BASE + 0x100).to_bytes(4, "little")
        self.expect_reject(bytes(img), step=6)

    def test_entry_vtor_fuori_area(self):
        img = bytearray(make_image(self.key))
        img[0x010:0x014] = (0x08000000).to_bytes(4, "little")
        self.expect_reject(bytes(img), step=6)

    # --- radice di fiducia (passo 7) ---------------------------------------

    def test_chiave_pubblica_non_corrisponde_a_otp(self):
        """Una chiave sostituita in flash non supera il confronto con l'OTP."""
        altra = sbl_crypto.generate_key()
        self.expect_reject(make_image(self.key), step=7,
                           pubkey=sbl_crypto.public_key_raw(altra))

    # --- firma (passo 8) ---------------------------------------------------

    def test_firma_alterata(self):
        self.expect_reject(tamper(make_image(self.key), 0x1C0), step=8)

    def test_firma_azzerata(self):
        img = bytearray(make_image(self.key))
        img[0x1C0:0x200] = b"\x00" * 64
        self.expect_reject(bytes(img), step=8)

    def test_firma_di_un_altra_chiave(self):
        """Firma valida in sé, ma prodotta da una chiave che non è la root."""
        altra = sbl_crypto.generate_key()
        payload = b"\xA5" * 4096
        hdr = fmt.build(payload, fmt.EXEC_BASE, 5, 0x01020304, 0x1)
        hdr.signature = sbl_crypto.sign(altra, hdr.signed_region())
        self.expect_reject(hdr.pack() + payload, step=8)

    def test_campo_riservato_alterato(self):
        """Anche i campi inutilizzati sono coperti dalla firma."""
        self.expect_reject(tamper(make_image(self.key), 0x040), step=8)

    def test_security_version_alterata_senza_rifirmare(self):
        """Alzare il campo per aggirare l'anti-rollback rompe la firma."""
        img = bytearray(make_image(self.key, security_version=1))
        img[0x014:0x018] = (999).to_bytes(4, "little")
        self.expect_reject(bytes(img), step=8)

    def test_payload_hash_alterato_senza_rifirmare(self):
        self.expect_reject(tamper(make_image(self.key), 0x020), step=8)

    # --- politica (passi 9-10) ---------------------------------------------

    def test_rollback_a_versione_precedente(self):
        img = make_image(self.key, security_version=2)
        self.expect_reject(img, step=9, otp_counter=5)

    def test_product_id_di_un_altro_prodotto(self):
        img = make_image(self.key, product_id=0x1)
        self.expect_reject(img, step=10, product_id=0x2)

    # --- payload (passo 11) ------------------------------------------------

    def test_payload_alterato(self):
        img = make_image(self.key)
        self.expect_reject(tamper(img, fmt.HEADER_SIZE + 10), step=11)

    def test_payload_ultimo_byte_alterato(self):
        img = make_image(self.key)
        self.expect_reject(tamper(img, len(img) - 1), step=11)

    # --- il tool di firma ---------------------------------------------------

    def test_il_tool_rifiuta_un_payload_troppo_grande(self):
        """Meglio un errore in build che un'immagine che nessuno accetterà."""
        with self.assertRaises(ValueError):
            fmt.build(b"\x00" * (fmt.MAX_PAYLOAD + 1),
                      fmt.EXEC_BASE, 1, 0, 0x1)

    def test_il_tool_rifiuta_un_payload_vuoto(self):
        with self.assertRaises(ValueError):
            fmt.build(b"", fmt.EXEC_BASE, 1, 0, 0x1)

    def test_payload_di_dimensione_massima(self):
        img = make_image(self.key, payload=b"\x5A" * fmt.MAX_PAYLOAD)
        self.assertEqual(self.check(img), fmt.OK)

    # --- versione firmware --------------------------------------------------

    def test_round_trip_versione(self):
        self.assertEqual(fmt.parse_fw_version("1.4.2.117"), 0x01040275)
        self.assertEqual(fmt.format_fw_version(0x01040275), "1.4.2.117")


class CHeaderConsistencyTest(unittest.TestCase):
    """
    Le costanti Python devono restare allineate a quelle C.

    Se divergono, il tool firma su un layout e il bootloader ne verifica un
    altro: le immagini verrebbero rifiutate solo sul target, dove il guasto
    è molto più caro da diagnosticare.
    """

    def _defines(self, *paths):
        text = ""
        for path in paths:
            with open(os.path.join(_ROOT, path), encoding="utf-8") as fh:
                text += fh.read() + "\n"
        out = {}
        for name, value in re.findall(
                r"^#define\s+(SBL_\w+)\s+(.+?)\s*(?:/\*.*)?$",
                text, re.MULTILINE):
            expr = value.strip().rstrip("u").replace("u)", ")")
            expr = re.sub(r"(\d)u\b", r"\1", expr)
            try:
                out[name] = eval(expr, {"__builtins__": {}}, dict(out))
            except Exception:
                pass
        return out

    def test_costanti_del_formato(self):
        d = self._defines("inc/image_header.h")
        self.assertEqual(d["SBL_MAGIC"], fmt.MAGIC)
        self.assertEqual(d["SBL_HEADER_SIZE"], fmt.HEADER_SIZE)
        self.assertEqual(d["SBL_SIGNED_LEN"], fmt.SIGNED_LEN)
        self.assertEqual(d["SBL_HEADER_VERSION"], fmt.HEADER_VERSION)
        self.assertEqual(d["SBL_SIGNATURE_LEN"], fmt.SIGNATURE_LEN)
        self.assertEqual(d["SBL_RESERVED_LEN"], fmt.RESERVED_LEN)

    def test_geometria_della_flash(self):
        d = self._defines("inc/image_header.h", "inc/memory_map.h")
        self.assertEqual(d["SBL_EXEC_BASE"], fmt.EXEC_BASE)
        self.assertEqual(d["SBL_EXEC_SIZE"], fmt.EXEC_SIZE)
        self.assertEqual(d["SBL_STAGE_BASE"], fmt.STAGE_BASE)
        self.assertEqual(d["SBL_STAGE_SIZE"], fmt.STAGE_SIZE)

    def test_max_payload_coerente(self):
        d = self._defines("inc/image_header.h", "inc/memory_map.h")
        self.assertEqual(d["SBL_MAX_PAYLOAD"], fmt.MAX_PAYLOAD)
        self.assertEqual(d["SBL_APP_VTOR"], fmt.APP_VTOR)


if __name__ == "__main__":
    unittest.main(verbosity=2)
