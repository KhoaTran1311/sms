# SMS — Enclaved LAN Messaging

A messaging app that lives entirely inside your LAN. Built for regulated industries (defense, banking, legal) where data may not leave firm-controlled hardware: every byte — messages, presence, files, metadata — travels and is stored inside the firm's network. No cloud, no external egress, no third-party services.

**Current status: Phase 0 — Proof of Concept (in progress).**

## Why an "enclave"?

Regulated firms often cannot use SaaS messaging: compliance rules require that communication data never leaves firm-controlled infrastructure. SMS inverts the usual architecture — instead of a cloud service, a small **local server daemon** runs on a firm-controlled machine, and clients connect over the local network. Everything is self-contained: the internal PKI, the message store, the audit trail, and the retention policies live and die inside the enclave.

## Core design

- **Hub-and-spoke topology** — one lightweight server daemon per deployment. Central history, audit, retention, and administration are far simpler than a peer-to-peer mesh, which suits regulated deployments.
- **CLI clients first** — fastest path to a working enclave; the client core is UI-agnostic so a Qt GUI can be added later without protocol rewrites.
- **TLS-only transport** — all traffic is authenticated and encrypted with an internal PKI (Phase 2); nothing is ever sent in the clear.
- **Server-side history** — SQLite-backed storage enables the compliance features firms actually need: audit, retention, DLP, eDiscovery.
- **Air-gapped by construction** — all dependencies are vendored via a pinned vcpkg submodule; builds resolve identical port versions on every machine with zero network dependency.

## Feature roadmap

| Phase | Theme | Deliverable |
|---|---|---|
| **Phase 0** | Proof of Concept | Repo bootstrap, TCP transport, frame codec, single-room relay, CLI REPL |
| **Phase 1** | Core Protocol | Protobuf protocol, multi-room, SQLite persistence, history sync, reconnect + offline queue |
| **Phase 2** | Security & Enclave | Internal PKI, TLS 1.3, Argon2 auth, permissions, hash-chained audit log |
| **Phase 3** | Compliance | Retention policies, signed eDiscovery export, DLP, admin CLI |
| **Phase 4** | Hardening & GUI | Fuzzing, at-rest encryption, SIEM logging, Qt GUI, packaging |

Phases are strictly sequential — each one's deliverable is the foundation of the next.

## Architecture

```
Client (CLI, C++) ──TLS──► Server daemon (C++, headless) ◄──TLS── Client
                              │
                              ├─ SQLite (WAL): users, rooms, messages, presence
                              ├─ Hash-chained audit log (SHA-256, tamper-evident)
                              └─ Internal CA: cert generation + issuance CLI tool

Discovery: mDNS + config-file fallback for air-gapped hardening
```

- **Transport**: TCP + TLS via OpenSSL, messages as protobuf in length-prefixed frames.
- **Storage**: SQLite on the server (history, rooms, users) + small local cache per client.

## Tech stack

| Layer | Choice |
|---|---|
| Language | C++20 |
| Build | CMake presets + vendored vcpkg manifest (offline-capable) |
| Networking | Asio (standalone) |
| Security | OpenSSL (TLS), Argon2 (passwords), SHA-256 (audit chain) |
| Serialization | protobuf |
| Storage | SQLite3 (WAL mode) |
| Logging | spdlog |
| Tests | GoogleTest + integration tests over real loopback sockets |

## Repository layout

```
CMakeLists.txt              # top-level, common/tooling options
CMakePresets.json           # includes cmake/presets.json (dev, ci, release)
cmake/presets.json          # dev, ci, release presets (configure/build/test)
vcpkg.json                  # manifest: dependencies + builtin-baseline
vcpkg/                      # vendored vcpkg (git submodule, pinned commit)
src/
  common/                   # Result<T>, ErrorCode, logging helpers
  net/                      # frame codec, TLS sessions
  server/                   # session manager, room manager, persistence
  client/                   # client core, connection manager
  security/                 # internal CA tooling, audit chain (Phase 2)
apps/
  serverd/                  # headless daemon entry point
  client-cli/               # CLI REPL entry point
tests/                      # unit + integration test targets
docs/                       # implementation plan, phase docs, tickets
```

Namespaces mirror the layout: `sms::common`, `sms::net`, `sms::server`, `sms::client`, `sms::security` (reserved).

## Building

### Prerequisites

- CMake >= 3.27
- Ninja
- A C++20 compiler (Clang or GCC)
- Git

### Dependencies

All dependencies are resolved from the vendored vcpkg submodule. Initialize it once:

```sh
git submodule update --init --recursive
```

The submodule is pinned to the same commit recorded in `vcpkg.json` (`builtin-baseline`), so every machine — dev, CI, customer — resolves identical port versions. This is the air-gapped guarantee.

### Configure, build, test

```sh
cmake --preset dev          # configure (Debug, tests on)
cmake --build --preset dev  # build
ctest --preset dev          # run tests
```

Other presets:

```sh
cmake --preset ci           # Release with tests, approximates CI
cmake --preset release      # Release, tests stripped
```

### Using the binaries

```sh
build/dev/apps/serverd/serverd      # start the server daemon
build/dev/apps/client-cli/client-cli  # connect a client
```

(CLI surface lands in Phase 0 tickets P0-10 / P0-13.)

## Testing

Unit tests use GoogleTest; integration tests run against real loopback sockets. `ctest --preset dev` runs everything. Tests are gated on `SMS_BUILD_TESTS` so release builds skip them.

## Documentation

- [`docs/implementation-plan.md`](docs/implementation-plan.md) — locked architectural decisions and build order
- [`docs/phases/`](docs/phases/) — per-phase write-ups: planning, tasks, exit criteria, risks
- [`docs/phase-0/`](docs/phase-0/) — Phase 0 tickets, with completed ones renamed to `DONE_*.md`

## Status

- [x] P0-01 — Repo bootstrap: CMake presets, vendored vcpkg manifest, layered source layout
- [x] P0-02 — Logging infrastructure (spdlog)
- [x] P0-03 — Frame codec
- [x] P0-04 — Frame codec tests
- [x] P0-05 — Error handling (`Result<T>`)
- [x] P0-06 — CI pipeline
- [x] P0-07 — Formatting hygiene
- [x] P0-08 — Server session
- [x] P0-09 — Server relay
- [x] P0-10 — Server daemon
- [x] P0-11 — Server integration tests
- [x] P0-12 — Client core
- [x] P0-13 — Client CLI REPL
- [x] P0-14 — Client integration tests
- [x] P0-15 — Demo script
- [ ] P0-16 — README & phase review
