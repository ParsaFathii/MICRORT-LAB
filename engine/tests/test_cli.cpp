// test_cli.cpp — in-process invocation of micrort::runCli.
//
// Captures std::cout / std::cerr / std::cin via rdbuf redirection (cli.cpp
// never uses printf for its outputs). Covers: validate (valid/invalid,
// stdin, malformed JSON), run (stub result, exit codes, --out file,
// --seed), schema (exact document), selftest (exit 0) and usage errors.
//
// Part of MicroRT-Lab — Deterministic Real-Time OS & Scheduling Laboratory.
// Copyright © 2026 Parsa Fathi. Apache-2.0 (see repository LICENSE).
#define TH_SUITE "test_cli"

#include "test_fixtures.hpp"
#include "test_harness.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

#include <micrort/cli.hpp>

namespace {

/// Redirects cout + cerr into owned stringstreams for the call's lifetime.
struct Redirect {
    std::ostringstream out;
    std::ostringstream err;
    std::streambuf* oldOut;
    std::streambuf* oldErr;

    Redirect() : oldOut(std::cout.rdbuf(out.rdbuf())),
                 oldErr(std::cerr.rdbuf(err.rdbuf())) {}
    ~Redirect() {
        std::cout.rdbuf(oldOut);
        std::cerr.rdbuf(oldErr);
    }
};

/// Feeds `text` to std::cin (as if --config - read stdin).
struct CinRedirect {
    std::streambuf* old;
    explicit CinRedirect(const std::string& text) : old(std::cin.rdbuf()) {
        static std::stringstream buffer; // shared: refilled per instance
        buffer.str(text);
        buffer.clear();
        std::cin.rdbuf(buffer.rdbuf());
        std::cin.clear();
    }
    ~CinRedirect() {
        std::cin.rdbuf(old);
        std::cin.clear();
    }
};

/// RAII temp file (unique name, removed on scope exit).
struct TempFile {
    std::string path;

    explicit TempFile(const std::string& content) {
        static int counter = 0;
        path = "/tmp/micrort-test-" + std::to_string(::getpid()) + "-" +
               std::to_string(++counter) + ".json";
        std::ofstream f(path, std::ios::out | std::ios::binary);
        f << content;
    }
    ~TempFile() { std::remove(path.c_str()); }
};

/// Calls runCli with argv built from strings (argv[0] is a dummy program
/// name). String storage must outlive the call — `args` does.
int run(const std::vector<std::string>& args, std::string& out,
        std::string& err) {
    std::vector<std::string> storage;
    storage.reserve(args.size() + 1);
    storage.emplace_back("micrort-engine");
    for (const auto& a : args) {
        storage.push_back(a);
    }
    std::vector<char*> argv;
    argv.reserve(storage.size());
    for (auto& s : storage) {
        argv.push_back(const_cast<char*>(s.data()));
    }
    Redirect r;
    const int rc = micrort::runCli(static_cast<int>(argv.size()), argv.data());
    out = r.out.str();
    err = r.err.str();
    return rc;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

int main() {
    // ------------------------------------------------------------------
    // validate
    // ------------------------------------------------------------------
    TEST("validate: valid config exits 0 and reports hash");
    {
        const TempFile f(kFixtureSmoke);
        std::string out, err;
        CHECK_EQ(run({"validate", "--config", f.path}, out, err), 0);
        CHECK_TRUE(contains(out, "\"valid\":true"));
        CHECK_TRUE(contains(out, "\"configHash\":\""));
        CHECK_TRUE(!out.empty() && out.back() == '\n');
        CHECK_TRUE(err.empty());
    }
    TEST("validate: invalid config exits 2 with details");
    {
        const std::string bad = R"({"schema":"micrort-config/1","name":"x",
            "duration":0,"scheduler":{"type":"fifo"},
            "tasks":[{"id":"A","kind":"aperiodic","steps":[]]}})";
        const TempFile f(bad);
        std::string out, err;
        CHECK_EQ(run({"validate", "--config", f.path}, out, err), 2);
        CHECK_TRUE(contains(out, "\"valid\":false"));
        CHECK_TRUE(contains(out, "\"details\""));
    }
    TEST("validate: malformed JSON via stdin");
    {
        std::string out, err;
        CinRedirect cin(R"({"name":"bad")");
        CHECK_EQ(run({"validate", "--config", "-"}, out, err), 2);
        CHECK_TRUE(contains(out, "\"valid\":false"));
        CHECK_TRUE(contains(out, "malformed JSON"));
    }
    TEST("validate: valid config via stdin");
    {
        std::string out, err;
        CinRedirect cin(kFixtureSmoke);
        CHECK_EQ(run({"validate", "--config", "-"}, out, err), 0);
        CHECK_TRUE(contains(out, "\"valid\":true"));
    }
    TEST("validate: missing file exits 3 (io)");
    {
        std::string out, err;
        CHECK_EQ(run({"validate", "--config", "/tmp/micrort-no-such-file.json"},
                     out, err), 3);
        CHECK_TRUE(contains(err, "\"error\":\"io\""));
    }
    TEST("validate: missing --config exits 3 (usage)");
    {
        std::string out, err;
        CHECK_EQ(run({"validate"}, out, err), 3);
        CHECK_TRUE(contains(err, "\"error\":\"usage\""));
    }

    // ------------------------------------------------------------------
    // run (stage 1: validated stub)
    // ------------------------------------------------------------------
    TEST("run: valid config prints stub result, exit 0");
    {
        const TempFile f(kFixtureSmoke);
        std::string out, err;
        CHECK_EQ(run({"run", "--config", f.path}, out, err), 0);
        CHECK_TRUE(contains(out, "\"schema\":\"micrort-result/1\""));
        CHECK_TRUE(contains(out, "\"status\":\"completed\""));
        CHECK_TRUE(contains(out, "\"metrics\""));
        CHECK_TRUE(contains(err, "run: smoke")); // one-line stderr summary
    }
    TEST("run: invalid config exits 2 with validation error on stderr");
    {
        const std::string bad = R"({"schema":"micrort-config/1","name":"x",
            "duration":0,"scheduler":{"type":"rr","quantum":0},
            "tasks":[{"id":"A","kind":"aperiodic",
                      "steps":[{"op":"cpu","d":0}]}]})";
        const TempFile f(bad);
        std::string out, err;
        CHECK_EQ(run({"run", "--config", f.path}, out, err), 2);
        CHECK_TRUE(contains(err, "\"error\":\"validation\""));
        CHECK_TRUE(contains(err, "\"details\""));
        CHECK_TRUE(out.empty()); // nothing on stdout
    }
    TEST("run: --out writes the file, stdout stays empty");
    {
        const TempFile cfg(kFixtureSmoke);
        const std::string outPath = "/tmp/micrort-test-run-out.json";
        std::string out, err;
        CHECK_EQ(run({"run", "--config", cfg.path, "--out", outPath}, out, err), 0);
        CHECK_TRUE(out.empty());
        std::ifstream f(outPath);
        std::string content((std::istreambuf_iterator<char>(f)),
                            std::istreambuf_iterator<char>());
        std::remove(outPath.c_str());
        CHECK_TRUE(contains(content, "\"status\":\"completed\""));
        CHECK_TRUE(contains(content, "\"trace\":"));
    }
    TEST("run: --seed accepted (non-negative)");
    {
        const TempFile f(kFixtureSmoke);
        std::string out, err;
        CHECK_EQ(run({"run", "--config", f.path, "--seed", "99"}, out, err), 0);
        CHECK_TRUE(contains(out, "\"status\":\"completed\""));
    }
    TEST("run: negative --seed is a usage error");
    {
        const TempFile f(kFixtureSmoke);
        std::string out, err;
        CHECK_EQ(run({"run", "--config", f.path, "--seed", "-5"}, out, err), 3);
        CHECK_TRUE(contains(err, "\"error\":\"usage\""));
    }
    TEST("run: unknown flag is a usage error");
    {
        const TempFile f(kFixtureSmoke);
        std::string out, err;
        CHECK_EQ(run({"run", "--config", f.path, "--verbose"}, out, err), 3);
        CHECK_TRUE(contains(err, "unknown option"));
    }
    TEST("run: missing config file exits 3");
    {
        std::string out, err;
        CHECK_EQ(run({"run", "--config", "/tmp/micrort-no-such.json"}, out, err), 3);
        CHECK_TRUE(contains(err, "\"error\":\"io\""));
    }

    // ------------------------------------------------------------------
    // schema
    // ------------------------------------------------------------------
    TEST("schema prints the contract summary");
    {
        std::string out, err;
        CHECK_EQ(run({"schema"}, out, err), 0);
        CHECK_STREQ(out,
            "{\"config\":\"micrort-config/1\",\"result\":\"micrort-result/1\","
            "\"schedulers\":[\"fifo\",\"rr\",\"priority\",\"priority_p\","
            "\"sjf\",\"srtf\",\"rm\",\"edf\"],"
            "\"steps\":[\"cpu\",\"io\",\"sleep\",\"lock\",\"unlock\",\"wait\","
            "\"signal\",\"send\",\"recv\",\"evwait\",\"evset\",\"alloc\","
            "\"free\"]}\n");
        CHECK_TRUE(err.empty());
    }

    // ------------------------------------------------------------------
    // selftest
    // ------------------------------------------------------------------
    TEST("selftest passes (exit 0)");
    {
        std::string out, err;
        CHECK_EQ(run({"selftest"}, out, err), 0);
        CHECK_TRUE(contains(out, "\"selftest\":\"ok\""));
        CHECK_TRUE(err.empty()); // failures would be listed here
    }

    // ------------------------------------------------------------------
    // usage errors
    // ------------------------------------------------------------------
    TEST("no arguments prints usage, exit 3");
    {
        std::string out, err;
        CHECK_EQ(run({}, out, err), 3);
        CHECK_TRUE(contains(err, "usage: micrort-engine"));
    }
    TEST("unknown subcommand exits 3");
    {
        std::string out, err;
        CHECK_EQ(run({"frobnicate"}, out, err), 3);
        CHECK_TRUE(contains(err, "unknown command"));
    }

    return TEST_SUMMARY();
}
