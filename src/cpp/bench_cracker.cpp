/*
 * @author Ismail Alwahsh
 * @since May 7, 2026
 * @description: End-to-end cracker benchmark. Runs a full crack attempt across
 * all three hash table implementations and reports wall-clock time per run.
 * Used to produce the thread-count vs crack-time numbers in the README.
 */

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <openssl/evp.h>

#include "src/cpp/tiny_ptr.hpp"
#include "src/cpp/hash_table.hpp"
#include "src/cpp/hash_table_naive.hpp"
#include "src/cpp/hash_table_stdmap.hpp"
#include "src/cpp/hash_table_prob.hpp"
#include "src/cpp/cracker.hpp"

static std::vector<std::string> load_lines(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

static bool hex_to_bytes(const std::string& hex, std::vector<uint8_t>& out) {
    if (hex.size() % 2 != 0) return false;
    const size_t n = hex.size() / 2;
    out.resize(n);
    for (size_t i = 0; i < n; ++i) {
        char buf[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
        char* end;
        out[i] = static_cast<uint8_t>(std::strtoul(buf, &end, 16));
        if (end != buf + 2) return false;
    }
    return true;
}

int main(int argc, char* argv[]) {
    std::string impl     = "tinyptr";
    std::string wordlist = "data/rockyou.txt";
    std::string hash_hex;
    int         threads  = 4;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--impl") == 0 && i + 1 < argc)
            impl = argv[++i];
        else if (std::strcmp(argv[i], "--wordlist") == 0 && i + 1 < argc)
            wordlist = argv[++i];
        else if (std::strcmp(argv[i], "--hash") == 0 && i + 1 < argc)
            hash_hex = argv[++i];
        else if (std::strcmp(argv[i], "--threads") == 0 && i + 1 < argc)
            threads = std::atoi(argv[++i]);
    }

    if (hash_hex.empty()) {
        std::fprintf(stderr,
            "usage: %s --hash <hex> [--impl tinyptr|naive|stdmap|prob] "
            "[--threads n] [--wordlist path]\n", argv[0]);
        return 1;
    }

    std::vector<uint8_t> target;
    if (!hex_to_bytes(hash_hex, target)) {
        std::fprintf(stderr, "error: invalid hash hex\n");
        return 1;
    }

    auto candidates = load_lines(wordlist);

    double      crack_ms    = 0.0;
    size_t      hashes_done = 0;
    std::string result;

    if (impl == "tinyptr") {
        PasswordPool pool;
        HashTable    table;
        std::ifstream f(wordlist);
        table.load(f, pool);
        auto t0 = std::chrono::steady_clock::now();
        result   = crack(candidates, table, pool, target.data(), threads, &hashes_done);
        crack_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();

    } else if (impl == "naive") {
        PasswordPool    pool;
        HashTableNaive  table;
        std::ifstream f(wordlist);
        table.load(f, pool);
        auto t0 = std::chrono::steady_clock::now();
        result   = crack(candidates, table, pool, target.data(), threads, &hashes_done);
        crack_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();

    } else if (impl == "stdmap") {
        PasswordPool   pool;   // not used for storage; passed for lookup signature
        HashTableStdMap table;
        std::ifstream f(wordlist);
        table.load(f);
        auto t0 = std::chrono::steady_clock::now();
        result   = crack(candidates, table, pool, target.data(), threads, &hashes_done);
        crack_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();

    } else if (impl == "prob") {
        PasswordPool  pool;   // unused by HashTableProb; passed to crack() for template compat
        HashTableProb table;
        std::ifstream f(wordlist);
        table.load(f);
        auto t0 = std::chrono::steady_clock::now();
        result   = crack(candidates, table, pool, target.data(), threads, &hashes_done);
        crack_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();

    } else {
        std::fprintf(stderr, "error: unknown impl '%s' (use tinyptr, naive, stdmap, or prob)\n",
            impl.c_str());
        return 1;
    }

    double mhs = (crack_ms > 0.0)
        ? static_cast<double>(hashes_done) / crack_ms / 1000.0
        : 0.0;

    std::printf("impl=%-8s threads=%d crack_ms=%6.1f mhs=%6.2f hashes=%zu result=%s\n",
        impl.c_str(), threads, crack_ms, mhs, hashes_done,
        result.empty() ? "NOT_FOUND" : result.c_str());
    return result.empty() ? 1 : 0;
}
