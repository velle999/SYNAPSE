/*
 * http_server.h — the llama.cpp-shaped face of synapd.
 *
 * ⛔ WHAT THIS IS FOR. synapd already holds a model in memory; everything that
 * wanted to use it had to speak synapd's own binary protocol over
 * /run/synapd/synapd.sock, which nothing outside this distribution does. So a
 * person with Continue, Open WebUI, aider, or anything else built against
 * llama.cpp's server or the OpenAI shape had to run a SECOND copy of the model
 * to use it — a second multi-gigabyte load, and on a GPU box, VRAM the machine
 * does not have twice.
 *
 * This speaks the subset of that API those clients actually use, over the model
 * that is already resident. It is a translation layer and nothing else: every
 * request ends up in the same inference_run() the socket protocol uses.
 *
 * ── A UNIX SOCKET, NOT A PORT ────────────────────────────────────────────────
 *
 * llama-server listens on 127.0.0.1:8080. This does not, and the difference is
 * deliberate: a loopback port is reachable by every process on the machine —
 * including a page in a browser, which can POST to localhost. The model here
 * answers questions about the machine it runs on, so "any local process" is a
 * wider audience than it sounds. The socket is 0660, group synapse, exactly
 * like the daemon's own; a client that needs a port can be given one by socat
 * or a systemd socket unit, which is then somebody's explicit decision.
 *
 *     curl --unix-socket /run/synapd/http.sock \
 *          http://localhost/v1/chat/completions -d '{"messages":[...]}'
 *
 * ── WHAT IS NOT HERE ─────────────────────────────────────────────────────────
 *
 * ⚠ STREAMING IS ANSWERED IN ONE CHUNK. `"stream": true` gets a real
 * text/event-stream with the real framing and a terminating [DONE], because
 * clients that ask for it break outright without one — but the whole answer
 * arrives in a single event, because inference_run() returns a finished string
 * and there is no token callback to hang a stream on. The content is identical;
 * only the tokens-as-they-come is missing. Making that real means a streaming
 * entry point in inference.c, which is a change to the generation loop rather
 * than to this file.
 *
 * SynapseOS Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * https://github.com/velle999/SYNAPSE
 */
#pragma once

#include "synapd.h"

#ifndef SYNAPD_HTTP_SOCKET_PATH
#define SYNAPD_HTTP_SOCKET_PATH "/run/synapd/http.sock"
#endif

/* Starts the listener and its accept thread. Returns 0 on success. A failure
 * here is logged and is NOT fatal to the daemon: the binary protocol is what
 * the desktop uses, and losing the compatibility face must not take the
 * assistant down with it. */
int  http_server_start(synapd_state_t *s);
void http_server_stop(synapd_state_t *s);
