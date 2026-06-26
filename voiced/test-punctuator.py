#!/usr/bin/env python3
"""Unit test for the punctuation-restoration step in gcin-voiced.py.

The safety guards (word-skeleton equality, fall-back-on-error, _clean wrappers)
run with no dependencies. The live round-trip is attempted only if Ollama is
reachable; otherwise it is skipped so CI without a GPU/Ollama still passes.

    python3 test-punctuator.py
"""

import importlib.util
import os
import urllib.error

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location(
    "gcin_voiced", os.path.join(HERE, "gcin-voiced.py"))
gv = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gv)


def test_word_skeleton():
    assert gv._word_skeleton("救命啊，快來救我。") == "救命啊快來救我"
    assert gv._word_skeleton("你好，世界！") == "你好世界"
    assert gv._word_skeleton("a, b. c!") == "abc"
    assert gv._word_skeleton("語音 輸入 測試") == "語音輸入測試"   # whitespace ignored
    print("ok: word-skeleton strips punctuation + whitespace")


def test_empty_passthrough():
    p = gv.Punctuator(enabled=True)
    assert p.add_punct("") == ""
    assert p.add_punct("   ") == "   "
    print("ok: empty/blank text passes through untouched")


def test_disabled_passthrough():
    p = gv.Punctuator(enabled=False)
    p._call = lambda text: (_ for _ in ()).throw(AssertionError("called while disabled"))
    assert p.add_punct("沒有標點") == "沒有標點"
    print("ok: disabled punctuator returns input verbatim")


def test_accepts_valid_punctuation():
    p = gv.Punctuator(enabled=True)
    p._call = lambda text: "救命啊，快來救我。"      # same chars, punctuation added
    assert p.add_punct("救命啊快來救我") == "救命啊，快來救我。"
    print("ok: valid punctuation-only edit is accepted")


def test_wording_change_falls_back():
    p = gv.Punctuator(enabled=True)
    p._call = lambda text: "汝好，世界。"            # translated 你→汝: wording changed
    assert p.add_punct("你好世界") == "你好世界"
    print("ok: wording change (e.g. translation) is rejected, raw kept")


def test_dropped_char_falls_back():
    p = gv.Punctuator(enabled=True)
    p._call = lambda text: "你好世。"                # dropped a 界
    assert p.add_punct("你好世界") == "你好世界"
    print("ok: dropped character is rejected, raw transcript kept")


def test_error_falls_back():
    p = gv.Punctuator(enabled=True)

    def boom(text):
        raise OSError("connection refused")
    p._call = boom
    assert p.add_punct("沒有標點") == "沒有標點"
    print("ok: Ollama-down error falls back to raw transcript")


def test_clean_strips_wrappers_and_reasoning():
    c = gv.Punctuator._clean
    assert c('"你好。"') == "你好。"
    assert c("```\n你好。\n```") == "你好。"
    assert c("「你好。」") == "你好。"
    assert c("<think>add commas</think>\n你好。") == "你好。"
    print("ok: _clean strips <think>, quotes, code fences")


def test_live_ollama_optional():
    """Real round-trip; skipped if Ollama isn't reachable."""
    p = gv.Punctuator(enabled=True, timeout=90)
    raw = "你好我今天想去台北車站"
    try:
        out = gv.Punctuator._clean(p._call(raw))
    except (urllib.error.URLError, OSError) as e:
        print(f"skip: live Ollama not reachable ({e})")
        return
    assert gv._word_skeleton(out) == raw, f"live model changed wording: {out!r}"
    assert out != raw, "live model added no punctuation"
    print(f"ok: live Ollama round-trip added punctuation -> {out}")


def main():
    test_word_skeleton()
    test_empty_passthrough()
    test_disabled_passthrough()
    test_accepts_valid_punctuation()
    test_wording_change_falls_back()
    test_dropped_char_falls_back()
    test_error_falls_back()
    test_clean_strips_wrappers_and_reasoning()
    test_live_ollama_optional()
    print("\nALL PUNCTUATOR TESTS PASSED")


if __name__ == "__main__":
    main()
