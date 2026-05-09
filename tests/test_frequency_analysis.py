from collections import Counter

from frequency_analysis import build_sub_counters, generate_variants, normalize


class TestNormalize:
    def test_leet_substitutions(self):
        assert normalize("p@ssw0rd") == "password"
        assert normalize("H3llo") == "hello"
        assert normalize("h3ck3r") == "hecker"

    def test_already_base(self):
        assert normalize("password") == "password"
        assert normalize("hello") == "hello"

    def test_case_folding(self):
        assert normalize("PASSWORD") == "password"
        assert normalize("MiXeD") == "mixed"

    def test_empty_string(self):
        assert normalize("") == ""


class TestBuildSubCounters:
    def test_basic_substitutions(self):
        freq = Counter({"p@ssword": 5, "password": 10, "h3llo": 3, "hello": 7})
        subs = build_sub_counters(freq)
        assert "a" in subs
        assert subs["a"]["@"] == 5   # p@ssword -> password, weight=5
        assert "e" in subs
        assert subs["e"]["3"] == 3   # h3llo -> hello, weight=3

    def test_no_substitutions(self):
        freq = Counter({"password": 10, "hello": 5})
        subs = build_sub_counters(freq)
        assert len(subs) == 0

    def test_base_not_in_corpus_is_skipped(self):
        # "p@ssword" normalizes to "password", but "password" is not in freq,
        # so no substitution should be recorded.
        freq = Counter({"p@ssword": 5})
        subs = build_sub_counters(freq)
        assert len(subs) == 0

    def test_weight_accumulation(self):
        # Each substituted character position is counted independently, so "l33t"
        # (two 3→e positions, count=4) contributes 4+4=8, and "fr33" (two 3→e
        # positions, count=2) contributes 2+2=4. Total: 12.
        freq = Counter({"l33t": 4, "leet": 10, "fr33": 2, "free": 8})
        subs = build_sub_counters(freq)
        assert subs["e"]["3"] == 8 + 4  # (4+4) from l33t + (2+2) from fr33


class TestGenerateVariants:
    def test_basic_variants(self):
        subs = {"a": Counter({"@": 100, "4": 50}), "e": Counter({"3": 80})}
        variants = generate_variants("base", subs)
        variant_words = [v for _, v in variants]
        assert "b@se" in variant_words
        assert "b4se" in variant_words
        assert "base" not in variant_words  # original word excluded

    def test_no_substitutable_chars(self):
        subs = {"x": Counter({"y": 10})}
        variants = generate_variants("zzz", subs)
        assert variants == []

    def test_cap_respected(self):
        # Word with 5 substitutable positions; cap=64 should never be exceeded.
        subs = {c: Counter({str(i): 10}) for i, c in enumerate("abcde")}
        variants = generate_variants("abcde", subs, cap=64)
        assert len(variants) <= 64

    def test_max_subs_limit(self):
        # With max_subs=1 only single-position substitutions should appear.
        subs = {"a": Counter({"@": 100}), "e": Counter({"3": 80})}
        variants = generate_variants("ace", subs, max_subs=1)
        variant_words = [v for _, v in variants]
        # Double substitution "@c3" should not appear with max_subs=1.
        assert "@c3" not in variant_words
        assert "@ce" in variant_words
        assert "ac3" in variant_words
