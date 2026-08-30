#!/usr/bin/env python3
"""A token endpoint that behaves like Google's, and can be made to refuse.

⛔ NOT A MOCK OF WHAT THE CLIENT SENDS. It CHECKS what the client sends — that
the grant type is right, that a code exchange carries a code_verifier, that a
refresh carries a refresh_token — and answers 400 with a provider-shaped error
when it does not. A fake that accepts anything proves only that the client can
talk to itself.

SynapseOS Project — GPL-2.0-or-later
"""
import json
import sys
import urllib.parse
from http.server import BaseHTTPRequestHandler, HTTPServer


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        form = urllib.parse.parse_qs(self.rfile.read(n).decode())
        get = lambda k: (form.get(k) or [""])[0]

        # ⛔ GOOGLE DEMANDS A SECRET FROM AN INSTALLED APP, so the fake does too
        # — for one client id, so both halves stay testable. syn-cal shipped a
        # sign-in that passed consent and died here on exactly this error, and
        # a fake that never asked for the secret is why nothing caught it.
        # The other client id models a public client that must NOT be sent one.
        if get("client_id") == "secret-client" and not get("client_secret"):
            return self.fail("invalid_request", "client_secret is missing.")
        if get("client_id") == "public-client" and get("client_secret"):
            return self.fail("invalid_request",
                             "client_secret is not allowed for a public client")

        grant = get("grant_type")
        if grant == "authorization_code":
            if not get("code_verifier"):
                return self.fail("invalid_request", "PKCE code_verifier missing")
            if not get("code"):
                return self.fail("invalid_request", "no code")
            body = {"access_token": "fake-access-1", "refresh_token": "fake-refresh-1",
                    "expires_in": 3599, "token_type": "Bearer"}
        elif grant == "refresh_token":
            tok = get("refresh_token")
            if not tok:
                return self.fail("invalid_request", "no refresh_token")
            if tok == "REFUSE":
                return self.fail("invalid_grant", "Token has been expired or revoked.")
            # ⚠ No refresh_token in the answer — the common case, and the one
            # that logs an account out an hour later if the client overwrites.
            body = {"access_token": "fake-access-2", "expires_in": 3599,
                    "token_type": "Bearer"}
        else:
            return self.fail("unsupported_grant_type", grant or "(none)")

        self.reply(200, body)

    def fail(self, err, desc):
        self.reply(400, {"error": err, "error_description": desc})

    def reply(self, code, obj):
        raw = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def log_message(self, *a):
        pass


if __name__ == "__main__":
    srv = HTTPServer(("127.0.0.1", 0), Handler)
    print(srv.server_address[1], flush=True)
    srv.serve_forever()
