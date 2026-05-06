#include <stdio.h>
#include <ibus.h>
#include "../gcin-core/gcin-core.h"

/* X11 modifier bitmasks — passed to gcin_core_feed_phrase() which forwards
   them to feed_phrase() inside libgcin-core (expects X11 values, not IBus). */
#define ShiftMask   0x0001
#define ControlMask 0x0004
#define Mod1Mask    0x0008

typedef struct _GcinEngine      GcinEngine;
typedef struct _GcinEngineClass GcinEngineClass;

struct _GcinEngine {
    IBusEngine       parent;
    IBusLookupTable *table;
    int              mode;         /* 0=Cangjie, 1=Zhuyin, 2=Quick, 3=Array, 4=CJ5, 5=SimplexPunc */
    gboolean         chinese_mode;
};
struct _GcinEngineClass { IBusEngineClass parent; };

G_DEFINE_TYPE(GcinEngine, gcin_engine, IBUS_TYPE_ENGINE)
#define GCIN_TYPE_ENGINE (gcin_engine_get_type())

/* ── Commit callback ─────────────────────────────────────────────── */

static void on_commit(const char *utf8, void *userdata) {
    IBusEngine *e = (IBusEngine *)userdata;
    IBusText *text = ibus_text_new_from_string(utf8);
    ibus_engine_commit_text(e, text);
}

/* ── Preedit + candidates update ─────────────────────────────────── */

static void update_ui(IBusEngine *iengine) {
    GcinEngine *e = (GcinEngine *)iengine;

    /* Preedit */
    char preedit[256];
    if (e->mode == 1)
        gcin_core_get_preedit_zhuyin(preedit, sizeof(preedit));
    else
        gcin_core_get_preedit(preedit, sizeof(preedit));  /* gtab: Cangjie, Quick, Array */
    IBusText *pre = ibus_text_new_from_string(preedit);
    ibus_text_append_attribute(pre, IBUS_ATTR_TYPE_UNDERLINE,
                               IBUS_ATTR_UNDERLINE_SINGLE, 0, -1);
    ibus_engine_update_preedit_text(iengine, pre, g_utf8_strlen(preedit, -1), preedit[0] != '\0');

    /* Candidates */
    ibus_lookup_table_clear(e->table);
    char cands[16][32];
    int n = (e->mode == 1)
        ? gcin_core_get_candidates_zhuyin(cands, 16)
        : gcin_core_get_candidates_cangjie(cands, 16);  /* gtab: Cangjie, Quick, Array */
    for (int i = 0; i < n; i++)
        ibus_lookup_table_append_candidate(e->table,
                                           ibus_text_new_from_string(cands[i]));
    if (n > 0)
        ibus_engine_update_lookup_table(iengine, e->table, TRUE);
    else
        ibus_engine_hide_lookup_table(iengine);
}

/* ── Key event ───────────────────────────────────────────────────── */

static gboolean gcin_engine_process_key_event(IBusEngine *iengine,
        guint keyval, guint keycode, guint modifiers) {
    (void)keycode;
    if (modifiers & IBUS_RELEASE_MASK) return FALSE;
    GcinEngine *e = (GcinEngine *)iengine;
    if (!e->chinese_mode) return FALSE;

    /* Shift+Space: toggle full-width character mode, same as gcin's Shift+Space
       binding (toggle_half_full_char in eve.cpp). Clear any pending composition. */
    if (keyval == IBUS_space && (modifiers & IBUS_SHIFT_MASK)) {
        gcin_core_toggle_full_width();
        gcin_core_reset();
        ibus_engine_hide_preedit_text(iengine);
        ibus_engine_hide_lookup_table(iengine);
        return TRUE;
    }

    /* Alt+Shift: phrase.table lookup — mirrors gcin eve.cpp:1227 */
    if ((modifiers & (IBUS_MOD1_MASK|IBUS_SHIFT_MASK)) == (IBUS_MOD1_MASK|IBUS_SHIFT_MASK)) {
        gcin_core_set_commit_cb(on_commit, iengine);
        return gcin_core_feed_phrase(keyval, Mod1Mask|ShiftMask) ? TRUE : FALSE;
    }

    /* Ctrl (without Alt): phrase-ctrl.table lookup — mirrors gcin eve.cpp:1293.
       Returns FALSE if key not in table so app Ctrl+shortcuts pass through. */
    if ((modifiers & IBUS_CONTROL_MASK) && !(modifiers & IBUS_MOD1_MASK)) {
        gcin_core_set_commit_cb(on_commit, iengine);
        if (gcin_core_feed_phrase(keyval, ControlMask)) return TRUE;
    }

    /* Re-register callback each keypress so commit fires on the active engine */
    gcin_core_set_commit_cb(on_commit, iengine);
    int consumed;
    switch (e->mode) {
        case 1:  consumed = gcin_core_feedkey_zhuyin(keyval, modifiers);  break;
        case 2:  consumed = gcin_core_feedkey_quick(keyval, modifiers);   break;
        case 3:  consumed = gcin_core_feedkey_array(keyval, modifiers);   break;
        case 4:  consumed = gcin_core_feedkey_cj5(keyval, modifiers);          break;
        case 5:  consumed = gcin_core_feedkey_simplex_punc(keyval, modifiers); break;
        default: consumed = gcin_core_feedkey_cangjie(keyval, modifiers);      break;
    }

    update_ui(iengine);
    return consumed ? TRUE : FALSE;
}

/* ── Engine lifecycle ────────────────────────────────────────────── */

static void gcin_engine_enable(IBusEngine *e) {
    GcinEngine *ge = (GcinEngine *)e;
    const gchar *name = ibus_engine_get_name(e);
    if (name && g_str_has_suffix(name, "zhuyin"))      ge->mode = 1;
    else if (name && g_str_has_suffix(name, "quick"))  ge->mode = 2;
    else if (name && g_str_has_suffix(name, "array"))  ge->mode = 3;
    else if (name && g_str_has_suffix(name, "cj5"))           ge->mode = 4;
    else if (name && g_str_has_suffix(name, "simplex-punc"))  ge->mode = 5;
    else                                                       ge->mode = 0;
    gcin_core_reset();
}
static void gcin_engine_disable(IBusEngine *e)   { (void)e; }

static void gcin_engine_reset(IBusEngine *e) {
    gcin_core_reset();
    ibus_engine_hide_preedit_text(e);
    ibus_engine_hide_lookup_table(e);
}

static void gcin_engine_focus_out(IBusEngine *e) {
    gcin_core_reset();
    ibus_engine_hide_preedit_text(e);
    ibus_engine_hide_lookup_table(e);
}

static void gcin_engine_class_init(GcinEngineClass *klass) {
    IBusEngineClass *ec = IBUS_ENGINE_CLASS(klass);
    ec->process_key_event = gcin_engine_process_key_event;
    ec->enable             = gcin_engine_enable;
    ec->disable            = gcin_engine_disable;
    ec->reset              = gcin_engine_reset;
    ec->focus_out          = gcin_engine_focus_out;
}

static void gcin_engine_init(GcinEngine *e) {
    e->table        = ibus_lookup_table_new(10, 0, TRUE, TRUE);
    e->chinese_mode = TRUE;
    e->mode         = 0;  /* set correctly in gcin_engine_enable() */
    g_object_ref_sink(e->table);
}

/* ── main ────────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    /* Init gcin core before connecting to IBus — table loading blocks the
       event loop, so it must complete before the daemon sends "create engine". */
    const char *table_dir = getenv("GCIN_TABLE_DIR");
    if (!table_dir) {
        static char local_dir[512];
        const char *home = getenv("HOME");
        snprintf(local_dir, sizeof(local_dir), "%s/.local/share/gcin",
                 home ? home : "/root");
        table_dir = local_dir;
    }
    gcin_core_init(table_dir);

    ibus_init();
    IBusBus *bus = ibus_bus_new();
    g_assert(ibus_bus_is_connected(bus));
    IBusFactory *factory = ibus_factory_new(ibus_bus_get_connection(bus));
    ibus_factory_add_engine(factory, "gcin-cangjie", GCIN_TYPE_ENGINE);
    ibus_factory_add_engine(factory, "gcin-zhuyin",  GCIN_TYPE_ENGINE);
    ibus_factory_add_engine(factory, "gcin-quick",   GCIN_TYPE_ENGINE);
    ibus_factory_add_engine(factory, "gcin-array",   GCIN_TYPE_ENGINE);
    ibus_factory_add_engine(factory, "gcin-cj5",          GCIN_TYPE_ENGINE);
    ibus_factory_add_engine(factory, "gcin-simplex-punc",  GCIN_TYPE_ENGINE);
    ibus_bus_request_name(bus, "org.freedesktop.IBus.Gcin", 0);
    ibus_main();
    return 0;
}
