#!/usr/bin/env python3
"""
Confronto differenziale fra l'implementazione C e il riferimento Python.

    python3 tests/test_differential.py

Compila src/verify.c — il codice vero del bootloader, non una riscrittura —
con un backend crittografico OpenSSL al posto di X-CUBE-CRYPTOLIB, gli dà in
pasto lo stesso corpus di vettori del riferimento Python, e pretende che i due
diano lo **stesso codice di esito su ogni vettore**.

Perché serve. La scelta della cryptolib ST (§3 di docs/00-decisions.md) rende
il codice di verifica non eseguibile su PC, e la mitigazione prevista era
un'interfaccia crittografica sottile con un backend software per i test.
Questo è quel test: senza, il riferimento Python sarebbe un documento e non
una specifica, e nulla garantirebbe che il C si comporti allo stesso modo.

Un disaccordo qui significa che uno dei due ha un bug — molto probabilmente
il C, ma vale la pena guardare entrambi.
"""

import os
import shutil
import subprocess
import sys
import tempfile
import unittest

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.dirname(_HERE)
sys.path.insert(0, _HERE)
sys.path.insert(0, os.path.join(_ROOT, "tools"))

import sbl_crypto
import sbl_format as fmt
import vectors

_HOST_DIR = os.path.join(_HERE, "host")


def _code_name(code):
    for name in dir(fmt):
        if (name == "OK" or name.startswith("ERR_")) \
                and getattr(fmt, name) == code:
            return name
    return "SCONOSCIUTO(%d)" % code


class DifferentialTest(unittest.TestCase):

    binary = None
    tmpdir = None

    @classmethod
    def setUpClass(cls):
        if shutil.which("gcc") is None and shutil.which("cc") is None:
            raise unittest.SkipTest("nessun compilatore C disponibile")

        build = subprocess.run(["make", "-C", _HOST_DIR],
                               capture_output=True, text=True)
        if build.returncode != 0:
            raise unittest.SkipTest(
                "il banco di prova su host non compila (serve libcrypto):\n"
                + build.stderr)
        cls.binary = os.path.join(_HOST_DIR, "run_vectors")

        cls.key = sbl_crypto.generate_key()
        cls.cases = vectors.build_cases(cls.key)
        cls.tmpdir = tempfile.mkdtemp(prefix="sbl-vectors-")
        manifest = vectors.write_corpus(cls.cases, cls.tmpdir)

        run = subprocess.run([cls.binary, manifest, cls.tmpdir],
                             capture_output=True, text=True)
        if run.returncode != 0:
            raise AssertionError(
                "il runner C ha segnalato un errore:\n" + run.stderr)

        cls.c_results = {}
        for line in run.stdout.splitlines():
            name, code = line.rsplit(" ", 1)
            cls.c_results[name] = int(code)

    @classmethod
    def tearDownClass(cls):
        if cls.tmpdir:
            shutil.rmtree(cls.tmpdir, ignore_errors=True)

    def _python_result(self, case):
        try:
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
        except fmt.VerifyError as exc:
            return exc.code

    def test_ogni_vettore_ha_un_esito_dal_c(self):
        mancanti = [c.name for c in self.cases
                    if c.name not in self.c_results]
        self.assertEqual(mancanti, [],
                         "il runner C non ha prodotto un esito per: %s"
                         % mancanti)

    def test_c_e_python_concordano(self):
        for case in self.cases:
            with self.subTest(vettore=case.name):
                py = self._python_result(case)
                c = self.c_results.get(case.name)
                self.assertEqual(
                    c, py,
                    "disaccordo su '%s': il C dice %s, il Python %s"
                    % (case.name, _code_name(c), _code_name(py)))

    def test_entrambi_rispettano_l_esito_atteso(self):
        for case in self.cases:
            with self.subTest(vettore=case.name):
                py = self._python_result(case)
                c = self.c_results.get(case.name)
                self.assertEqual(
                    py, case.expected,
                    "il riferimento Python su '%s' dà %s invece di %s"
                    % (case.name, _code_name(py), _code_name(case.expected)))
                self.assertEqual(
                    c, case.expected,
                    "il C su '%s' dà %s invece di %s"
                    % (case.name, _code_name(c), _code_name(case.expected)))

    def test_il_corpus_copre_tutti_i_codici(self):
        """Ogni passo della specifica deve avere almeno un vettore."""
        attesi = {fmt.OK, fmt.ERR_MAGIC, fmt.ERR_HEADER_VERSION,
                  fmt.ERR_HEADER_SIZE, fmt.ERR_IMAGE_SIZE,
                  fmt.ERR_LOAD_ADDRESS, fmt.ERR_ENTRY_VTOR,
                  fmt.ERR_ROOT_KEY, fmt.ERR_SIGNATURE,
                  fmt.ERR_SECURITY_VERSION, fmt.ERR_PRODUCT_ID,
                  fmt.ERR_PAYLOAD_HASH}
        coperti = {c.expected for c in self.cases}
        scoperti = {_code_name(x) for x in attesi - coperti}
        self.assertEqual(scoperti, set(),
                         "nessun vettore produce: %s" % scoperti)


if __name__ == "__main__":
    unittest.main(verbosity=2)
