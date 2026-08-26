"""
Formato dell'immagine firmware e verificatore di riferimento.

Specifica: docs/02-image-format.md
Le costanti devono restare allineate a inc/image_header.h e inc/memory_map.h.

Questo modulo è il riferimento di comportamento per l'implementazione C del
bootloader: la sequenza di verifica qui sotto segue gli stessi dodici passi,
nello stesso ordine, e restituisce gli stessi codici di errore.
"""

import hashlib
import struct

# --- formato dell'header (inc/image_header.h) -------------------------------

MAGIC = 0x314C4253  # 'SBL1' little-endian
HEADER_VERSION = 1
HEADER_SIZE = 512
SIGNED_LEN = 448
SHA256_LEN = 32
SIGNATURE_LEN = 64
PUBKEY_LEN = 64
RESERVED_LEN = 384

_STRUCT = struct.Struct("<IHHIIIIII32s384s64s")
assert _STRUCT.size == HEADER_SIZE

# --- geometria della flash (inc/memory_map.h) -------------------------------

EXEC_BASE = 0x0800C000
EXEC_SIZE = 100 * 1024
STAGE_BASE = 0x08025000
STAGE_SIZE = 100 * 1024
MAX_PAYLOAD = EXEC_SIZE - HEADER_SIZE
APP_VTOR = EXEC_BASE + HEADER_SIZE

# --- esiti (sbl_verify_result_t) --------------------------------------------

OK = 0x5A3CC3A5
ERR_MAGIC = 1
ERR_HEADER_VERSION = 2
ERR_HEADER_SIZE = 3
ERR_IMAGE_SIZE = 4
ERR_LOAD_ADDRESS = 5
ERR_ENTRY_VTOR = 6
ERR_ROOT_KEY = 7
ERR_SIGNATURE = 8
ERR_SECURITY_VERSION = 9
ERR_PRODUCT_ID = 10
ERR_PAYLOAD_HASH = 11

_ERR_NAMES = {
    ERR_MAGIC: "magic assente o corrotto",
    ERR_HEADER_VERSION: "versione del formato non supportata",
    ERR_HEADER_SIZE: "header_size diverso da 512",
    ERR_IMAGE_SIZE: "payload nullo o oltre la capienza dell'area",
    ERR_LOAD_ADDRESS: "load_address non è l'area attesa",
    ERR_ENTRY_VTOR: "entry_vtor fuori area o non allineato a 512",
    ERR_ROOT_KEY: "la chiave pubblica non corrisponde all'hash in OTP",
    ERR_SIGNATURE: "firma ECDSA non valida",
    ERR_SECURITY_VERSION: "rollback a una versione precedente",
    ERR_PRODUCT_ID: "immagine destinata a un altro prodotto",
    ERR_PAYLOAD_HASH: "payload alterato",
}


class VerifyError(Exception):
    """Immagine rifiutata. `step` è il passo di docs/02-image-format.md."""

    def __init__(self, step, code):
        self.step = step
        self.code = code
        super().__init__(f"passo {step}: {_ERR_NAMES[code]}")


class Header:
    __slots__ = (
        "magic", "header_version", "header_size", "image_size",
        "load_address", "entry_vtor", "security_version",
        "firmware_version", "product_id", "payload_sha256",
        "reserved", "signature",
    )

    def __init__(self, **kw):
        for name in self.__slots__:
            setattr(self, name, kw.get(name))

    def pack(self):
        return _STRUCT.pack(
            self.magic, self.header_version, self.header_size,
            self.image_size, self.load_address, self.entry_vtor,
            self.security_version, self.firmware_version, self.product_id,
            self.payload_sha256, self.reserved, self.signature,
        )

    @classmethod
    def unpack(cls, blob):
        if len(blob) < HEADER_SIZE:
            raise VerifyError(1, ERR_MAGIC)
        f = _STRUCT.unpack(blob[:HEADER_SIZE])
        return cls(
            magic=f[0], header_version=f[1], header_size=f[2],
            image_size=f[3], load_address=f[4], entry_vtor=f[5],
            security_version=f[6], firmware_version=f[7], product_id=f[8],
            payload_sha256=f[9], reserved=f[10], signature=f[11],
        )

    def signed_region(self):
        """I 448 byte coperti dalla firma."""
        return self.pack()[:SIGNED_LEN]


def format_fw_version(value):
    return "%d.%d.%d.%d" % (
        (value >> 24) & 0xFF, (value >> 16) & 0xFF,
        (value >> 8) & 0xFF, value & 0xFF,
    )


def parse_fw_version(text):
    parts = text.split(".")
    if len(parts) != 4:
        raise ValueError("la versione va scritta come MM.mm.pp.bb")
    out = 0
    for p in parts:
        n = int(p, 0)
        if not 0 <= n <= 255:
            raise ValueError(f"campo di versione fuori range: {p}")
        out = (out << 8) | n
    return out


def build(payload, load_address, security_version, firmware_version,
          product_id, entry_vtor=None):
    """Compone l'header con la firma azzerata. Da firmare con `sign`."""
    if not payload:
        raise ValueError("payload vuoto")
    if len(payload) > MAX_PAYLOAD:
        raise ValueError(
            f"payload di {len(payload)} byte: supera i {MAX_PAYLOAD} "
            f"disponibili nell'area di esecuzione"
        )
    return Header(
        magic=MAGIC,
        header_version=HEADER_VERSION,
        header_size=HEADER_SIZE,
        image_size=len(payload),
        load_address=load_address,
        entry_vtor=load_address + HEADER_SIZE if entry_vtor is None
        else entry_vtor,
        security_version=security_version,
        firmware_version=firmware_version,
        product_id=product_id,
        payload_sha256=hashlib.sha256(payload).digest(),
        reserved=b"\xFF" * RESERVED_LEN,
        signature=b"\x00" * SIGNATURE_LEN,
    )


def verify(image, root_pubkey, otp_pubkey_hash, otp_security_version,
           device_product_id, area_base=EXEC_BASE, area_size=EXEC_SIZE,
           verify_signature=None):
    """
    Verificatore di riferimento: i dodici passi di docs/02-image-format.md.

    `verify_signature(pubkey, message, signature) -> bool` è iniettato per
    tenere questo modulo indipendente dalla libreria crittografica.

    Solleva VerifyError al primo passo che fallisce, altrimenti restituisce OK.
    """
    hdr = Header.unpack(image)

    # 1-3: struttura
    if hdr.magic != MAGIC:
        raise VerifyError(1, ERR_MAGIC)
    if hdr.header_version != HEADER_VERSION:
        raise VerifyError(2, ERR_HEADER_VERSION)
    if hdr.header_size != HEADER_SIZE:
        raise VerifyError(3, ERR_HEADER_SIZE)

    # 4-6: limiti. Vengono prima di ogni lettura dimensionata dall'header:
    # un image_size gonfiato al passo 11 leggerebbe fuori dall'area.
    max_payload = area_size - HEADER_SIZE
    if hdr.image_size == 0 or hdr.image_size > max_payload:
        raise VerifyError(4, ERR_IMAGE_SIZE)
    # image_size deve stare anche nei byte effettivamente leggibili. Sul
    # target l'area di flash è sempre leggibile per intero, quindi questo
    # controllo coincide con quello sopra; qui e in ogni contesto in cui il
    # buffer è più corto dell'area, è ciò che impedisce al passo 11 di
    # leggere oltre la fine del buffer.
    if hdr.image_size > len(image) - HEADER_SIZE:
        raise VerifyError(4, ERR_IMAGE_SIZE)
    if hdr.load_address != area_base:
        raise VerifyError(5, ERR_LOAD_ADDRESS)
    if not (area_base <= hdr.entry_vtor < area_base + area_size) \
            or (hdr.entry_vtor & 0x1FF) != 0:
        raise VerifyError(6, ERR_ENTRY_VTOR)

    # 7: la radice di fiducia è l'hash in OTP, non la chiave in flash.
    # Verificare una firma con una chiave non autenticata non dimostra nulla.
    if hashlib.sha256(root_pubkey).digest() != otp_pubkey_hash:
        raise VerifyError(7, ERR_ROOT_KEY)

    # 8: firma sui primi 448 byte dell'header
    if not verify_signature(root_pubkey, hdr.signed_region(), hdr.signature):
        raise VerifyError(8, ERR_SIGNATURE)

    # 9-10: campi attendibili solo ora che la firma li ha coperti
    if hdr.security_version < otp_security_version:
        raise VerifyError(9, ERR_SECURITY_VERSION)
    if hdr.product_id != device_product_id:
        raise VerifyError(10, ERR_PRODUCT_ID)

    # 11: il più costoso per ultimo, ~100 KB da digerire
    payload = image[HEADER_SIZE:HEADER_SIZE + hdr.image_size]
    if hashlib.sha256(payload).digest() != hdr.payload_sha256:
        raise VerifyError(11, ERR_PAYLOAD_HASH)

    # 12: il chiamante imposta VTOR e salta
    return OK
