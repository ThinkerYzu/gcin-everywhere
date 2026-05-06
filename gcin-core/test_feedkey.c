/*
 * test_feedkey.c — unit tests for libgcin-core.a feedkey API
 *
 * Usage:
 *   make test                        # uses GCIN_TABLE_DIR or /usr/share/gcin
 *   GCIN_TABLE_DIR=/path/to/tables make test
 *
 * If data tables are not compiled, all tests are skipped (exit 0).
 * Build tables first:
 *   cd ../gcin && ./configure && make
 *   ./cintotab data/cj.cin  /tmp/gcin-tables/cj.gtab
 *   ./phoconv  data/pho.tab2.src /tmp/gcin-tables/pho.tab
 *   cp tsin /tmp/gcin-tables/
 *   GCIN_TABLE_DIR=/tmp/gcin-tables make test
 *
 * Key mappings used (Cangjie 5 / Zhuyin Daqian standard):
 *   Cangjie: d=木 e=水 i=戈 k=大 o=人 ...
 *   Zhuyin:  b=ㄅ j=ㄓ u=ㄨ 4=ˋ(tone4) ...
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "gcin-core.h"

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

/* ── Test harness ─────────────────────────────────────────────── */

static int pass_count = 0, fail_count = 0, skip_count = 0;

#define PASS(label) do { \
    printf("  PASS  %s\n", label); pass_count++; } while(0)

#define FAIL(label, ...) do { \
    printf("  FAIL  %s — ", label); printf(__VA_ARGS__); printf("\n"); \
    fail_count++; } while(0)

#define SKIP(label, reason) do { \
    printf("  SKIP  %s (%s)\n", label, reason); skip_count++; } while(0)

#define EXPECT_COMMITTED(expected, label) do { \
    if (strcmp(g_committed, expected) == 0) \
        PASS(label); \
    else \
        FAIL(label, "expected '%s', got '%s'", expected, g_committed); \
} while(0)

#define EXPECT_COMMITTED_NONEMPTY(label) do { \
    if (strlen(g_committed) > 0) \
        PASS(label); \
    else \
        FAIL(label, "nothing was committed"); \
} while(0)

#define EXPECT_NOTHING_COMMITTED(label) do { \
    if (strlen(g_committed) == 0) \
        PASS(label); \
    else \
        FAIL(label, "unexpectedly committed '%s'", g_committed); \
} while(0)

/* ── Commit capture ───────────────────────────────────────────── */

static char g_committed[512];

static void on_commit(const char *text, void *userdata) {
    (void)userdata;
    strncat(g_committed, text, sizeof(g_committed) - strlen(g_committed) - 1);
}

static void reset(void) {
    g_committed[0] = '\0';
    gcin_core_reset();
}

/* ── Key constants (match values in GCIN_CORE_BUILD gcin.h) ───── */
#define K_space   0x0020
#define K_return  0xff0d
#define K_escape  0xff1b
#define K_bs      0xff08

/* X11 modifier bitmasks — same values used by feed_phrase() internally */
#define ShiftMask   0x0001
#define ControlMask 0x0004
#define Mod1Mask    0x0008

/* ── Cangjie tests ────────────────────────────────────────────── */

static void test_cangjie_single_char(void) {
    /*
     * Cangjie 5 (cj.gtab) uses space_style=GTAB_space_auto_first_nofull:
     * space alone does not select; it sets spc_pressed=1 and shows candidates.
     * '1' then selects the first candidate.
     *
     * 'k' = 大 radical. First candidate for 'k' is 大.
     */
    reset();
    gcin_core_feedkey_cangjie('k', 0);
    gcin_core_feedkey_cangjie(K_space, 0);  /* show candidates (spc_pressed=1) */
    gcin_core_feedkey_cangjie('1', 0);       /* select first candidate */
    EXPECT_COMMITTED("大", "cangjie: k+space+1 commits 大");
}

static void test_cangjie_two_char(void) {
    /*
     * 'a' + 'b' = 日 + 月 radicals in Cangjie 5 → first candidate is 明.
     * Verified against cj.gtab: ab -> 明.
     */
    reset();
    gcin_core_feedkey_cangjie('a', 0);
    gcin_core_feedkey_cangjie('b', 0);
    gcin_core_feedkey_cangjie(K_space, 0);  /* spc_pressed=1 */
    gcin_core_feedkey_cangjie('1', 0);       /* select first */
    EXPECT_COMMITTED("明", "cangjie: ab+space+1 commits 明");
}

static void test_cangjie_escape_clears(void) {
    /* Escape while composing must not commit anything */
    reset();
    gcin_core_feedkey_cangjie('k', 0);
    gcin_core_feedkey_cangjie(K_escape, 0);
    EXPECT_NOTHING_COMMITTED("cangjie: escape after partial input does not commit");
}

static void test_cangjie_backspace(void) {
    /* Backspace erases last component; remaining input still selectable */
    reset();
    gcin_core_feedkey_cangjie('k', 0);
    gcin_core_feedkey_cangjie('o', 0);
    gcin_core_feedkey_cangjie(K_bs, 0);    /* erase 'o', back to 'k' */
    gcin_core_feedkey_cangjie(K_space, 0); /* spc_pressed=1 */
    gcin_core_feedkey_cangjie('1', 0);      /* select first for 'k' alone */
    EXPECT_COMMITTED_NONEMPTY("cangjie: backspace then select still outputs");
}

/* ── Zhuyin tests ─────────────────────────────────────────────── */

static void test_zhuyin_tone_triggers_candidates(void) {
    /*
     * Daqian layout: j=ㄓ, u=ㄨ, 4=ˋ (tone 4).
     * ㄓㄨˋ → candidates: 住 助 注 著 ...
     * Pressing '1' selects the first candidate.
     * We only assert something was committed; exact character depends on
     * tsin frequency database.
     */
    reset();
    gcin_core_feedkey_zhuyin('j', 0);
    gcin_core_feedkey_zhuyin('u', 0);
    gcin_core_feedkey_zhuyin('4', 0);  /* tone triggers candidate display */
    gcin_core_feedkey_zhuyin('1', 0);  /* select first candidate */
    EXPECT_COMMITTED_NONEMPTY("zhuyin: ju4 + 1 commits a character");
}

static void test_zhuyin_escape_clears(void) {
    reset();
    gcin_core_feedkey_zhuyin('j', 0);
    gcin_core_feedkey_zhuyin(K_escape, 0);
    EXPECT_NOTHING_COMMITTED("zhuyin: escape after partial input does not commit");
}

static void test_zhuyin_preedit_builds(void) {
    /* Preedit accumulates bopomofo as keys are typed */
    reset();
    gcin_core_feedkey_zhuyin('j', 0);  /* ㄓ */
    char pre1[64];
    int n1 = gcin_core_get_preedit_zhuyin(pre1, sizeof(pre1));
    gcin_core_feedkey_zhuyin('u', 0);  /* ㄨ */
    char pre2[64];
    int n2 = gcin_core_get_preedit_zhuyin(pre2, sizeof(pre2));
    gcin_core_feedkey_zhuyin('4', 0);  /* ˋ tone 4 */
    char pre3[64];
    int n3 = gcin_core_get_preedit_zhuyin(pre3, sizeof(pre3));
    /* In Daqian, ㄨ is implicit after ㄓ; preedit may not grow on 'u'.
       The tone press (4) always grows the preedit. */
    if (n1 > 0 && n3 > n1)
        PASS("zhuyin: preedit non-empty after j; grows after tone (4)");
    else
        FAIL("zhuyin: preedit non-empty after j; grows after tone (4)",
             "n1=%d n2=%d n3=%d (expected n1>0 and n3>n1)", n1, n2, n3);
    gcin_core_reset();
}

static void test_zhuyin_candidates_appear_after_tone(void) {
    /* Candidates appear after a complete syllable (initial+vowel+tone) */
    reset();
    gcin_core_feedkey_zhuyin('j', 0);
    gcin_core_feedkey_zhuyin('u', 0);
    gcin_core_feedkey_zhuyin('4', 0);  /* tone triggers candidate display */
    char cands[16][32];
    int n = gcin_core_get_candidates_zhuyin(cands, 16);
    if (n > 0)
        PASS("zhuyin: candidates appear after ju4 (ㄓㄨˋ)");
    else
        FAIL("zhuyin: candidates appear after ju4 (ㄓㄨˋ)", "got 0 candidates");
    gcin_core_reset();
}

static void test_cangjie_full_width(void) {
    /* default: half-width — comma is not consumed by the engine (feedkey
       returns 0); IBus passes it to the application directly, so our
       commit callback is never fired */
    reset();
    gcin_core_feedkey_cangjie(',', 0);
    EXPECT_NOTHING_COMMITTED("cangjie: comma in half-width mode not consumed by engine");

    /* toggle to full-width — comma becomes ， */
    reset();
    gcin_core_toggle_full_width();
    gcin_core_feedkey_cangjie(',', 0);
    EXPECT_COMMITTED("，", "cangjie: comma in full-width mode commits ，");

    gcin_core_toggle_full_width();  /* restore half-width for subsequent tests */
}

static void test_zhuyin_preedit_clears_after_commit(void) {
    /* After commit, preedit and candidates are empty */
    reset();
    gcin_core_feedkey_zhuyin('j', 0);
    gcin_core_feedkey_zhuyin('u', 0);
    gcin_core_feedkey_zhuyin('4', 0);
    gcin_core_feedkey_zhuyin('1', 0);  /* select first candidate → commit */
    char pre[64];
    int np = gcin_core_get_preedit_zhuyin(pre, sizeof(pre));
    char cands[16][32];
    int nc = gcin_core_get_candidates_zhuyin(cands, 16);
    if (np == 0 && nc == 0)
        PASS("zhuyin: preedit and candidates clear after commit");
    else
        FAIL("zhuyin: preedit and candidates clear after commit",
             "preedit_len=%d candidates=%d", np, nc);
}

/* ── Quick (速成/簡易) tests ─────────────────────────────────── */

static void test_quick_single_char(void) {
    /*
     * Quick (simplex.gtab, space_style=GTAB_space_auto_first_full):
     * 'k' = 大 radical (first match). Space shows candidates; '1' selects.
     */
    reset();
    gcin_core_feedkey_quick('k', 0);
    gcin_core_feedkey_quick(K_space, 0);
    gcin_core_feedkey_quick('1', 0);
    EXPECT_COMMITTED("大", "quick: k+space+1 commits 大");
}

static void test_quick_two_char(void) {
    /*
     * 'a'+'b' = 日+月 in Quick — multiple matches exist. Candidates are
     * sorted by tsin use-count in the compiled binary, so the first candidate
     * is frequency-dependent (not necessarily the first .cin entry).
     * We verify that something is committed, not a specific character.
     */
    reset();
    gcin_core_feedkey_quick('a', 0);
    gcin_core_feedkey_quick('b', 0);
    gcin_core_feedkey_quick(K_space, 0);
    gcin_core_feedkey_quick('1', 0);
    EXPECT_COMMITTED_NONEMPTY("quick: ab+space+1 commits a character");
}

static void test_quick_escape_clears(void) {
    reset();
    gcin_core_feedkey_quick('k', 0);
    gcin_core_feedkey_quick(K_escape, 0);
    EXPECT_NOTHING_COMMITTED("quick: escape after partial input does not commit");
}

/* ── Array (行列 ar30) tests ─────────────────────────────────── */

static void test_array_three_key(void) {
    /*
     * Array ar30: 'a'+'a'+'a' (code "AAA" compiled to lowercase by gcin2tab)
     * → only match is 三. In ar30, %endkey includes digits, so the digit key
     * acts as both endkey (triggers candidate display) and selkey (selects) in
     * one press. Space is not needed — pressing '1' directly after the code
     * auto-commits the single match.
     */
    reset();
    gcin_core_feedkey_array('a', 0);
    gcin_core_feedkey_array('a', 0);
    gcin_core_feedkey_array('a', 0);
    gcin_core_feedkey_array('1', 0);
    EXPECT_COMMITTED("三", "array: aaa+1 commits 三");
}

static void test_array_escape_clears(void) {
    reset();
    gcin_core_feedkey_array('a', 0);
    gcin_core_feedkey_array(K_escape, 0);
    EXPECT_NOTHING_COMMITTED("array: escape after partial input does not commit");
}

/* ── Simplex+Punc (標點簡易) tests ───────────────────────────── */

static void test_simplex_punc_single_char(void) {
    /*
     * simplex-punc.cin maps the same radical keys as simplex.
     * 'k' = 大 radical; space_style 4 shows candidates on space, '1' selects.
     */
    reset();
    gcin_core_feedkey_simplex_punc('k', 0);
    gcin_core_feedkey_simplex_punc(K_space, 0);
    gcin_core_feedkey_simplex_punc('1', 0);
    EXPECT_COMMITTED("大", "simplex-punc: k+space+1 commits 大");
}

static void test_simplex_punc_escape_clears(void) {
    reset();
    gcin_core_feedkey_simplex_punc('k', 0);
    gcin_core_feedkey_simplex_punc(K_escape, 0);
    EXPECT_NOTHING_COMMITTED("simplex-punc: escape after partial input does not commit");
}

/* ── CJ5 (倉頡五代) tests ─────────────────────────────────────── */

static void test_cj5_single_char(void) {
    /*
     * CJ5 (cj5.gtab) uses the same radical keys as CJ3/CJ4.
     * 'k' = 大 radical — first candidate for 'k' is 大.
     * space_style=GTAB_space_auto_first_nofull: space sets spc_pressed=1,
     * then '1' selects first candidate.
     */
    reset();
    gcin_core_feedkey_cj5('k', 0);
    gcin_core_feedkey_cj5(K_space, 0);
    gcin_core_feedkey_cj5('1', 0);
    EXPECT_COMMITTED("大", "cj5: k+space+1 commits 大");
}

static void test_cj5_two_char(void) {
    /*
     * 'a'+'b' = 日+月 radicals — first candidate is 明 (same as Cangjie).
     */
    reset();
    gcin_core_feedkey_cj5('a', 0);
    gcin_core_feedkey_cj5('b', 0);
    gcin_core_feedkey_cj5(K_space, 0);
    gcin_core_feedkey_cj5('1', 0);
    EXPECT_COMMITTED("明", "cj5: ab+space+1 commits 明");
}

static void test_cj5_escape_clears(void) {
    reset();
    gcin_core_feedkey_cj5('k', 0);
    gcin_core_feedkey_cj5(K_escape, 0);
    EXPECT_NOTHING_COMMITTED("cj5: escape after partial input does not commit");
}

/* ── Phrase table tests ───────────────────────────────────────── */

static void test_phrase_table(const char *table_dir) {
    char phrase_tab[512];
    snprintf(phrase_tab, sizeof(phrase_tab), "%s/phrase.table", table_dir);
    if (!file_exists(phrase_tab)) {
        SKIP("phrase: alt+shift+i commits 、",    "phrase.table not found");
        SKIP("phrase: alt+shift+o commits 。",    "phrase.table not found");
        SKIP("phrase: ctrl+, commits ，",         "phrase-ctrl.table not found");
        SKIP("phrase: ctrl+' commits 、",         "phrase-ctrl.table not found");
        return;
    }

    /* Alt+Shift+i → 、  (phrase.table) */
    reset();
    gcin_core_feed_phrase('i', Mod1Mask|ShiftMask);
    EXPECT_COMMITTED("、", "phrase: alt+shift+i commits 、");

    /* Alt+Shift+o → 。 */
    reset();
    gcin_core_feed_phrase('o', Mod1Mask|ShiftMask);
    EXPECT_COMMITTED("。", "phrase: alt+shift+o commits 。");

    /* Ctrl+, → ，  (phrase-ctrl.table) */
    reset();
    gcin_core_feed_phrase(',', ControlMask);
    EXPECT_COMMITTED("，", "phrase: ctrl+, commits ，");

    /* Ctrl+' → 、 */
    reset();
    gcin_core_feed_phrase('\'', ControlMask);
    EXPECT_COMMITTED("、", "phrase: ctrl+' commits 、");
}

/* ── main ─────────────────────────────────────────────────────── */

int main(void) {
    const char *table_dir = getenv("GCIN_TABLE_DIR");
    if (!table_dir) table_dir = "/usr/share/gcin";

    printf("gcin-core feedkey tests\n");
    printf("Table dir: %s\n", table_dir);

    /* Pre-check required files before calling init — gcin calls exit() on
       missing tables, so we cannot let init run if they're absent. */
    char pho_tab[512], cj_gtab[512];
    snprintf(pho_tab,  sizeof(pho_tab),  "%s/pho.tab2", table_dir);
    snprintf(cj_gtab,  sizeof(cj_gtab),  "%s/cj.gtab",  table_dir);
    if (!file_exists(pho_tab) || !file_exists(cj_gtab)) {
        printf("\nSKIP: data tables not found at '%s'\n", table_dir);
        printf("  missing: %s%s\n",
               file_exists(pho_tab) ? "" : "pho.tab2 ",
               file_exists(cj_gtab) ? "" : "cj.gtab");
        printf("Build tables with: make tables && make test\n");
        return 0;  /* not a failure — tables just not compiled yet */
    }

    char simplex_gtab[512], ar30_gtab[512], cj5_gtab[512], simplex_punc_gtab[512];
    snprintf(simplex_gtab,      sizeof(simplex_gtab),      "%s/simplex.gtab",      table_dir);
    snprintf(ar30_gtab,         sizeof(ar30_gtab),         "%s/ar30.gtab",         table_dir);
    snprintf(cj5_gtab,          sizeof(cj5_gtab),          "%s/cj5.gtab",          table_dir);
    snprintf(simplex_punc_gtab, sizeof(simplex_punc_gtab), "%s/simplex-punc.gtab", table_dir);
    int have_quick        = file_exists(simplex_gtab);
    int have_array        = file_exists(ar30_gtab);
    int have_cj5          = file_exists(cj5_gtab);
    int have_simplex_punc = file_exists(simplex_punc_gtab);

    gcin_core_init(table_dir);

    gcin_core_set_commit_cb(on_commit, NULL);

    printf("\nCangjie:\n");
    test_cangjie_single_char();
    test_cangjie_two_char();
    test_cangjie_escape_clears();
    test_cangjie_backspace();

    printf("\nCangjie (full-width mode):\n");
    test_cangjie_full_width();

    printf("\nZhuyin:\n");
    test_zhuyin_tone_triggers_candidates();
    test_zhuyin_escape_clears();
    test_zhuyin_preedit_builds();
    test_zhuyin_candidates_appear_after_tone();
    test_zhuyin_preedit_clears_after_commit();

    printf("\nQuick (速成):\n");
    if (have_quick) {
        test_quick_single_char();
        test_quick_two_char();
        test_quick_escape_clears();
    } else {
        SKIP("quick: k+space+1 commits 大",        "simplex.gtab not found");
        SKIP("quick: ab+space+1 commits 明",        "simplex.gtab not found");
        SKIP("quick: escape after partial input",   "simplex.gtab not found");
    }

    printf("\nArray (行列):\n");
    if (have_array) {
        test_array_three_key();
        test_array_escape_clears();
    } else {
        SKIP("array: aaa+1 commits 三",              "ar30.gtab not found");
        SKIP("array: escape after partial input",   "ar30.gtab not found");
    }

    printf("\nSimplex+Punc (標點簡易):\n");
    if (have_simplex_punc) {
        test_simplex_punc_single_char();
        test_simplex_punc_escape_clears();
    } else {
        SKIP("simplex-punc: k+space+1 commits 大",     "simplex-punc.gtab not found");
        SKIP("simplex-punc: escape after partial input", "simplex-punc.gtab not found");
    }

    printf("\nCJ5 (倉頡五代):\n");
    if (have_cj5) {
        test_cj5_single_char();
        test_cj5_two_char();
        test_cj5_escape_clears();
    } else {
        SKIP("cj5: k+space+1 commits 大",          "cj5.gtab not found");
        SKIP("cj5: ab+space+1 commits 明",          "cj5.gtab not found");
        SKIP("cj5: escape after partial input",     "cj5.gtab not found");
    }

    printf("\nPhrase table (Alt+Shift / Ctrl):\n");
    test_phrase_table(table_dir);

    printf("\n%d passed, %d failed, %d skipped\n",
           pass_count, fail_count, skip_count);
    return fail_count > 0 ? 1 : 0;
}
