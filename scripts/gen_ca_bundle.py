#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Paquet de certificats racines pour la mise à jour en ligne (bibliothèque standard seulement).

La boîte télécharge ses mises à jour sur GitHub en HTTPS et doit vérifier le certificat du serveur. Plutôt que
de figer une ou deux autorités (GitHub en change), elle embarque toutes les autorités racines de Mozilla, au
format de esp_crt_bundle (WiFiClientSecure::setCACertBundle): nombre de certificats sur 2 octets, puis pour
chacun la longueur du nom et de la clé (2 octets chacune), le nom du sujet (DER) et sa clé publique (DER), triés
par nom.

  curl -o cacert.pem https://curl.se/ca/cacert.pem      # liste de Mozilla, extraite par curl (MPL-2.0)
  python3 scripts/gen_ca_bundle.py cacert.pem src/certs/x509_crt_bundle.bin

À refaire de temps en temps (autorités ajoutées ou retirées), puis recompiler et publier une version.
"""

import base64
import re
import sys
from pathlib import Path


def read_tlv(data: bytes, offset: int) -> tuple:
    """(début de l'élément, début du contenu, fin) de l'élément DER qui commence à `offset`."""
    length = data[offset + 1]
    content = offset + 2
    if length & 0x80:
        count = length & 0x7F
        length = int.from_bytes(data[content:content + count], "big")
        content += count
    return offset, content, content + length


def subject_and_key(der: bytes) -> tuple:
    """Nom du sujet et clé publique (SubjectPublicKeyInfo), en DER, d'un certificat X.509."""
    _, certificate, _ = read_tlv(der, 0)
    _, field, tbs_end = read_tlv(der, certificate)
    fields = []
    while field < tbs_end:
        start, _, end = read_tlv(der, field)
        fields.append(der[start:end])
        field = end
    if fields[0][0] == 0xA0:  # [0] version, présente en X.509 v3
        fields = fields[1:]
    # numéro de série, algorithme de signature, émetteur, validité, sujet, clé publique
    return fields[4], fields[5]


def bundle(pem: str) -> bytes:
    certificates = []
    for block in re.findall(r"-----BEGIN CERTIFICATE-----(.+?)-----END CERTIFICATE-----", pem, re.S):
        certificates.append(subject_and_key(base64.b64decode("".join(block.split()))))
    certificates.sort(key=lambda item: item[0])
    out = bytearray(len(certificates).to_bytes(2, "big"))
    for subject, key in certificates:
        out += len(subject).to_bytes(2, "big") + len(key).to_bytes(2, "big") + subject + key
    return bytes(out)


def main(argv: list) -> int:
    if len(argv) != 2:
        print(__doc__)
        return 2
    data = bundle(Path(argv[0]).read_text(encoding="utf-8"))
    Path(argv[1]).parent.mkdir(parents=True, exist_ok=True)
    Path(argv[1]).write_bytes(data)
    print(f"{argv[1]}: {int.from_bytes(data[:2], 'big')} autorités, {len(data)} octets")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
