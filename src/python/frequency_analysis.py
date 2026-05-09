#!/usr/bin/env python3

'''
@author Patrick Rooney 
@since May 8, 2026
@description: Frequency analysis script. Used as step 1 in project pipeline, with the following goals:

1. Obtain candidate patterns to rank for password cracker. The most frequently appearing patterns in the rockyou dataset will be used for guessing patterns first. 
2. Generate visualizations/plots of the most frequently occurring patterns in the rockyou dataset. The distribution of common patterns will help visualize the foundation of the algorithm and supplement analysis in the project write up. The visualization will create a bar chart of the top 20 leet substitutions (substitutions where letters are replaced by numbers or other non-alphabetical characters). 
'''

# util imports 
import argparse
import json
import re
import sys
from collections import Counter, defaultdict
from itertools import combinations, product
from pathlib import Path

# viz/external imports 
import matplotlib.pyplot as plt

DATA_DIR    = Path("data")
RESULTS_DIR = Path("results")
ROCKYOU     = DATA_DIR / "rockyou.txt"
CANDIDATES  = DATA_DIR / "candidates_ranked.txt"
SUBS_JSON   = DATA_DIR / "substitution_rules.json"

# Leet-variant mapping 
LEET: dict[str, list[str]] = {
    'a': ['@', '4'],
    'e': ['3'],
    'i': ['1', '!'],
    'o': ['0'],
    's': ['$', '5'],
    'l': ['1'],
    't': ['+', '7'],
    'b': ['8'],
    'g': ['9'],
}

# '1' maps to 'i' before 'l' since setdefault keeps the first winner
REV_LEET: dict[str, str] = {}
for _plain, _syms in LEET.items():
    for _s in _syms:
        REV_LEET.setdefault(_s, _plain)


# convert password to lower and check for leet symbols 
def normalize(pw: str) -> str:
    return ''.join(REV_LEET.get(c, c) for c in pw.lower())

# load the rockyou dataset (uses utf-8 encoding)
def load_rockyou(path: Path, limit: int = 0) -> Counter[str]:
    freq: Counter[str] = Counter()
    with open(path, 'rb') as fh:
        for raw in fh:
            raw = raw.rstrip(b'\r\n')
            if not raw:
                continue
            try:
                pw = raw.decode('utf-8')
            except UnicodeDecodeError:
                pw = raw.decode('latin-1')  # rockyou has mixed encodings
            freq[pw] += 1
            if limit and sum(freq.values()) >= limit:
                break
    return freq

# tally how often each plain letter is subbed for a leet symbol, and create weightings 
def build_sub_counters(freq: Counter[str]) -> dict[str, Counter[str]]:
    sub_counters: dict[str, Counter[str]] = defaultdict(Counter)
    for pw, count in freq.items():
        base = normalize(pw)
        if base == pw or base not in freq or len(base) != len(pw):
            continue
        for pw_ch, base_ch in zip(pw, base):
            if pw_ch != base_ch:
                sub_counters[base_ch][pw_ch] += count  # weight by how many users had this password
    return sub_counters

# expand base word into ranked leet variants 
def generate_variants(
    word: str,
    sub_counters: dict[str, Counter[str]],
    max_subs: int = 2,
    cap: int = 64,
) -> list[tuple[int, str]]:
    positions = [
        (i, ch, list(sub_counters[ch].most_common()))
        for i, ch in enumerate(word.lower())
        if ch in sub_counters and sub_counters[ch]
    ]
    if not positions:
        return []

    results: list[tuple[int, str]] = []
    for r in range(1, min(max_subs + 1, len(positions) + 1)):
        for subset in combinations(positions, r):
            sub_opts = [syms for _, _, syms in subset]
            for combo in product(*sub_opts):
                score = 1
                chars = list(word)
                for (idx, _, _), (sym, cnt) in zip(subset, combo):
                    chars[idx] = sym
                    score *= cnt  # rarer combos naturally sort lower
                results.append((score, ''.join(chars)))

    results.sort(key=lambda x: x[0], reverse=True)
    seen: set[str] = set()
    deduped: list[tuple[int, str]] = []
    for score, v in results:
        if v not in seen and v != word:
            seen.add(v)
            deduped.append((score, v))
            if len(deduped) >= cap:
                break
    return deduped

# Extract and count the most common non-alphabetical suffixes appended to passwords in corpus
def top_suffixes(freq: Counter[str], top_n: int = 50) -> list[tuple[str, int]]:
    suffix_re = re.compile(r'^[a-zA-Z].*?([^a-zA-Z]+)$')
    counts: Counter[str] = Counter()
    for pw, count in freq.items():
        m = suffix_re.match(pw)
        if m:
            counts[m.group(1)] += count  # only count passwords with a real alpha prefix
    return counts.most_common(top_n)

# Render and save a bar chart of top 20 most frequent substitutions in the rockyou corpus 
def plot_substitutions(
    sub_counters: dict[str, Counter[str]],
    out_path: Path,
    title: str = 'Top character substitutions observed in RockYou',
    exclude_case_subs: bool = False,
) -> None:
    pairs: list[tuple[str, int]] = []
    for plain, subs in sorted(sub_counters.items()):
        for sym, count in subs.most_common(3):
            if exclude_case_subs and sym == plain.upper():  # skip a->A, s->S, etc.
                continue
            pairs.append((f"{plain}→{sym}", count))
    pairs.sort(key=lambda x: x[1], reverse=True)
    pairs = pairs[:20]
    if not pairs:
        return

    labels, counts = zip(*pairs)
    fig, ax = plt.subplots(figsize=(10, 5))
    ax.bar(range(len(labels)), counts)
    ax.set_xticks(range(len(labels)))
    ax.set_xticklabels(labels, rotation=45, ha='right')
    ax.set_ylabel('Occurrences in RockYou corpus')
    ax.set_title(title)
    fig.tight_layout()
    fig.savefig(out_path, dpi=120)
    plt.close(fig)
    print(f"  Chart saved to {out_path}")


# entry point: load corpus --> analyze substitutions --> generate ranked candidates --> save charts and write outputs for cracker to use 
def main() -> None:
    parser = argparse.ArgumentParser(
        description="Frequency analysis engine: produces ranked candidates and substitution rules."
    )
    parser.add_argument(
        '--limit', type=int, default=0,
        help='Max passwords to load from rockyou.txt (0 = no limit, default: 0)',
    )
    parser.add_argument(
        '--top', type=int, default=50_000,
        help='Top N base words to expand with leet variants (default: 50000)',
    )
    parser.add_argument(
        '--output-limit', type=int, default=0,
        help='Max candidates to write to candidates_ranked.txt (0 = no limit)',
    )
    args = parser.parse_args()

    if not ROCKYOU.exists():
        print(
            f"error: {ROCKYOU} not found.\n"
            "Download rockyou.txt and place it at data/rockyou.txt.",
            file=sys.stderr,
        )
        sys.exit(1)

    RESULTS_DIR.mkdir(exist_ok=True)

    print("Loading rockyou.txt ...", flush=True)
    freq = load_rockyou(ROCKYOU, limit=args.limit)
    total_entries = sum(freq.values())
    unique_pw = len(freq)
    print(f"  {unique_pw:,} unique passwords ({total_entries:,} total entries)")

    print("Analyzing substitution patterns ...", flush=True)
    sub_counters = build_sub_counters(freq)

    sub_rules_json: dict[str, dict[str, int]] = {
        plain: dict(cnt.most_common())
        for plain, cnt in sorted(sub_counters.items())
        if cnt
    }
    with open(SUBS_JSON, 'w', encoding='utf-8') as f:
        json.dump(sub_rules_json, f, indent=2)
    print(f"  {len(sub_rules_json)} base chars with substitutions -> {SUBS_JSON}")

    if sub_counters:
        print("  Top substitutions:")
        flat = [
            (plain, sym, cnt)
            for plain, subs in sub_counters.items()
            for sym, cnt in subs.most_common(2)
        ]
        flat.sort(key=lambda x: x[2], reverse=True)
        for plain, sym, cnt in flat[:8]:
            print(f"    {plain} -> {sym}  ({cnt:,}x)")

    print("Analyzing suffixes ...", flush=True)
    suffixes = top_suffixes(freq, top_n=50)
    if suffixes:
        print("  Top 10 suffixes:")
        for suf, cnt in suffixes[:10]:
            print(f"    {repr(suf):12s}  {cnt:,}")

    print("Building ranked candidate list ...", flush=True)

    ranked: list[str] = [pw for pw, _ in freq.most_common()]  # raw frequency order first
    seen: set[str] = set(ranked)

    # aggregate by base word so "password" outscores its rarer leet variants when picking what to expand
    base_freq: Counter[str] = Counter()
    for pw, count in freq.items():
        base_freq[normalize(pw)] += count

    variants_added = 0
    for base_word, _ in base_freq.most_common(args.top):
        for _, variant in generate_variants(base_word, sub_counters):
            if variant not in seen:
                ranked.append(variant)
                seen.add(variant)
                variants_added += 1

    total_candidates = len(ranked)
    write_limit = args.output_limit or total_candidates
    print(
        f"  {total_candidates:,} total candidates "
        f"({unique_pw:,} raw + {variants_added:,} generated variants)"
    )
    if args.output_limit:
        print(f"  Writing first {write_limit:,} (--output-limit)")

    with open(CANDIDATES, 'w', encoding='utf-8') as f:
        for i, pw in enumerate(ranked):
            if i >= write_limit:
                break
            f.write(pw + '\n')
    print(f"  Wrote {CANDIDATES}")

    if sub_counters:
        print("Generating substitution charts ...", flush=True)
        plot_substitutions(
            sub_counters,
            RESULTS_DIR / "substitution_analysis_all.png",
            title='All substitutions observed in RockYou (including case changes)',
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
