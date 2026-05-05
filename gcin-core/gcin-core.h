#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/* Callback fired when gcin commits a character or string */
typedef void (*GcinCommitCb)(const char *utf8, void *userdata);

/* Register the commit callback before calling init or feedkey */
void gcin_core_set_commit_cb(GcinCommitCb cb, void *userdata);

/* Initialize gcin core. table_dir: path to compiled data tables
   (the directory containing cj.gtab, pho.tab, tsin, etc.)
   Returns 0 on success, -1 on failure. */
int gcin_core_init(const char *table_dir);

/* Feed a keypress to the Cangjie engine.
   keyval: IBus/X11 key symbol. modifiers: IBus/X11 modifier bitmask.
   Returns 1 if key was consumed, 0 to pass through to application. */
int gcin_core_feedkey_cangjie(unsigned long keyval, int modifiers);
int gcin_core_feedkey_cangjie_release(unsigned long keyval, int modifiers);

/* Feed a keypress to the Zhuyin engine. Same conventions. */
int gcin_core_feedkey_zhuyin(unsigned long keyval, int modifiers);

/* Reset engine state (e.g. on focus loss) */
void gcin_core_reset(void);

/* Toggle full-width character mode (Shift+Space in gcin).
   In full-width mode all printable ASCII is converted to full-width Unicode
   via gcin's fullchar[] table before being committed. Returns new state. */
int gcin_core_toggle_full_width(void);
int gcin_core_get_full_width(void);

/* Get current Cangjie preedit (key-name glyphs typed so far).
   Returns byte count written to out (not including NUL). */
int gcin_core_get_preedit(char *out, int outlen);

/* Get Cangjie candidates into cands[0..return_value-1].
   Each entry is a NUL-terminated UTF-8 string (<= 31 bytes).
   max_n: size of the cands array. Returns number of candidates. */
int gcin_core_get_candidates_cangjie(char (*cands)[32], int max_n);

/* Get current Zhuyin preedit (bopomofo symbols for syllable typed so far).
   Returns byte count written to out (not including NUL). */
int gcin_core_get_preedit_zhuyin(char *out, int outlen);

/* Get Zhuyin candidates into cands[0..return_value-1].
   Each entry is a NUL-terminated UTF-8 string (<= 31 bytes).
   max_n: size of the cands array. Returns number of candidates. */
int gcin_core_get_candidates_zhuyin(char (*cands)[32], int max_n);

#ifdef __cplusplus
}
#endif
