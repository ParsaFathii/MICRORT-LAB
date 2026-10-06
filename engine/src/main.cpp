// main.cpp — micrort-engine executable entry point.
//
// Thin wrapper: all logic lives in micrort::runCli (cli.cpp) so the CLI is
// unit-testable in-process. main() only adds a last-resort exception guard
// (contract: no exception ever escapes main; runtime failures exit 3).
//
// Part of MicroRT-Lab — Deterministic Real-Time OS & Scheduling Laboratory.
// Copyright © 2026 Parsa Fathi. Apache-2.0 (see repository LICENSE).

#include <exception>
#include <iostream>

#include <nlohmann/json.hpp>

#include <micrort/cli.hpp>

int main(int argc, char** argv) {
    try {
        return micrort::runCli(argc, argv);
    } catch (const std::exception& e) {
        nlohmann::json j;
        j["error"] = "internal";
        j["message"] = e.what();
        std::cerr << j.dump() << "\n";
        return 3;
    } catch (...) {
        std::cerr << "{\"error\":\"internal\",\"message\":\"unknown exception\"}\n";
        return 3;
    }
}
