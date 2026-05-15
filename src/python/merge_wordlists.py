#!/usr/bin/env python3

'''
@author Ismail Alwahsh
@since May 9, 2026
@description: Merge a weakpass lookup result into the frequency-ranked
candidate list, deduplicating against the existing candidates. Weakpass
entries are either appended (default) or prepended based on the caller's
choice. The merged list is written to a caller-specified output path.
'''

from pathlib import Path


def _load_wordlist(path: Path) -> list[str]:
    try:
        text = path.read_text(encoding='utf-8')
    except UnicodeDecodeError:
        text = path.read_text(encoding='latin-1')
    return [line.rstrip('\r\n') for line in text.splitlines() if line.strip()]


def merge_wordlists(
    weakpass_path: Path,
    candidates_path: Path,
    output_path: Path,
    prepend: bool = False,
) -> int:
    candidates = _load_wordlist(candidates_path)
    weakpass   = _load_wordlist(weakpass_path)

    seen = set(candidates)
    new_entries = [w for w in weakpass if w not in seen]

    if prepend:
        merged = new_entries + candidates
    else:
        merged = candidates + new_entries

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text('\n'.join(merged) + '\n' if merged else '')

    return len(merged)


def main() -> None:
    import argparse
    parser = argparse.ArgumentParser(
        description="Merge a weakpass result into the ranked candidate list."
    )
    parser.add_argument("--weakpass",    required=True, help="weakpass result file")
    parser.add_argument("--candidates",  required=True, help="frequency-ranked candidate list")
    parser.add_argument("--output",      required=True, help="output path for merged list")
    parser.add_argument("--prepend",     action="store_true",
                        help="place weakpass entries before candidates (default: append)")
    args = parser.parse_args()

    total = merge_wordlists(
        Path(args.weakpass),
        Path(args.candidates),
        Path(args.output),
        prepend=args.prepend,
    )
    print(f"merged: {total} entries -> {args.output}")


if __name__ == "__main__":
    main()
