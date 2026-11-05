#!/usr/bin/env python3
"""Fail-closed checks for this diagnostic only; no device access or writes.

Output format pinned to esptool 5.2.0 cmds.py get_security_info/read_mac.
The expected MAC comes from the recorded board USB serial. If the eFuse MAC
differs, stop and establish identity; never silently accept another device.
"""
import hashlib
from pathlib import Path
import re
import struct
import sys


def check_report(mode, report):
    lines = [" ".join(line.split()) for line in report.splitlines()]
    expected = ({"Secure Boot:": "Secure Boot: Disabled",
                 "Flash Encryption:": "Flash Encryption: Disabled"}
                if mode == "security" else
                {"MAC:": "MAC: 44:b1:76:b6:a5:74"})
    for prefix, wanted in expected.items():
        matches = [line for line in lines if line.startswith(prefix)]
        if mode == "target":
            matches = [line.lower() for line in matches]
            wanted = wanted.lower()
            # esptool prints the MAC at connect and again for read-mac.
            if matches and all(line == wanted for line in matches):
                continue
        if matches != [wanted]:
            raise ValueError("Identité/sécurité non confirmée : " + prefix)


def check_partitions(data):
    expected = [
        (1, 2, 0x9000, 0x5000, b"nvs", 0),
        (1, 0, 0xe000, 0x2000, b"otadata", 0),
        (0, 16, 0x10000, 0x140000, b"app0", 0),
        (0, 17, 0x150000, 0x140000, b"app1", 0),
        (1, 130, 0x290000, 0x160000, b"spiffs", 0),
        (1, 3, 0x3f0000, 0x10000, b"coredump", 0),
    ]
    if len(data) != 3072:
        raise ValueError("Taille de table inattendue")
    for i, wanted in enumerate(expected):
        magic, kind, subtype, offset, size, name, flags = struct.unpack_from(
            "<HBBII16sI", data, i * 32)
        if magic != 0x50aa or (kind, subtype, offset, size,
                               name.rstrip(b"\0"), flags) != wanted:
            raise ValueError("Partition différente de la cible diagnostic")
    end = len(expected) * 32
    if data[end:end + 16] != b"\xeb\xeb" + b"\xff" * 14:
        raise ValueError("Empreinte MD5 de table absente")
    if data[end + 16:end + 32] != hashlib.md5(data[:end]).digest():
        raise ValueError("Empreinte MD5 de table incorrecte")
    if data[end + 32:] != b"\xff" * (len(data) - end - 32):
        raise ValueError("Partitions supplémentaires inattendues")


def main():
    try:
        if len(sys.argv) == 3 and sys.argv[1] == "partitions":
            with Path(sys.argv[2]).open("rb") as source:
                check_partitions(source.read(4097))
        elif len(sys.argv) == 2 and sys.argv[1] in ("security", "target"):
            report = sys.stdin.read(65537)
            if len(report) > 65536 or re.search(r"\x1b|\x00", report):
                raise ValueError("Rapport invalide ou trop grand")
            check_report(sys.argv[1], report)
        else:
            raise ValueError("Usage : check-diagnostic-preflight.py security|target|partitions fichier")
    except (ValueError, OSError) as exc:
        print("Arrêt avant écriture : " + str(exc), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
