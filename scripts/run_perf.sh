#!/usr/bin/env bash
#
# Runs each perf binary 7 times. Counters are collected via perf_event_open
# inside the binary and scoped to the lookup loop only (load and query-
# generation phases are excluded). Each run emits `PERF: <name> = <value>`
# lines on stdout, one per counter.
#
set -euo pipefail

N=7
PROJ="$(cd "$(dirname "$0")/.." && pwd)"

for impl in tinyptr naive prob stdmap; do
    bin="${PROJ}/build/perf_${impl}"
    out="${PROJ}/results/perf_${impl}.txt"

    printf "=== lookup-only counters: %s (%d runs) ===\n" "${impl}" "${N}" > "${out}"

    for i in $(seq 1 "${N}"); do
        printf -- "--- run %d ---\n" "${i}" >> "${out}"
        "${bin}" >> "${out}" 2>&1
        printf "\n" >> "${out}"
    done

    echo "done: ${impl}"
done

echo ""
echo "All done. Results in results/perf_*.txt"
