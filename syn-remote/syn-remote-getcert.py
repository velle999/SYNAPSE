#!/usr/bin/env python3
"""syn-remote-getcert — fetch a VNC server's TLS certificate, as PEM on stdout.

⛔ WHY THIS CANNOT BE `openssl s_client`. VNC does not start in TLS. The
server opens with the RFB version banner in the clear, the two sides agree a
protocol version, the server lists its security types, the client picks
VeNCrypt, they agree a VeNCrypt version, the client picks a subtype — and only
THEN does the TLS session begin. s_client speaks TLS from the first byte and
its -starttls list has no VNC in it, so there is no way to reach the
certificate with the tools already in the package.

What it is for: `syn-remote trust`. wayvnc's certificate is self-signed, so a
client has nothing to validate it against until it has been handed the
certificate itself — trust on first use, with the fingerprint shown to a human
before anything is stored.

⚠ IT DELIBERATELY DOES NOT VERIFY. Verifying here would be circular: the whole
point is to obtain the certificate that verification would need. The check that
matters happens in front of a person, against the printed fingerprint, and
every later connection validates against what they accepted.

SynapseOS Project
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import socket
import ssl
import struct
import sys

RFB_VERSION = b"RFB 003.008\n"
SEC_VENCRYPT = 19
VENCRYPT_X509PLAIN = 262


def fetch(host, port, timeout):
    s = socket.create_connection((host, port), timeout=timeout)
    try:
        banner = s.recv(12)
        if not banner.startswith(b"RFB "):
            raise RuntimeError("not a VNC server — it did not send an RFB banner")
        s.sendall(RFB_VERSION)

        n = s.recv(1)
        if not n:
            raise RuntimeError("the server closed the connection during the handshake")
        n = n[0]
        if n == 0:
            # RFB says a zero count is followed by a reason string.
            reason = s.recv(4096).decode("utf-8", "replace").strip()
            raise RuntimeError("the server refused the connection: " + reason)
        types = s.recv(n)
        if SEC_VENCRYPT not in types:
            raise RuntimeError(
                "the server does not offer VeNCrypt, so it has no certificate to "
                "fetch (it offered: %s)" % ", ".join(str(t) for t in types))

        s.sendall(bytes([SEC_VENCRYPT]))
        ver = s.recv(2)
        s.sendall(bytes([ver[0], ver[1]]))
        if s.recv(1)[0] != 0:
            raise RuntimeError("the server rejected the VeNCrypt version")

        m = s.recv(1)[0]
        raw = s.recv(4 * m)
        subtypes = [struct.unpack(">I", raw[i * 4:(i + 1) * 4])[0] for i in range(m)]
        if VENCRYPT_X509PLAIN not in subtypes:
            raise RuntimeError(
                "the server offers no X509 VeNCrypt subtype, so it presents no "
                "certificate (it offered: %s)" % ", ".join(str(t) for t in subtypes))
        s.sendall(struct.pack(">I", VENCRYPT_X509PLAIN))
        if s.recv(1)[0] != 1:
            raise RuntimeError("the server rejected X509Plain")

        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        tls = ctx.wrap_socket(s)
        der = tls.getpeercert(binary_form=True)
        tls.close()
        s = None
        return der
    finally:
        if s is not None:
            s.close()


def main():
    ap = argparse.ArgumentParser(add_help=True, description=__doc__.split("\n")[0])
    ap.add_argument("host")
    ap.add_argument("port", nargs="?", type=int, default=5900)
    ap.add_argument("--timeout", type=float, default=8.0)
    ap.add_argument("--fingerprint", action="store_true",
                    help="print only the SHA-256 fingerprint")
    a = ap.parse_args()

    try:
        der = fetch(a.host, a.port, a.timeout)
    except (OSError, RuntimeError, IndexError, struct.error) as e:
        # ⚠ One sentence to stderr, never a traceback: this runs behind
        # `syn-remote trust`, and a Python stack trace is not a thing to show
        # somebody whose desktop will not connect.
        sys.stderr.write("syn-remote-getcert: %s\n" % e)
        return 1

    if a.fingerprint:
        print(hashlib.sha256(der).hexdigest())
    else:
        sys.stdout.write(ssl.DER_cert_to_PEM_cert(der))
    return 0


if __name__ == "__main__":
    sys.exit(main())
