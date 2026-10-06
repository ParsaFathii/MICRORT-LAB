// cli.hpp — the micrort-engine command line entry point.
//
// Contract (SIMULATION_SCHEMA.md §Engine CLI contract):
//   micrort-engine run      --config <file|-> [--out <file|->] [--seed N]
//   micrort-engine validate --config <file|->
//   micrort-engine schema
//   micrort-engine selftest
//
// Exit codes: 0 = success, 2 = validation error (JSON error object on
// stderr), 3 = runtime/usage failure. `-` means stdin for --config and
// stdout for --out. runCli never throws and never calls exit(); main.cpp
// (and the unit tests) call it in-process.
//
// Part of MicroRT-Lab — Deterministic Real-Time OS & Scheduling Laboratory.
// Copyright © 2026 Parsa Fathi. Apache-2.0 (see repository LICENSE).
#ifndef MICRORT_CLI_HPP
#define MICRORT_CLI_HPP

namespace micrort {

/// Execute a CLI invocation. `argv` follows the usual main() conventions
/// (argv[0] = program name, argv[1] = subcommand). Output goes to
/// std::cout / std::cerr so tests can capture it via rdbuf redirection.
/// Returns the process exit code (0 / 2 / 3).
int runCli(int argc, char** argv);

} // namespace micrort

#endif // MICRORT_CLI_HPP
