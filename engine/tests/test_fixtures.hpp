// test_fixtures.hpp — shared sample configurations for the engine tests.
//
// kFixtureSmoke is the reference config from the task brief / schema docs.
// kFixturePool exercises pool memory, EDF, jitter, sporadic releases and
// every synchronization op. Both MUST stay valid (tests assert it).
//
// Part of MicroRT-Lab — Deterministic Real-Time OS & Scheduling Laboratory.
// Copyright © 2026 Parsa Fathi. Apache-2.0 (see repository LICENSE).
#ifndef MICRORT_ENGINE_TEST_FIXTURES_HPP
#define MICRORT_ENGINE_TEST_FIXTURES_HPP

#include <string>

static const std::string kFixtureSmoke = R"JSON({
  "schema": "micrort-config/1",
  "name": "smoke",
  "duration": 50,
  "seed": 7,
  "contextSwitchCost": 1,
  "scheduler": {"type": "rr", "quantum": 4},
  "aging": {"interval": 20, "cap": 5},
  "memory": {"model": "region", "total": 512, "policy": "first_fit"},
  "resources": [
    {"id": "M1", "type": "mutex", "protocol": "inherit"},
    {"id": "S1", "type": "sem", "initial": 0, "max": 3},
    {"id": "Q1", "type": "msgq", "capacity": 4},
    {"id": "E1", "type": "evflags"}
  ],
  "tasks": [
    {"id": "P1", "kind": "periodic", "priority": 5, "arrival": 0, "period": 20,
     "relativeDeadline": 15, "steps": [
        {"op": "cpu", "d": 3}, {"op": "lock", "res": "M1"},
        {"op": "cpu", "d": 2}, {"op": "alloc", "size": 64, "tag": "buf"},
        {"op": "unlock", "res": "M1"}, {"op": "io", "d": 4, "dev": "disk"},
        {"op": "free", "tag": "buf"}]},
    {"id": "C1", "kind": "aperiodic", "priority": 3, "arrival": 2,
     "steps": [{"op": "recv", "res": "Q1"}, {"op": "cpu", "d": 2}]},
    {"id": "SP", "kind": "sporadic", "priority": 1, "arrival": 0,
     "releases": [3, 9, 21], "steps": [{"op": "cpu", "d": 1}]}
  ]
})JSON";

static const std::string kFixturePool = R"JSON({
  "schema": "micrort-config/1",
  "name": "pool-edf",
  "description": "pool memory + edf + every sync op",
  "seed": 123456789,
  "duration": 200,
  "contextSwitchCost": 2,
  "scheduler": {"type": "edf"},
  "aging": {"interval": 25, "cap": 7},
  "memory": {"model": "pool", "blockSize": 32, "blockCount": 8},
  "resources": [
    {"id": "M2", "type": "mutex", "protocol": "none"},
    {"id": "S2", "type": "sem", "initial": 2, "max": 5},
    {"id": "Q2", "type": "msgq", "capacity": 2},
    {"id": "E2", "type": "evflags"}
  ],
  "tasks": [
    {"id": "EDF_P", "kind": "periodic", "priority": 2, "arrival": 0, "period": 30,
     "relativeDeadline": 25, "jitter": 3, "steps": [
        {"op": "cpu", "d": 2}, {"op": "wait", "res": "S2"},
        {"op": "sleep", "d": 1}, {"op": "alloc", "size": 32},
        {"op": "signal", "res": "S2"},
        {"op": "evwait", "res": "E2", "mask": 5, "mode": "all"},
        {"op": "free", "tag": "a3"}, {"op": "cpu", "d": 1}]},
    {"id": "EDF_S", "kind": "sporadic", "priority": 1, "releases": [0, 40, 90],
     "steps": [{"op": "evset", "res": "E2", "mask": 5}, {"op": "cpu", "d": 3}]},
    {"id": "EDF_A", "kind": "aperiodic", "priority": 4, "arrival": 5, "steps": [
        {"op": "lock", "res": "M2"}, {"op": "send", "res": "Q2", "msg": 42},
        {"op": "cpu", "d": 2}, {"op": "unlock", "res": "M2"}]}
  ]
})JSON";

#endif // MICRORT_ENGINE_TEST_FIXTURES_HPP
