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
        "usage: %s --hash <md5_hex> [options]\n"
        "\n"
        "options:\n"
        "  --hash       <hex>   32-char MD5 hash to crack (required)\n"
        "  --wordlist   <path>  wordlist file, one password per line\n"
        "                       (default: data/rockyou_1m.txt)\n"
        "  --candidates <path>  frequency-ranked candidate list\n"
        "                       (defaults to wordlist if not provided)\n"
        "  --threads    <n>     number of worker threads (default: 4)\n"
        "  --log-path   <path>  log file path (default: logs/cracker.log)\n",
        prog);
}

static bool hex_to_bytes(const std::string& hex, uint8_t out[16]) {
    if (hex.size() != 32) return false;
    for (size_t i = 0; i < 16; ++i) {
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
    std::string wordlist_path   = "data/rockyou_1m.txt";
    std::string candidates_path;
    std::string log_path        = "logs/cracker.log";
    int         num_threads     = 4;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--hash") == 0 && i + 1 < argc)
            hash_hex = argv[++i];
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

    uint8_t target_hash[16];
    if (!hex_to_bytes(hash_hex, target_hash)) {
        std::fprintf(stderr, "error: --hash must be a 32-character hex string\n");
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
    std::string result = crack(candidates, table, pool, target_hash, num_threads, logger);
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
