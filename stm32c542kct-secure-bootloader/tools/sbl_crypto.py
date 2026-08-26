"""
Wrapper ECDSA P-256 sopra `cryptography`.

Isolato in un modulo a parte perché sul target queste operazioni le fa
X-CUBE-CRYPTOLIB: qui serve solo per firmare in fase di build e per far
girare i test su PC.
"""

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils

CURVE = ec.SECP256R1()
COORD_LEN = 32


def generate_key():
    return ec.generate_private_key(CURVE)


def load_private_key(path):
    with open(path, "rb") as fh:
        key = serialization.load_pem_private_key(fh.read(), password=None)
    if not isinstance(key.curve, ec.SECP256R1):
        raise ValueError("la chiave non è su curva P-256")
    return key


def save_private_key(key, path):
    pem = key.private_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PrivateFormat.PKCS8,
        encryption_algorithm=serialization.NoEncryption(),
    )
    with open(path, "wb") as fh:
        fh.write(pem)


def public_key_raw(key):
    """Chiave pubblica come X || Y, 64 byte. È il formato che sta in flash."""
    pub = key.public_key() if hasattr(key, "public_key") else key
    numbers = pub.public_numbers()
    return (numbers.x.to_bytes(COORD_LEN, "big")
            + numbers.y.to_bytes(COORD_LEN, "big"))


def public_key_from_raw(raw):
    if len(raw) != 2 * COORD_LEN:
        raise ValueError("la chiave pubblica raw deve essere di 64 byte")
    return ec.EllipticCurvePublicNumbers(
        int.from_bytes(raw[:COORD_LEN], "big"),
        int.from_bytes(raw[COORD_LEN:], "big"),
        CURVE,
    ).public_key()


def sign(private_key, message):
    """Firma `message`. Restituisce r || s, 64 byte."""
    der = private_key.sign(message, ec.ECDSA(hashes.SHA256()))
    r, s = utils.decode_dss_signature(der)
    return r.to_bytes(COORD_LEN, "big") + s.to_bytes(COORD_LEN, "big")


def verify(pubkey_raw, message, signature):
    """Verifica r || s. Restituisce True/False, non solleva."""
    if len(signature) != 2 * COORD_LEN:
        return False
    r = int.from_bytes(signature[:COORD_LEN], "big")
    s = int.from_bytes(signature[COORD_LEN:], "big")
    if r == 0 or s == 0:
        return False
    der = utils.encode_dss_signature(r, s)
    try:
        public_key_from_raw(pubkey_raw).verify(
            der, message, ec.ECDSA(hashes.SHA256()))
        return True
    except Exception:
        return False
