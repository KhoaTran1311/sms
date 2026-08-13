# Phase 0 Demo — Two Terminals, One Message, No Internet

> **Run time:** under one minute once built (first build ~5 min incl. vcpkg ports).
> **Requires:** three terminals in this repo root, on the same machine.
> This is the script the Phase 0 review (P0-16) executes step by step.
> Everything below assumes the pinned defaults: `--host 127.0.0.1 --port 9000` on both binaries.

## Prereqs

- macOS or Linux with the vendored vcpkg submodule checked out:
  ```sh
  git submodule update --init --recursive
  ```
- Ninja (build tool)
- CMake >= 3.27 and a C++20 compiler (Clang or GCC)

## Build (first time ~5 min incl. vcpkg ports; cached after)

```sh
cmake --preset dev && cmake --build --preset dev
```

Crib sheet: `serverd` = `build/dev/apps/serverd/serverd`, `sms-cli` = `build/dev/apps/client-cli/sms-cli`.

## Terminal 1 — server

```sh
./build/dev/apps/serverd/serverd
```

Expected:

```
<timestamp> [info] [sms] serverd listening on 127.0.0.1:9000
<timestamp> [info] [sms] session 1 connected from 127.0.0.1:XXXXX
<timestamp> [info] [sms] session 2 connected from 127.0.0.1:XXXXX
```

(The session lines appear as each client connects; leave this terminal running.)

For an annotated run, `serverd --log-level debug` adds per-frame relay detail.

## Terminal 2 — client A

```sh
./build/dev/apps/client-cli/sms-cli
```

Expected:

```
connecting to 127.0.0.1:9000 ...
connected. type a message; /quit to leave.
>
```

## Terminal 3 — client B

```sh
./build/dev/apps/client-cli/sms-cli
```

Expected (same as terminal 2):

```
connecting to 127.0.0.1:9000 ...
connected. type a message; /quit to leave.
>
```

## The moment

In terminal 2, type and press Enter:

```
hello enclave
```

- Terminal 3 shows:  `[<HH:MM:SS>] hello enclave`
- Terminal 2 shows:  `>` (echo + prompt — no duplicate of your own message)
- Terminal 1 logs:  nothing new — the relay is implicit in the PoC (no relay log line by design; see "What you're seeing" below)

That's the whole product in one line: a message typed on one machine, delivered to another, with no internet involved.

## The no-internet proof

- Disable WiFi (and ethernet) on the machine — the network interfaces drop.
- Repeat the steps above on loopback (`127.0.0.1`): server up, both clients connect, message relays.
- Everything still works. That is the product: the enclave is self-contained; there is no cloud dependency to lose.

## Teardown

| Terminal | Action | Expected | Exit code |
|---|---|---|---|
| 2 (client A) | type `/quit` | prints `bye` | 0 |
| 3 (client B) | press `Ctrl-D` (EOF) | prints `bye` | 0 |
| 1 (server) | press `Ctrl-C` | logs `session N disconnected` (per client) then `serverd exited cleanly` | 0 |

## Troubleshooting

- **"address already in use"** — another `serverd` is running. `pkill -f serverd`, or re-run both binaries with `--port 9100`.
- **"connect failed: connection refused"** — the server isn't up yet. Start terminal 1 first and wait for the `listening` line.
- **Client log file** — the client writes diagnostics to `sms-client.log` (file-only by design; nothing on stderr beyond the prompt).

## What you're seeing

This is Phase 0 — a proof of concept, deliberately bare:

- A single **unencrypted TCP transport** with a frame codec — no TLS, no authentication, no encryption. Anyone on the LAN can currently connect and read the room.
- A **single shared room** — everyone relays to everyone; no private conversations, no identity.
- **In-memory relay only** — nothing is persisted; restart the server and the history is gone.
- **No security at all** — no PKI, no accounts, no permissions, no audit.

Phase 1 (Core Protocol) builds the real foundation on top of this skeleton: a protobuf protocol, multi-room messaging, SQLite persistence on the server, history sync, and reconnect with an offline queue. Phase 2 adds the enclave security story: internal PKI, TLS 1.3, Argon2 auth, permissions, and the hash-chained audit log.

What this demo proves — and the reason it matters — is the transport premise the whole product stands on: **two terminals, one message, relayed through a local daemon, with zero internet**. Everything in Phase 1+ is layered onto exactly that.
