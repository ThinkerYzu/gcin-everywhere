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

#ifdef __cplusplus
}
#endif
