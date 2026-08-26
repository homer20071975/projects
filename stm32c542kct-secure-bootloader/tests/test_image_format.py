#!/usr/bin/env python3
"""
Test del formato immagine contro il riferimento Python.

    python3 tests/test_image_format.py

Il corpus dei vettori sta in vectors.py, condiviso con test_differential.py:
un caso aggiunto lì entra in entrambi.

Qui si verifica non solo che un'immagine malformata venga rifiutata, ma **a
quale passo**. Il passo conta: un'immagine con image_size gonfiato deve cadere
al passo 4, sui limiti, non al passo 8 sulla firma — se cadesse sulla firma
vorrebbe dire che il bootloader ha già letto fuori area. Un test che
controllasse solo "rifiutata" lascerebbe passare quel bug.
"""

import os
import re
import sys
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(_HERE)
sys.path.insert(0, _HERE)
sys.path.insert(0, os.path.join(_ROOT, "tools"))

import sbl_crypto
import sbl_format as fmt
import vectors


class CorpusTest(unittest.TestCase):
    """Ogni vettore del corpus, contro il riferimento Python."""

    @classmethod
    def setUpClass(cls):
        cls.key = sbl_crypto.generate_key()
        cls.cases = vectors.build_cases(cls.key)

    def _verify(self, case):
        return fmt.verify(
            image=case.image,
            root_pubkey=case.pubkey,
            otp_pubkey_hash=case.otp_hash,
            otp_security_version=case.otp_secver,
            device_product_id=case.product_id,
            area_base=case.area_base,
            area_size=case.area_size,
            verify_signature=sbl_crypto.verify,
        )

    def test_corpus(self):
        for case in self.cases:
            with self.subTest(vettore=case.name):
                if case.expected == fmt.OK:
                    self.assertEqual(self._verify(case), fmt.OK)
                else:
                    with self.assertRaises(fmt.VerifyError) as ctx:
                        self._verify(case)
                    self.assertEqual(
                        ctx.exception.code, case.expected,
                        "rifiutata al passo %d (%s) invece che con il codice "
                        "%d" % (ctx.exception.step, ctx.exception,
                                case.expected))


class SigningToolTest(unittest.TestCase):
    """Comportamenti del tool di firma, che non passano dal verificatore."""

    def test_rifiuta_un_payload_troppo_grande(self):
        """Meglio un errore in build che un'immagine che nessuno accetterà."""
        with self.assertRaises(ValueError):
            fmt.build(b"\x00" * (fmt.MAX_PAYLOAD + 1),
                      fmt.EXEC_BASE, 1, 0, 0x1)

    def test_rifiuta_un_payload_vuoto(self):
        with self.assertRaises(ValueError):
            fmt.build(b"", fmt.EXEC_BASE, 1, 0, 0x1)

    def test_entry_vtor_segue_l_header(self):
        hdr = fmt.build(b"\x01" * 16, fmt.EXEC_BASE, 1, 0, 0x1)
        self.assertEqual(hdr.entry_vtor, fmt.EXEC_BASE + fmt.HEADER_SIZE)
        self.assertEqual(hdr.entry_vtor & 0x1FF, 0)

    def test_header_lungo_512_byte(self):
        hdr = fmt.build(b"\x01" * 16, fmt.EXEC_BASE, 1, 0, 0x1)
        self.assertEqual(len(hdr.pack()), fmt.HEADER_SIZE)
        self.assertEqual(len(hdr.signed_region()), fmt.SIGNED_LEN)

    def test_round_trip_versione(self):
        self.assertEqual(fmt.parse_fw_version("1.4.2.117"), 0x01040275)
        self.assertEqual(fmt.format_fw_version(0x01040275), "1.4.2.117")

    def test_versione_malformata(self):
        for bad in ("1.2.3", "1.2.3.4.5", "1.2.3.256", "x.y.z.w"):
            with self.subTest(versione=bad):
                with self.assertRaises(ValueError):
                    fmt.parse_fw_version(bad)


class CHeaderConsistencyTest(unittest.TestCase):
    """
    Le costanti Python devono restare allineate a quelle C.

    Se divergono, il tool firma su un layout e il bootloader ne verifica un
    altro: le immagini verrebbero rifiutate solo sul target, dove il guasto è
    molto più caro da diagnosticare.
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
            expr = re.sub(r"(\d)u\b", r"\1", value.strip().rstrip("u"))
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
        self.assertEqual(d["SBL_PUBKEY_LEN"], fmt.PUBKEY_LEN)

    def test_geometria_della_flash(self):
        d = self._defines("inc/image_header.h", "inc/memory_map.h")
        self.assertEqual(d["SBL_EXEC_BASE"], fmt.EXEC_BASE)
        self.assertEqual(d["SBL_EXEC_SIZE"], fmt.EXEC_SIZE)
        self.assertEqual(d["SBL_STAGE_BASE"], fmt.STAGE_BASE)
        self.assertEqual(d["SBL_STAGE_SIZE"], fmt.STAGE_SIZE)
        self.assertEqual(d["SBL_MAX_PAYLOAD"], fmt.MAX_PAYLOAD)
        self.assertEqual(d["SBL_APP_VTOR"], fmt.APP_VTOR)

    def test_codici_di_esito(self):
        """L'enum C e le costanti Python devono coincidere."""
        with open(os.path.join(_ROOT, "inc/image_header.h"),
                  encoding="utf-8") as fh:
            text = fh.read()
        found = dict(re.findall(r"SBL_(OK|ERR_\w+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)",
                                text))
        self.assertTrue(found, "nessun codice di esito trovato nell'header")
        for name, value in found.items():
            with self.subTest(codice=name):
                self.assertEqual(int(value, 0), getattr(fmt, name))


if __name__ == "__main__":
    unittest.main(verbosity=2)
