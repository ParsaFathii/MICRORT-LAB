# Security Policy

MicroRT-Lab — Deterministic Real-Time Operating System & Scheduling Laboratory.

Copyright © 2026 Parsa Fathi — Apache-2.0.

## Supported versions

| Version | Supported |
|---|---|
| `main` branch | yes |

MicroRT-Lab is a laboratory/teaching tool under active development; security fixes
land on `main` and there are no long-term maintenance branches.

## Reporting a vulnerability

Please do **not** open a public issue for security problems. Instead:

- use **GitHub Security Advisories** on the repository
  (https://github.com/ParsaFathii/MICRORT-LAB/security/advisories), or
- contact the repository owner (Parsa Fathi) via GitHub
  (https://github.com/ParsaFathii).

Include reproduction steps, the affected layer (kernel / engine / services/api /
src), and, where relevant, a minimal config JSON. You will receive an
acknowledgement; fixes are released on `main` and credited in the advisory unless
you prefer otherwise.

## Secure configuration

- **The API service binds 127.0.0.1 by design.** MicroRT-Lab is a single-user lab
  tool, not an internet-facing service. Do **not** expose port 3031 publicly (no
  reverse proxy, no `0.0.0.0` bind, no port forwarding). The web workstation talks
  to it on the loopback interface.
- **Engine subprocess execution.** Run endpoints execute the engine binary
  (`micrort-engine`) as a subprocess. A configuration is executed **only after**
  double validation: the pydantic models (field shapes and cross-field rules)
  reject invalid input first, and the engine re-validates the same grammar before
  simulating. Nevertheless, treat configs from untrusted sources as untrusted
  input: validate them standalone with
  `micrort-engine validate --config <file>` before running, and only run the
  service in an environment you control.
- **File paths come from environment variables** (`MICRORT_ENGINE_BIN`,
  `MICRORT_DATA_DIR`, `MICRORT_EXPERIMENTS_DIR`). Point them at paths inside your
  own workspace; nothing else in the service reads or writes arbitrary paths
  (reports are written into the data dir and the requested `--out` of the engine
  CLI in direct use).

## Secret handling

- The repository contains **no secrets, tokens or credentials**, and it is scanned;
  keep it that way. Tokens must never be committed (in the development sandbox any
  git credential material lives outside the repository and the relevant files are
  gitignored).
- No secrets are required to run any component: the engine, API and UI run without
  authentication or keys.

## Known limitations (by design)

- **No authentication/authorization on the API.** This is intentional for a
  single-user laboratory tool bound to loopback. If you need multi-user access,
  put an authenticating proxy in front — and understand that you are leaving the
  supported configuration.
- The service has no rate limiting; run endpoints execute one engine subprocess per
  request (60 s timeout, hard kill).
- Configuration from untrusted sources should be validated with
  `micrort-engine validate` (or the API's validate endpoint) before being run —
  validation is a grammar check, not a sandbox; run untrusted workloads in a
  controlled environment.
