/*
 * @author Ismail Alwahsh
 * @since May 10, 2026
 * @description: CLI entry point for the tinymole cracker. Parses --hash, --algo,
 * --wordlist, --candidates, --threads, and --log-path flags. Builds the hash
 * table from the wordlist, loads the candidate list, then runs the multithreaded
 * crack. Prints "cracked: <password>" on success or "not found" on failure.
 * Structured logs go to logs/cracker.log via Quill.
 */

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <openssl/evp.h>

#include "quill/Backend.h"
#include "quill/Frontend.h"
#include "quill/LogMacros.h"
#include "quill/Logger.h"
#include "quill/sinks/ConsoleSink.h"
#include "quill/sinks/FileSink.h"

#include "src/cpp/tiny_ptr.hpp"
#include "src/cpp/hash_table.hpp"
#include "src/cpp/cracker.hpp"

static void usage(const char* prog) {
    std::fprintf(stderr,
        "usage: %s --hash <hex> [options]\n"
        "\n"
        "options:\n"
        "  --hash       <hex>   hex-encoded hash to crack (required)\n"
        "  --algo       <name>  hash algorithm: md5, sha1, sha256 (default: md5)\n"
        "  --wordlist   <path>  wordlist file, one password per line\n"
        "                       (default: data/rockyou_1m.txt)\n"
        "  --candidates <path>  frequency-ranked candidate list\n"
        "                       (defaults to wordlist if not provided)\n"
        "  --threads    <n>     number of worker threads (default: 4)\n"
        "  --log-path   <path>  log file path (default: logs/cracker.log)\n",
        prog);
}

static const EVP_MD* algo_to_md(const std::string& name) {
    if (name == "md5")    return EVP_md5();
    if (name == "sha1")   return EVP_sha1();
    if (name == "sha256") return EVP_sha256();
    return nullptr;
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

int main(int argc, char* argv[]) {
    std::string hash_hex;
    std::string algo            = "md5";
    std::string wordlist_path   = "data/rockyou_1m.txt";
    std::string candidates_path;
    std::string log_path        = "logs/cracker.log";
    int         num_threads     = 4;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--hash") == 0 && i + 1 < argc)
            hash_hex = argv[++i];
        else if (std::strcmp(argv[i], "--algo") == 0 && i + 1 < argc)
            algo = argv[++i];
        else if (std::strcmp(argv[i], "--wordlist") == 0 && i + 1 < argc)
            wordlist_path = argv[++i];
        else if (std::strcmp(argv[i], "--candidates") == 0 && i + 1 < argc)
            candidates_path = argv[++i];
        else if (std::strcmp(argv[i], "--threads") == 0 && i + 1 < argc)
            num_threads = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--log-path") == 0 && i + 1 < argc)
            log_path = argv[++i];
        else {
            usage(argv[0]);
            return 1;
        }
    }

    if (hash_hex.empty()) {
        usage(argv[0]);
        return 1;
    }

    const EVP_MD* md = algo_to_md(algo);
    if (!md) {
        std::fprintf(stderr, "error: unsupported algorithm '%s' (supported: md5, sha1, sha256)\n",
            algo.c_str());
        return 1;
    }

    const int expected_bytes = EVP_MD_get_size(md);
    std::vector<uint8_t> target_hash;
    if (!hex_to_bytes(hash_hex, target_hash) ||
            static_cast<int>(target_hash.size()) != expected_bytes) {
        std::fprintf(stderr, "error: --hash must be %d hex chars for %s\n",
            expected_bytes * 2, algo.c_str());
        return 1;
    }

    // Start Quill backend thread before any logging calls
    quill::BackendOptions backend_opts;
    quill::Backend::start(backend_opts);

    auto console_sink = quill::Frontend::create_or_get_sink<quill::ConsoleSink>("console");
    auto file_sink    = quill::Frontend::create_or_get_sink<quill::FileSink>(
        log_path,
        []() {
            quill::FileSinkConfig cfg;
            cfg.set_open_mode('w');
            return cfg;
        }()
    );

    quill::Logger* logger = quill::Frontend::create_or_get_logger(
        "cracker", {console_sink, file_sink}
    );

    LOG_INFO(logger, "target hash: {}", hash_hex);
    LOG_INFO(logger, "algorithm:   {}", algo);
    LOG_INFO(logger, "wordlist:    {}", wordlist_path);
    LOG_INFO(logger, "threads:     {}", num_threads);

    // Build hash table from wordlist
    PasswordPool pool;
    HashTable    table;

    {
        std::ifstream f(wordlist_path);
        if (!f) {
            std::fprintf(stderr, "error: cannot open wordlist: %s\n", wordlist_path.c_str());
            quill::Backend::stop();
            return 1;
        }
        auto stats = table.load(f, pool, logger);
        LOG_INFO(logger, "table ready: {} entries", stats.loaded);
    }

    // Load candidate list (defaults to wordlist if not provided)
    std::string cpath = candidates_path.empty() ? wordlist_path : candidates_path;
    LOG_INFO(logger, "loading candidates from: {}", cpath);
    auto candidates = load_lines(cpath);
    LOG_INFO(logger, "candidates loaded: {}", candidates.size());

    // Crack
    auto wall_start = std::chrono::steady_clock::now();
    std::string result = crack(candidates, table, pool, target_hash.data(), num_threads, md, logger);
    auto wall_end   = std::chrono::steady_clock::now();

    double total_ms = std::chrono::duration<double, std::milli>(wall_end - wall_start).count();

    if (!result.empty()) {
        LOG_INFO(logger, "CRACKED in {:.1f} ms: {}", total_ms, result);
        std::printf("cracked: %s\n", result.c_str());
    } else {
        LOG_INFO(logger, "NOT FOUND after {:.1f} ms", total_ms);
        std::printf("not found\n");
    }

    // Stop backend only after all threads have joined
    quill::Backend::stop();
    return result.empty() ? 1 : 0;
}
