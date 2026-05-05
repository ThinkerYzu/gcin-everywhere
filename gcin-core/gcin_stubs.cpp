#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "gcin-core.h"
#include "../gcin/gcin.h"
#include "../gcin/pho.h"
#include "../gcin/tsin.h"
#include "../gcin/gst.h"

/* ── Globals from excluded files expected by gcin source ─────── */

/* From eve.cpp */
gboolean test_mode = 0;
int current_in_win_x = -1, current_in_win_y = -1;

/* From gcin.cpp */
int win_xl = 0, win_yl = 0;
int win_x  = 0, win_y  = 0;
int dpy_xl = 1920, dpy_yl = 1080;

/* From gcin-settings.cpp */
int gcin_font_size = 16;

/* From win-gtab.cpp */
GtkWidget *gwin_gtab    = NULL;
int        win_gtab_max_key_press = 10;
gboolean   last_cursor_off = 0;

/* From win-pho.cpp */
GtkWidget *gwin_pho     = NULL;

/* From win1.cpp */
GtkWidget *gwin1        = NULL;

/* From gcin-common.cpp / pho2pinyin.cpp */
PIN_JUYIN *pin_juyin    = NULL;
int        text_pho_N   = 3;

/* current_CS: gcin tracks the active X11 client.
   IBus engine is single-client; use a static instance. */
static ClientState _cs  = {0};
ClientState *current_CS = &_cs;

/* ── Output callback ──────────────────────────────────────────── */
static GcinCommitCb g_commit_cb   = NULL;
static void        *g_commit_data = NULL;

void gcin_core_set_commit_cb(GcinCommitCb cb, void *userdata) {
    g_commit_cb   = cb;
    g_commit_data = userdata;
}

void send_text(char *text) {
    if (g_commit_cb) g_commit_cb(text, g_commit_data);
}

void send_utf8_ch(char *ch) {
    char buf[CH_SZ + 1];
    memcpy(buf, ch, CH_SZ);
    buf[CH_SZ] = '\0';
    if (g_commit_cb) g_commit_cb(buf, g_commit_data);
}

void send_ascii(char key) {
    char buf[2] = { key, '\0' };
    if (g_commit_cb) g_commit_cb(buf, g_commit_data);
}

/* ── gcin_core_init ───────────────────────────────────────────── */
extern void init_TableDir(void);
extern void load_tab_pho_file(void);
extern void load_tsin_db(int reload);

int gcin_core_init(const char *table_dir) {
    if (table_dir && *table_dir) {
        TableDir = (char *)table_dir;
    }
    init_TableDir();
    load_tab_pho_file();
    load_tsin_db(0);
    return 0;
}

/* ── gcin_core_feedkey ────────────────────────────────────────── */
extern int feedkey_gtab(KeySym key, int kbstate);
extern int feedkey_gtab_release(KeySym key, int kbstate);
extern int feedkey_pho(KeySym xkey, int kbstate);

int gcin_core_feedkey_cangjie(unsigned long keyval, int modifiers) {
    return feedkey_gtab((KeySym)keyval, modifiers);
}

int gcin_core_feedkey_cangjie_release(unsigned long keyval, int modifiers) {
    return feedkey_gtab_release((KeySym)keyval, modifiers);
}

int gcin_core_feedkey_zhuyin(unsigned long keyval, int modifiers) {
    return feedkey_pho((KeySym)keyval, modifiers);
}

void gcin_core_reset(void) {
    /* TODO: call gcin reset functions when identified */
}

/* ── Utility implementations from excluded gcin-common.cpp ───── */

/* Flip alpha KeySym case: lowercase ↔ uppercase (X11 range 0x41-0x7a) */
void case_inverse(KeySym *xkey, int shift_m) {
    if (*xkey >= 'a' && *xkey <= 'z')
        *xkey -= 0x20;
    else if (*xkey >= 'A' && *xkey <= 'Z')
        *xkey += 0x20;
}

/* Monotonic time in microseconds */
gint64 current_time() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (gint64)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000LL;
}

/* ── UI stubs — void ──────────────────────────────────────────── */
void show_win_gtab(void)            {}
void hide_win_gtab(void)            {}
void show_win_pho(void)             {}
void hide_win_pho(void)             {}
void hide_win_kbm(void)             {}
void hide_win0(void)                {}
void hide_row2_if_necessary(void)   {}
void minimize_win_gtab(void)        {}
void minimize_win_pho(void)         {}
void disp_gtab(char *s)             { (void)s; }
void disp_gbuf(void)                {}
void disp_gtab_sel(char *s)         { (void)s; }
void disp_gtab_pre_sel(char *s)     { (void)s; }
void disp_pho(int i, char *s)       { (void)i; (void)s; }
void disp_pho_sel(char *s)          { (void)s; }
void disp_pho_sub(GtkWidget *l, int i, char *s) { (void)l; (void)i; (void)s; }
void disp_label_edit(char *s)       { (void)s; }
void disp_char(int i, char *s)      { (void)i; (void)s; }
void clear_gtab_input_error_color(void) {}
void set_gtab_input_error_color(void)   {}
void set_key_codes_label(char *s, int b) { (void)s; (void)b; }
void set_page_label(char *s)        { (void)s; }
void set_label_font_size(GtkWidget *l, int sz) { (void)l; (void)sz; }
void set_label_space(GtkWidget *l)  { (void)l; }
void set_no_focus(GtkWidget *w)     { (void)w; }
void clr_tsin_cursor(int i)         { (void)i; }
void bell(void)                     {}
void disp_tray_icon(void)           {}
void save_CS_current_to_temp(void)  {}
void show_tsin_stat(void)           {}
void recreate_win1_if_nessary(void) {}
void start_gtab_pho_query(char *s)  { (void)s; }
void pho_play(phokey_t k)           { (void)k; }
void gtab_scan_pre_select(gboolean b) { (void)b; }
void hide_gtab_pre_sel(void)        {}
void change_win_fg_bg(GtkWidget *w, GtkWidget *l) { (void)w; (void)l; }
void change_win_bg(GtkWidget *w)    { (void)w; }
void exec_gcin_setup(void)          {}
void get_win_size(GtkWidget *w, int *wd, int *ht) { (void)w; if(wd)*wd=0; if(ht)*ht=0; }
void win32_init_win(GtkWidget *w)   { (void)w; }

/* From tsin.cpp (declared locally there as extern void show_win0()) */
void show_win0(void)                {}

/* create_win_save_phrase — takes WSP_S* which needs win-save-phrase.h;
   forward-declare rather than include to avoid pulling in GtkWidget struct defs */
struct WSP_S;
void create_win_save_phrase(struct WSP_S *wsp, int wspN) { (void)wsp; (void)wspN; }

/* ── UI stubs — boolean ───────────────────────────────────────── */
gboolean full_char_proc(KeySym k)          { (void)k; return FALSE; }
gboolean shift_char_proc(KeySym k, int s)  { (void)k; (void)s; return FALSE; }
gboolean pre_punctuation(KeySym k)         { (void)k; return FALSE; }
gboolean pre_punctuation_hsu(KeySym k)     { (void)k; return FALSE; }
gboolean gcin_edit_display_ap_only(void)   { return FALSE; }
gboolean gcin_display_on_the_spot_key(void){ return FALSE; }

/* ── Misc stubs ───────────────────────────────────────────────── */
void char_play(char *utf8)          { (void)utf8; }
void check_CS(void)                 {}
void skip_utf8_sigature(FILE *fp)   { (void)fp; }
