#!/usr/bin/env python3

'''
@author Patrick Rooney
@since May 9, 2026
@description: Combined candidate pipeline. Runs full frequency analysis on
rockyou.txt, merges a Weakpass wordlist (appended after the frequency-ranked
candidates), and writes all outputs used by the cracker:

  data/candidates_ranked.txt   -- combined ranked list (rockyou first, weakpass after)
  data/substitution_rules.json -- substitution pattern frequencies from combined corpus
  results/substitution_analysis_all.png
  results/substitution_analysis_leet.png

Delegates dataset-specific logic to frequency_analysis.py and merge_wordlists.py.
'''

import argparse
import json
import sys
from collections import Counter
from pathlib import Path

from frequency_analysis import (
    DATA_DIR, RESULTS_DIR, ROCKYOU, CANDIDATES, SUBS_JSON,
    load_rockyou, build_sub_counters, generate_variants,
    top_suffixes, plot_substitutions, normalize,
)
from merge_wordlists import _load_wordlist


def main() -> None:
    # entry point: loads both corpora, runs combined analysis, and writes all candidate outputs
    parser = argparse.ArgumentParser(
        description="Build a combined candidate list from rockyou + a Weakpass wordlist."
    )
    parser.add_argument(
        '--weakpass', type=Path, default=Path('data/ignis_10m.txt'),
        help='Path to a locally downloaded Weakpass-format wordlist (default: data/ignis_10m.txt)',
    )
    parser.add_argument(
        '--limit', type=int, default=0,
        help='Max passwords to read from rockyou.txt (0 = all, default: 0)',
    )
    parser.add_argument(
        '--top', type=int, default=50_000,
        help='Top N base words to expand with leet/suffix variants (default: 50000)',
    )
    parser.add_argument(
        '--output-limit', type=int, default=0,
        help='Max candidates to write to candidates_ranked.txt (0 = all)',
    )
    parser.add_argument(
        '--suffixes', type=int, default=10,
        help='Top N suffixes to append to base words (default: 10)',
    )
    args = parser.parse_args()

    if not ROCKYOU.exists():
        print(
            f"error: {ROCKYOU} not found.\n"
            "Download rockyou.txt and place it at data/rockyou.txt.",
            file=sys.stderr,
        )
        sys.exit(1)
    weakpass_available = args.weakpass.exists()
    if not weakpass_available:
        print(f"note: weakpass wordlist not found at {args.weakpass}, skipping merge", flush=True)

    RESULTS_DIR.mkdir(exist_ok=True)

    print(f"Configuration: --limit {args.limit} --top {args.top} --output-limit {args.output_limit}", flush=True)

    # load both corpora before any analysis so substitution stats reflect the full dataset
    print("Loading rockyou.txt", flush=True)
    freq = load_rockyou(ROCKYOU, limit=args.limit)
    unique_pw = len(freq)
    print(f"{unique_pw:,} unique passwords ({sum(freq.values()):,} total entries)")

    # weakpass is a flat wordlist so each entry gets count=1; duplicates accumulate naturally
    weakpass_freq: Counter[str] = Counter()
    if weakpass_available:
        print(f"Loading weakpass wordlist from {args.weakpass}", flush=True)
        weakpass_freq = Counter(_load_wordlist(args.weakpass))
        print(f"{len(weakpass_freq):,} unique weakpass entries")

    # Counter addition sums counts for shared keys, so rockyou frequency weights dominate
    combined_freq = freq + weakpass_freq

    # substitution analysis on combined corpus
    print("Analyzing substitution patterns", flush=True)
    sub_counters = build_sub_counters(combined_freq)

    # serialize substitution rules so the cracker can reference them later
    sub_rules_json: dict[str, dict[str, int]] = {
        plain: dict(cnt.most_common())
        for plain, cnt in sorted(sub_counters.items())
        if cnt
    }
    with open(SUBS_JSON, 'w', encoding='utf-8') as f:
        json.dump(sub_rules_json, f, indent=2)
    print(f"{len(sub_rules_json)} base chars with substitutions -> {SUBS_JSON}")

    print("Analyzing suffixes", flush=True)
    suffixes = top_suffixes(combined_freq, top_n=50)

    # build ranked candidate list seeded from rockyou frequency order
    print("Building ranked candidate list from rockyou", flush=True)
    ranked: list[str] = [pw for pw, _ in freq.most_common()]
    seen: set[str] = set(ranked)  # tracks everything added so far for O(1) dedup

    # aggregate rockyou counts under normalized base words so "password" outranks "p@ssword"
    base_freq: Counter[str] = Counter()
    for pw, count in freq.items():
        base_freq[normalize(pw)] += count

    # expand each top base word into its most statistically likely leet variants
    leet_added = 0
    for base_word, _ in base_freq.most_common(args.top):
        for _, variant in generate_variants(base_word, sub_counters):
            if variant not in seen:
                ranked.append(variant)
                seen.add(variant)
                leet_added += 1

    # append common non-alpha suffixes (e.g. "123", "!") to the same top base words
    suffix_list = [suf for suf, _ in suffixes[:args.suffixes]]
    suffixes_added = 0
    for base_word, _ in base_freq.most_common(args.top):
        for suf in suffix_list:
            variant = base_word + suf
            if variant not in seen:
                ranked.append(variant)
                seen.add(variant)
                suffixes_added += 1

    print(
        f"{len(ranked):,} rockyou candidates "
        f"({unique_pw:,} raw + {leet_added:,} leet + {suffixes_added:,} suffix)"
    )

    # append weakpass entries not already in the list
    if weakpass_available:
        new_from_weakpass = [pw for pw in weakpass_freq if pw not in seen]
        ranked.extend(new_from_weakpass)
        print(
            f"{len(weakpass_freq):,} weakpass entries, "
            f"{len(new_from_weakpass):,} new appended after rockyou candidates"
        )

    # write the final candidate list, honoring --output-limit if set
    total_candidates = len(ranked)
    write_limit = args.output_limit or total_candidates
    with open(CANDIDATES, 'w', encoding='utf-8') as f:
        for i, pw in enumerate(ranked):
            if i >= write_limit:
                break
            f.write(pw + '\n')
    print(f"Wrote {min(write_limit, total_candidates):,} candidates to {CANDIDATES}")

    if sub_counters:
        print("Generating substitution charts", flush=True)
        plot_substitutions(
            sub_counters,
            RESULTS_DIR / "substitution_analysis_all.png",
            title='All substitutions observed in combined corpus (including case changes)',
        )
        plot_substitutions(
            sub_counters,
            RESULTS_DIR / "substitution_analysis_leet.png",
            title='Leet-speak substitutions only (case changes excluded)',
            exclude_case_subs=True,
        )

    print("Done.")


if __name__ == '__main__':
    main()
