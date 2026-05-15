from pathlib import Path

import pytest

from src.python.merge_wordlists import _load_wordlist, merge_wordlists


def _write(path: Path, lines: list[str], encoding: str = 'utf-8') -> None:
    path.write_bytes(b'\n'.join(line.encode(encoding) for line in lines) + b'\n')


class TestLoadWordlist:
    def test_basic_utf8(self, tmp_path):
        f = tmp_path / "words.txt"
        _write(f, ["password", "123456", "qwerty"])
        assert _load_wordlist(f) == ["password", "123456", "qwerty"]

    def test_empty_lines_skipped(self, tmp_path):
        f = tmp_path / "words.txt"
        f.write_bytes(b"password\n\n123456\n\n")
        assert _load_wordlist(f) == ["password", "123456"]

    def test_crlf_stripped(self, tmp_path):
        f = tmp_path / "words.txt"
        f.write_bytes(b"password\r\n123456\r\nqwerty\r\n")
        assert _load_wordlist(f) == ["password", "123456", "qwerty"]

    def test_latin1_fallback(self, tmp_path):
        f = tmp_path / "words.txt"
        # 0xe9 is 'é' in latin-1 but not valid UTF-8
        f.write_bytes(b"caf\xe9\npassword\n")
        result = _load_wordlist(f)
        assert result[0] == "café"
        assert result[1] == "password"

    def test_empty_file(self, tmp_path):
        f = tmp_path / "words.txt"
        f.write_bytes(b"")
        assert _load_wordlist(f) == []


class TestMergeWordlists:
    def _make_files(self, tmp_path, candidates: list[str], weakpass: list[str]):
        c = tmp_path / "candidates.txt"
        w = tmp_path / "weakpass.txt"
        o = tmp_path / "merged.txt"
        _write(c, candidates)
        _write(w, weakpass)
        return c, w, o

    def _read_output(self, path: Path) -> list[str]:
        return [line for line in path.read_text().splitlines() if line]

    def test_new_entries_appended(self, tmp_path):
        c, w, o = self._make_files(tmp_path, ["password", "123456"], ["letmein", "sunshine"])
        merge_wordlists(w, c, o)
        assert self._read_output(o) == ["password", "123456", "letmein", "sunshine"]

    def test_duplicates_excluded(self, tmp_path):
        c, w, o = self._make_files(tmp_path, ["password", "123456"], ["123456", "letmein"])
        merge_wordlists(w, c, o)
        result = self._read_output(o)
        assert result.count("123456") == 1
        assert "letmein" in result

    def test_prepend_places_weakpass_first(self, tmp_path):
        c, w, o = self._make_files(tmp_path, ["password", "123456"], ["letmein", "sunshine"])
        merge_wordlists(w, c, o, prepend=True)
        result = self._read_output(o)
        assert result == ["letmein", "sunshine", "password", "123456"]

    def test_all_duplicates_nothing_added(self, tmp_path):
        c, w, o = self._make_files(tmp_path, ["password", "123456"], ["password", "123456"])
        merge_wordlists(w, c, o)
        assert self._read_output(o) == ["password", "123456"]

    def test_empty_weakpass(self, tmp_path):
        c, w, o = self._make_files(tmp_path, ["password", "123456"], [])
        merge_wordlists(w, c, o)
        assert self._read_output(o) == ["password", "123456"]

    def test_returns_total_count(self, tmp_path):
        c, w, o = self._make_files(tmp_path, ["password", "123456"], ["letmein"])
        total = merge_wordlists(w, c, o)
        assert total == 3

    def test_output_dir_created(self, tmp_path):
        c = tmp_path / "candidates.txt"
        w = tmp_path / "weakpass.txt"
        o = tmp_path / "subdir" / "merged.txt"
        _write(c, ["password"])
        _write(w, ["letmein"])
        merge_wordlists(w, c, o)
        assert o.exists()
