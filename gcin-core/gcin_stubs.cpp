#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "gcin-core.h"
#include "../gcin/gcin.h"
#include "../gcin/gtab.h"
#include "../gcin/pho.h"
#include "../gcin/tsin.h"
#include "../gcin/gst.h"
#include "../gcin/win1.h"

/* ── Globals from excluded files expected by gcin source ─────── */

/* From eve.cpp */
gboolean test_mode = 0;
int current_in_win_x = -1, current_in_win_y = -1;

/* From gcin.cpp */
int win_xl = 0, win_yl = 0;
int win_x  = 0, win_y  = 0;
int dpy_xl = 1920, dpy_yl = 1080;

/* gcin_font_size: defined in gcin-settings.cpp (compiled) */

/* From win-gtab.cpp */
GtkWidget *gwin_gtab    = NULL;
int        win_gtab_max_key_press = 10;
gboolean   last_cursor_off = 0;

/* From win-pho.cpp */
GtkWidget *gwin_pho     = NULL;

/* From win0.cpp */
GtkWidget *gwin0        = NULL;

/* From win1.cpp */
GtkWidget *gwin1        = NULL;

/* From gcin-common.cpp / pho2pinyin.cpp */
PIN_JUYIN *pin_juyin    = NULL;
int        text_pho_N   = 3;

/* From gcin.cpp */
gboolean win_kbm_inited = 0;
int      b_show_win_kbm = 0;

/* From gcin-common.cpp */
gboolean b_use_full_space = 0;

/* current_CS: gcin tracks the active X11 client.
   IBus engine is single-client; use a static instance. */
static ClientState _cs;
ClientState *current_CS = &_cs;
/* im_state must be GCIN_STATE_CHINESE (2) for feedkey_gtab/feedkey_pho to process keys */
__attribute__((constructor)) static void init_cs(void) {
    memset(&_cs, 0, sizeof(_cs));
    _cs.im_state = 2; /* GCIN_STATE_CHINESE */
}

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

/* pho-util.cpp calls update_table_file() to copy system tables to ~/.gcin/.
   In gcin-everywhere we load tables from a specified dir, so no user-local
   copy is needed; this stub silences the spurious "mv: cannot stat" error. */
void update_table_file(char *name, int version) { (void)name; (void)version; }

/* ── gcin_core_init ───────────────────────────────────────────── */
extern void init_TableDir(void);
extern void load_tab_pho_file(void);
extern void load_tsin_db(int reload);
extern void load_setttings(void);       /* note: typo is in gcin source */
extern void load_gtab_list(int skip_disabled);
extern void init_gtab(int inmdno);

/* Find first gtab entry whose filename contains needle; return index or -1 */
static int find_inmd(const char *needle) {
    for (int i = 0; i < inmdN; i++) {
        if (inmd[i].filename && strstr(inmd[i].filename, needle))
            return i;
    }
    return -1;
}

static int g_cangjie_inmd = -1;
static int g_zhuyin_inmd  = -1;

int gcin_core_init(const char *table_dir) {
    if (table_dir && *table_dir) {
        TableDir = (char *)table_dir;
        /* pho_load() branches on getenv("GCIN_TABLE_DIR"), not TableDir directly */
        setenv("GCIN_TABLE_DIR", table_dir, 1);
    }
    load_setttings();           /* initializes pho_kbm_name, pho_selkey, etc. */
    gtab_auto_select_by_phrase = 2; /* GTAB_OPTION_NO: commit each char directly, no phrase buffer */
    init_TableDir();
    load_gtab_list(1);          /* populates inmd[] from gtab.list */
    load_tab_pho_file();        /* loads pho.tab2 / zo.kbm for Zhuyin */
    load_tsin_db(0);            /* loads tsin32 word-frequency database */

    /* Pre-load Cangjie gtab so cur_inmd is set on first feedkey_cangjie call */
    g_cangjie_inmd = find_inmd("cj");
    if (g_cangjie_inmd >= 0) {
        init_gtab(g_cangjie_inmd);
        current_CS->in_method  = g_cangjie_inmd;
        current_CS->tsin_pho_mode = 1;  /* required for feedkey_gtab to process keys */
    }

    g_zhuyin_inmd = find_inmd("pho");  /* Zhuyin uses method_type_PHO */
    return 0;
}

/* ── gcin_core_feedkey ────────────────────────────────────────── */
extern int feedkey_gtab(KeySym key, int kbstate);
extern int feedkey_gtab_release(KeySym key, int kbstate);
extern int feedkey_pho(KeySym xkey, int kbstate);

int gcin_core_feedkey_cangjie(unsigned long keyval, int modifiers) {
    if (g_cangjie_inmd >= 0 && current_CS->in_method != g_cangjie_inmd) {
        current_CS->in_method = g_cangjie_inmd;
        init_gtab(g_cangjie_inmd);
    }
    return feedkey_gtab((KeySym)keyval, modifiers);
}

int gcin_core_feedkey_cangjie_release(unsigned long keyval, int modifiers) {
    return feedkey_gtab_release((KeySym)keyval, modifiers);
}

int gcin_core_feedkey_zhuyin(unsigned long keyval, int modifiers) {
    return feedkey_pho((KeySym)keyval, modifiers);
}

int gcin_core_get_preedit(char *out, int outlen) {
    extern int get_DispInArea_str(char *out);
    char buf[512];
    int n = get_DispInArea_str(buf);
    if (n >= outlen) n = outlen - 1;
    memcpy(out, buf, n);
    out[n] = '\0';
    return n;
}

int gcin_core_get_candidates_cangjie(char (*cands)[32], int max_n) {
    extern char **seltab;
    if (!seltab || !cur_inmd) return 0;
    int n = 0;
    for (int i = 0; i < cur_inmd->M_DUP_SEL && n < max_n; i++) {
        if (seltab[i] && seltab[i][0]) {
            strncpy(cands[n], seltab[i], 31);
            cands[n][31] = '\0';
            n++;
        }
    }
    return n;
}

int gcin_core_get_preedit_zhuyin(char *out, int outlen) {
    if (!poo.typ_pho[0] && !poo.typ_pho[1] && !poo.typ_pho[2] && !poo.typ_pho[3]) {
        out[0] = '\0';
        return 0;
    }
    phokey_t key = pho2key(poo.typ_pho);
    char *s = phokey_to_str(key);
    int n = strlen(s);
    if (n >= outlen) n = outlen - 1;
    memcpy(out, s, n);
    out[n] = '\0';
    return n;
}

int gcin_core_get_candidates_zhuyin(char (*cands)[32], int max_n) {
    extern PHO_ITEM *ch_pho;
    if (!ch_pho || !poo.maxi) return 0;
    int n = 0;
    int ii = poo.start_idx + poo.cpg;
    for (int i = 0; i < poo.maxi && n < max_n && ii < poo.stop_idx; i++, ii++) {
        memcpy(cands[n], ch_pho[ii].ch, CH_SZ);
        cands[n][CH_SZ] = '\0';
        n++;
    }
    return n;
}

void gcin_core_reset(void) {
    extern void ClrIn(void);
    extern void clrin_pho(void);
    ClrIn();        /* clears ggg.spc_pressed, ggg.ci, composition state */
    clrin_pho();    /* clears Zhuyin phonetic buffer */
}

/* ── Full-width character mode ────────────────────────────────── */

/* Shift+Space toggles full-width mode in gcin (toggle_half_full_char in eve.cpp).
   We expose a simple API rather than copying the full toggle function, which
   has display-update side effects (disp_im_half_full etc.) not relevant here. */
int gcin_core_toggle_full_width(void) {
    current_CS->b_half_full_char = !current_CS->b_half_full_char;
    return current_CS->b_half_full_char;
}

int gcin_core_get_full_width(void) {
    return current_CS->b_half_full_char;
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
void minimize_win_gtab(void)        {}
void minimize_win_pho(void)         {}
void disp_gtab(char *s)             { (void)s; }
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
void recreate_win1_if_nessary(void) {}
void pho_play(phokey_t k)           { (void)k; }
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

/* full_char_proc: copied from eve.cpp (excluded — contains XTest, GDK, and
   hundreds of X11-dependent functions that can't be guarded economically).
   The function itself has no X11/GTK calls; it only uses fullchar[], utf8cpy(),
   send_text(), and gcin state already available in the core build.
   Simplified: TSIN-mode and phrase-buffer branches are both inactive in our
   build (current_method_type() returns 0, phrase buffer disabled), so they
   are omitted. */
gboolean full_char_proc(KeySym keysym) {
    char *s = half_char_to_full_char(keysym);
    if (!s) return 0;
    char tt[CH_SZ + 1];
    utf8cpy(tt, s);
    send_text(tt);
    return 1;
}
gboolean gcin_edit_display_ap_only(void)   { return FALSE; }
gboolean gcin_display_on_the_spot_key(void){ return FALSE; }

/* ── Misc stubs ───────────────────────────────────────────────── */
void char_play(char *utf8)          { (void)utf8; }
void check_CS(void)                 {}
/* skip_utf8_sigature: defined in locale.cpp (compiled) */

/* ── Functions from excluded eve.cpp / gcin.cpp ─────────────── */
char current_method_type(void)      { return 0; }
void init_in_method(void)           {}

/* ── Functions from excluded win-sym.cpp ────────────────────── */
gboolean win_sym_page_up(void)      { return FALSE; }
gboolean win_sym_page_down(void)    { return FALSE; }

/* ── Functions from excluded gcin.cpp ───────────────────────── */

/* half_char_to_full_char: copied from gcin.cpp (excluded — defines globals
   like dpy/root/win_xl that would conflict with our stubs, plus main() and
   X11/XIM setup code). The function itself is a one-liner into fullchar[]
   which is already compiled from fullchar.cpp. */
char *half_char_to_full_char(KeySym xkey) {
    extern unich_t *fullchar[];
    if (xkey < ' ' || xkey > 127) return NULL;
    return fullchar[xkey - ' '];
}

/* ── Pinyin stubs (only needed for Pinyin mode) ─────────────── */
void load_pin_juyin(void)                    {}
gboolean inph_typ_pho_pinyin(int k)          { (void)k; return FALSE; }

/* ── Win1 display stubs ──────────────────────────────────────── */
void set_win1_cb(cb_selec_by_idx_t a, cb_page_ud_t b, cb_page_ud_t c) { (void)a; (void)b; (void)c; }
void init_tsin_selection_win(void)  {}
void clear_sele(void)               {}
void set_sele_text(int tN, int i, char *t, int l) { (void)tN; (void)i; (void)t; (void)l; }
void disp_arrow_up(void)            {}
void disp_arrow_down(void)          {}
void disp_selections(int x, int y)  { (void)x; (void)y; }
void hide_selections_win(void)      {}

/* ── Win0 display stubs ──────────────────────────────────────── */
void clear_chars_all(void)          {}
void hide_char(int i)               { (void)i; }
void set_cursor_tsin(int i)         { (void)i; }
void disp_tsin_pho(int i, char *s)  { (void)i; (void)s; }
void clr_in_area_pho_tsin(void)     {}
void compact_win0(void)             {}
void disp_tsin_eng_pho(int e)       { (void)e; }
void disp_tsin_select(int i)        { (void)i; }
void show_button_pho(gboolean b)    { (void)b; }

/* ── Win-gtab display stubs ──────────────────────────────────── */
void clear_gtab_in_area(void)       {}
void get_win_gtab_geom(void)        {}
void init_gtab_pho_query_win(void)  {}
void set_key_codes_label_pho(char *s) { (void)s; }

/* ── Win-pho-near stubs ──────────────────────────────────────── */
void create_win_pho_near(phokey_t p)  { (void)p; }
void close_win_pho_near(void)         {}

/* ── Win-gtab display stubs (additional) ────────────────────── */
void gtab_disp_empty(char *tt, int N) { (void)tt; (void)N; }

/* ── watch_fopen: file-watch utility — just open the file ─────── */
FILE *watch_fopen(char *filename, time_t *pfile_modify_time) {
    FILE *fp = fopen(filename, "rb");
    if (fp && pfile_modify_time) {
        struct stat st;
        if (fstat(fileno(fp), &st) == 0) *pfile_modify_time = st.st_mtime;
    }
    return fp;
}
