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

/* ── Cangjie tests ────────────────────────────────────────────── */

static void test_cangjie_single_char(void) {
    /*
     * 'k' = 大 radical in Cangjie 5. Pressing k+space should commit 大
     * (assuming 大 is a valid one-component character in the table).
     * If the commit value changes, update the expected string here.
     */
    reset();
    gcin_core_feedkey_cangjie('k', 0);
    gcin_core_feedkey_cangjie(K_space, 0);
    EXPECT_COMMITTED("大", "cangjie: k+space commits 大");
}

static void test_cangjie_two_char(void) {
    /*
     * Cangjie code for 大人: k (大 radical) + o (人 radical).
     * space selects the first candidate.
     * Expected: 大人 (or another character sharing that code; update if needed).
     */
    reset();
    gcin_core_feedkey_cangjie('k', 0);
    gcin_core_feedkey_cangjie('o', 0);
    gcin_core_feedkey_cangjie(K_space, 0);
    EXPECT_COMMITTED_NONEMPTY("cangjie: ko+space commits something");
}

static void test_cangjie_escape_clears(void) {
    /* Escape while composing must not commit anything */
    reset();
    gcin_core_feedkey_cangjie('k', 0);
    gcin_core_feedkey_cangjie(K_escape, 0);
    EXPECT_NOTHING_COMMITTED("cangjie: escape after partial input does not commit");
}

static void test_cangjie_backspace(void) {
    /* Backspace erases last input component, commit still works after */
    reset();
    gcin_core_feedkey_cangjie('k', 0);
    gcin_core_feedkey_cangjie('o', 0);
    gcin_core_feedkey_cangjie(K_bs, 0);   /* erase 'o' */
    gcin_core_feedkey_cangjie(K_space, 0); /* commit 'k' alone */
    EXPECT_COMMITTED_NONEMPTY("cangjie: backspace then commit still outputs");
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
        printf("Build tables and retry:\n");
        printf("  cd ../gcin && ./configure && make\n");
        printf("  mkdir /tmp/gcin-tables\n");
        printf("  ./cintotab data/cj.cin /tmp/gcin-tables/cj.gtab\n");
        printf("  ./phoconv  data/pho.tab2.src /tmp/gcin-tables/pho.tab\n");
        printf("  cp tsin /tmp/gcin-tables/\n");
        printf("  GCIN_TABLE_DIR=/tmp/gcin-tables make test\n");
        return 0;  /* not a failure — tables just not compiled yet */
    }

    gcin_core_init(table_dir);

    gcin_core_set_commit_cb(on_commit, NULL);

    printf("\nCangjie:\n");
    test_cangjie_single_char();
    test_cangjie_two_char();
    test_cangjie_escape_clears();
    test_cangjie_backspace();

    printf("\nZhuyin:\n");
    test_zhuyin_tone_triggers_candidates();
    test_zhuyin_escape_clears();

    printf("\n%d passed, %d failed, %d skipped\n",
           pass_count, fail_count, skip_count);
    return fail_count > 0 ? 1 : 0;
}
