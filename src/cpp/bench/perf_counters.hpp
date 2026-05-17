/*
 * @author Ismail Alwahsh
 * @description Lightweight wrapper over perf_event_open(2) for scoping hardware
 * counters around a specific code region (e.g., the lookup loop) rather than
 * measuring the entire program. Each counter is opened user-only, started with
 * PERF_EVENT_IOC_ENABLE before the region, stopped with DISABLE after it, and
 * read once at the end. Output is printed as parseable `PERF: <name> = <value>`
 * lines on stdout.
 */

#pragma once

#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

class PerfCounters {
public:
    struct Spec {
        const char* name;
        uint32_t    type;
        uint64_t    config;
    };

    explicit PerfCounters(const std::vector<Spec>& specs) {
        names_.reserve(specs.size());
        fds_.reserve(specs.size());
        for (const auto& s : specs) {
            perf_event_attr a{};
            a.type           = s.type;
            a.size           = sizeof(a);
            a.config         = s.config;
            a.exclude_kernel = 1;
            a.exclude_hv     = 1;
            a.disabled       = 1;
            int fd = static_cast<int>(::syscall(SYS_perf_event_open, &a, 0, -1, -1, 0));
            if (fd < 0) {
                std::fprintf(stderr, "perf_event_open(%s) failed: %s\n",
                             s.name, std::strerror(errno));
            }
            fds_.push_back(fd);
            names_.emplace_back(s.name);
        }
    }

    ~PerfCounters() {
        for (int fd : fds_)
            if (fd >= 0) ::close(fd);
    }

    void reset_and_start() {
        for (int fd : fds_) {
            if (fd < 0) continue;
            ::ioctl(fd, PERF_EVENT_IOC_RESET,  0);
            ::ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
        }
    }

    void stop() {
        for (int fd : fds_)
            if (fd >= 0) ::ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
    }

    void print() const {
        for (size_t i = 0; i < fds_.size(); ++i) {
            uint64_t v = 0;
            if (fds_[i] >= 0) {
                if (::read(fds_[i], &v, sizeof(v)) != sizeof(v))
                    v = 0;
            }
            std::printf("PERF: %s = %llu\n",
                        names_[i].c_str(), static_cast<unsigned long long>(v));
        }
    }

private:
    std::vector<int>         fds_;
    std::vector<std::string> names_;
};

// Standard counter set: matches what perf stat -e ... was collecting before,
// but scoped to the measured region rather than the whole program.
inline std::vector<PerfCounters::Spec> standard_events() {
    return {
        {"cache_misses",    PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES},
        {"cache_refs",      PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_REFERENCES},
        {"instructions",    PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS},
        {"cycles",          PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES},
        {"branch_misses",   PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES},
        {"dtlb_misses",     PERF_TYPE_HW_CACHE,
            static_cast<uint64_t>(PERF_COUNT_HW_CACHE_DTLB)
            | (static_cast<uint64_t>(PERF_COUNT_HW_CACHE_OP_READ)     << 8)
            | (static_cast<uint64_t>(PERF_COUNT_HW_CACHE_RESULT_MISS) << 16)},
        // Intel L1D.REPLACEMENT (event=0x51, umask=0x01). Tiger Lake / Alder Lake.
        {"l1d_replacement", PERF_TYPE_RAW, 0x151ULL},
    };
}
