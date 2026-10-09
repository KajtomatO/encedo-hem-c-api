#!/usr/bin/env python3
"""Mint a VALID self-signed CA and an EXPIRED leaf certificate it signed, for
localhost (REQ-BUILD-005 / REQ-NET-005 check in the release workflow): the
release binary must classify "certificate has expired" under the wolfSSL-backed
libcurl, so a local HTTPS endpoint serving the expired leaf is probed by
`hem-tool recovery` with the CA as its trust anchor; it prints "diagnosis: the
device certificate has EXPIRED" only when the classifier fired.

The CA must be valid: wolfSSL (unlike a lax backend) refuses to even LOAD an
expired certificate as a trust anchor, so a self-signed expired cert cannot be
its own CA in this check. Writes ca.pem, expired-cert.pem, expired-key.pem into
the given directory. Needs the `cryptography` package.
"""
import datetime
import os
import sys

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID

out = sys.argv[1] if len(sys.argv) > 1 else "."
now = datetime.datetime.now(datetime.timezone.utc)

ca_key = ec.generate_private_key(ec.SECP256R1())
ca_name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "hem-release-check CA")])
ca = (
    x509.CertificateBuilder()
    .subject_name(ca_name).issuer_name(ca_name)
    .public_key(ca_key.public_key())
    .serial_number(x509.random_serial_number())
    .not_valid_before(now - datetime.timedelta(days=1))
    .not_valid_after(now + datetime.timedelta(days=3650))
    .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True)
    .add_extension(x509.KeyUsage(digital_signature=True, key_cert_sign=True, crl_sign=True,
                                 content_commitment=False, key_encipherment=False,
                                 data_encipherment=False, key_agreement=False,
                                 encipher_only=False, decipher_only=False), critical=True)
    .sign(ca_key, hashes.SHA256())
)

leaf_key = ec.generate_private_key(ec.SECP256R1())
leaf_name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "localhost")])
leaf = (
    x509.CertificateBuilder()
    .subject_name(leaf_name).issuer_name(ca_name)
    .public_key(leaf_key.public_key())
    .serial_number(x509.random_serial_number())
    .not_valid_before(now - datetime.timedelta(days=400))
    .not_valid_after(now - datetime.timedelta(days=30))      # expired a month ago
    .add_extension(x509.SubjectAlternativeName([x509.DNSName("localhost")]), critical=False)
    .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
    .sign(ca_key, hashes.SHA256())
)

with open(os.path.join(out, "ca.pem"), "wb") as f:
    f.write(ca.public_bytes(serialization.Encoding.PEM))
with open(os.path.join(out, "expired-cert.pem"), "wb") as f:
    f.write(leaf.public_bytes(serialization.Encoding.PEM))
with open(os.path.join(out, "expired-key.pem"), "wb") as f:
    f.write(leaf_key.private_bytes(serialization.Encoding.PEM,
                                   serialization.PrivateFormat.PKCS8,
                                   serialization.NoEncryption()))
print("CA valid until", ca.not_valid_after_utc, "; leaf expired:",
      leaf.not_valid_before_utc, "..", leaf.not_valid_after_utc)
