#!/usr/bin/env python3

'''
@author Ismail Alwahsh
@since May 9, 2026
@description: Hash lookup utility. Queries multiple online APIs in sequence
(Weakpass, hashes.com, md5decrypt.net) and returns the plaintext password if
any service finds it. Results are cached in data/weakpass_cache.json so the
same hash is never queried twice. Exits 0 with "cracked: <password>" on a hit,
exits 1 on a miss. Intended to run as a fast check before or after local cracking.
'''

import argparse
import json
import sys
from pathlib import Path

import httpx

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


def _try_weakpass(client: httpx.Client, hash_hex: str) -> str | None:
    try:
        r = client.get(f"https://weakpass.com/api/v1/search/{hash_hex}", timeout=10)
        if r.status_code == 200:
            return r.json().get("password")
    except httpx.RequestError:
        pass
    return None


def _try_hashes_com(client: httpx.Client, hash_hex: str) -> str | None:
    try:
        r = client.post(
            "https://hashes.com/en/api/search",
            data={"hashes[]": hash_hex},
            timeout=10,
        )
        if r.status_code == 200:
            data = r.json()
            # response is a list of objects with "hash" and "plaintext" fields
            for entry in data.get("result", []):
                if entry.get("hash", "").lower() == hash_hex.lower():
                    return entry.get("plaintext") or None
    except (httpx.RequestError, ValueError):
        pass
    return None


def _try_md5decrypt(client: httpx.Client, hash_hex: str, algo: str) -> str | None:
    # md5decrypt.net only supports MD5 and a few others
    if algo not in ("md5", "sha1", "sha256"):
        return None
    try:
        r = client.get(
            "https://md5decrypt.net/Api/api.php",
            params={"hash": hash_hex, "hash_type": algo, "email": "guest", "code": "guest"},
            timeout=10,
        )
        if r.status_code == 200:
            text = r.text.strip()
            if text and text != "ERROR":
                return text
    except httpx.RequestError:
        pass
    return None


def lookup(hash_hex: str, algo: str = "md5") -> tuple[str | None, str | None]:
    '''Returns (password, service_name) or (None, None) if not found.'''
    cache = _load_cache()
    if hash_hex in cache:
        entry = cache[hash_hex]
        if isinstance(entry, dict):
            return entry.get("password"), entry.get("service")
        return entry, "cache"

    services = [
        ("weakpass",     lambda c: _try_weakpass(c, hash_hex)),
        ("hashes.com",   lambda c: _try_hashes_com(c, hash_hex)),
        ("md5decrypt",   lambda c: _try_md5decrypt(c, hash_hex, algo)),
    ]

    with httpx.Client() as client:
        for name, fn in services:
            password = fn(client)
            if password:
                cache[hash_hex] = {"password": password, "service": name}
                _save_cache(cache)
                return password, name

    return None, None


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Query online hash lookup APIs. No local dataset required."
    )
    parser.add_argument("--hash",  required=True, help="hex-encoded hash to look up")
    parser.add_argument("--algo",  default="md5", help="hash algorithm (md5, sha1, sha256)")
    args = parser.parse_args()

    password, service = lookup(args.hash, args.algo)
    if password:
        print(f"cracked: {password} (via {service})")
        sys.exit(0)
    else:
        print("not found")
        sys.exit(1)


if __name__ == "__main__":
    main()
