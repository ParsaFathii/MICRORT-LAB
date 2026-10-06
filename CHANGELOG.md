# Changelog

All notable changes to MicroRT-Lab are documented in this file.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and the project adheres to [Semantic Versioning](https://semver.org/).

## [1.0.0] — 2026-10-06

First public release: the complete, deterministic scheduling laboratory.

### Added

**Simulation kernel (C11)**
- Fixed-capacity task control blocks, ready queue, and a tick-driven core
  designed for deterministic discrete-event execution
- Kernel-style synchronization primitives: mutex (with optional priority
  inheritance), counting semaphores, fixed-capacity message queues,
  event flags (wait/set), and a first/best/worst-fit memory allocator
- 11 in-process unit test suites (`kernel/tests`)

**Simulation engine (C++20)**
- Discrete-event engine with 8 schedulers: FIFO, RR (configurable quantum),
  static priority, priority with preemption, priority with aging, SJF, SRTF,
  Rate-Monotonic and EDF
- 13-step job interpreter: cpu, io, sleep, lock, unlock, wait, signal,
  send, recv, evwait, evset, alloc, free
- Deadlock detection via resource-allocation graph cycle search, reported
  with the participating tasks and resources
- Byte-deterministic result documents (SHA-256 config hashing, seeded RNG);
  identical config + seed ⇒ identical result modulo `wallMicros`
- JSON result document: trace events, Gantt segments, per-task accounting,
  aggregate metrics, deadlock records, memory statistics
- CLI: `run`, `validate`, `schema`, `selftest`
- 6 in-process unit test suites (`engine/tests`)

**Analysis layer (Python 3.12 + FastAPI)**
- REST API (`/api/v1`): health, experiments, simulations, trace with
  filters/pagination, metrics cross-check, RT analysis, comparisons,
  reports (markdown/CSV/JSON)
- Python recomputation of every aggregate metric as an independent
  cross-check of the C++ engine (tolerance-based match report)
- Real-time scheduling analysis: Liu & Layland utilization bounds,
  response-time analysis, deadline miss attribution, starvation detection,
  memory fragmentation summaries
- Scheduler comparison: deep-merge variant specs over one base config,
  run every variant, best-per-metric highlighting, delta-vs-first matrix
- 97 pytest tests (unit + API + real-engine integration)

**Web workstation (Next.js 16 + TypeScript)**
- 10 views: workbench, timeline (Gantt), live playback console, trace
  browser, resources, deadlock RAG, memory map, metrics, compare, reports
- Tick-accurate playback with play/pause/step and adjustable speed
- Deterministic UI: the frontend never computes scheduler results —
  every number comes from the engine or the analysis layer
- Bilingual documentation: English + Persian (فارسی)

**9 built-in experiments**
- rate-monotonic, edf-deadlines, rr-quantum-comparison, priority-inversion,
  priority-inheritance, priority-starvation, producer-consumer,
  memory-fragmentation, deadlock-circular

**Infrastructure**
- GitHub Actions CI: matrix build (gcc-13, clang-16) with warnings-as-errors,
  ctest, engine selftest, all experiments, determinism regression,
  Python lint + tests against the real engine binary, web lint + type check,
  documentation link verification, credential history scan

[1.0.0]: https://github.com/ParsaFathii/MICRORT-LAB/releases/tag/v1.0.0
