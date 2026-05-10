#!/usr/bin/env python3

# @author Ish
# @since May 2026
# Weakpass API lookup. Queries weakpass.com for a hash with no local dataset
# required. Exits 0 and prints "cracked: <password>" on hit; exits 1 on miss.
# Uses a local JSON cache so repeated lookups during benchmarking don't burn
# API requests.

import argparse
import json
import sys
from pathlib import Path

import httpx

API_BASE  = "https://weakpass.com/api/v1"
CACHE_PATH = Path("data/weakpass_cache.json")


def _load_cache() -> dict[str, str]:
    if CACHE_PATH.exists():
        try:
            return json.loads(CACHE_PATH.read_text())
        except (json.JSONDecodeError, OSError):
            pass
    return {}


def _save_cache(cache: dict[str, str]) -> None:
    CACHE_PATH.parent.mkdir(parents=True, exist_ok=True)
    CACHE_PATH.write_text(json.dumps(cache, indent=2))


def lookup(hash_hex: str) -> str | None:
    cache = _load_cache()
    if hash_hex in cache:
        return cache[hash_hex]

    try:
        r = httpx.get(f"{API_BASE}/search/{hash_hex}", timeout=10)
        if r.status_code == 200:
            password = r.json().get("password")
            if password:
                cache[hash_hex] = password
                _save_cache(cache)
            return password
        return None
    except httpx.RequestError as e:
        print(f"error: weakpass API unreachable: {e}", file=sys.stderr)
        return None


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Query the weakpass API for a hash. No local dataset required."
    )
    parser.add_argument("--hash",  required=True, help="hex-encoded hash to look up")
    parser.add_argument("--algo",  default="md5", help="hash algorithm (informational only)")
    args = parser.parse_args()

    result = lookup(args.hash)
    if result:
        print(f"cracked: {result}")
        sys.exit(0)
    else:
        print("not found")
        sys.exit(1)


if __name__ == "__main__":
    main()
