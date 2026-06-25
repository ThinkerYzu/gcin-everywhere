#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <ibus.h>
#include <glib/gstdio.h>
#include "../gcin-core/gcin-core.h"

/* Voice input (台語語音): mode value past the table/phonetic methods (0..5).
   The recognizer lives in the out-of-process gcin-voiced daemon; this engine is
   a thin socket client. See research/VOICE-INPUT-DESIGN.md. */
#define MODE_VOICE 6

/* Voice sub-state, mirrors the daemon's state machine plus a PENDING state for a
   returned transcript sitting in the preedit awaiting commit. */
enum { VOICE_IDLE = 0, VOICE_RECORDING, VOICE_THINKING, VOICE_PENDING };

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
    int              mode;         /* 0=Cangjie, 1=Zhuyin, 2=Quick, 3=Array, 4=CJ5, 5=SimplexPunc, 6=Voice */
    gboolean         chinese_mode;
    gboolean         allow_switch; /* TRUE only for the unified gcin-everywhere engine */
    IBusProperty    *prop;         /* panel property showing the active method (everywhere only) */
    IBusPropList    *props;        /* prop list registered with IBus (everywhere only) */

    /* Voice mode (gcin-voiced socket client). All async; never blocks the key loop. */
    int          voiced_fd;        /* Unix socket to gcin-voiced, -1 if unconnected */
    GIOChannel  *voiced_chan;      /* GLib wrapper around voiced_fd */
    guint        voiced_watch;     /* g_io_add_watch source id (0 if none) */
    int          voice_status;     /* VOICE_IDLE / RECORDING / THINKING / PENDING */
    char         voice_text[1024]; /* transcript held in the preedit awaiting commit */
    char         voiced_buf[4096]; /* accumulates partial event lines from the socket */
    int          voiced_buflen;
};
struct _GcinEngineClass { IBusEngineClass parent; };

G_DEFINE_TYPE(GcinEngine, gcin_engine, IBUS_TYPE_ENGINE)
#define GCIN_TYPE_ENGINE (gcin_engine_get_type())

/* ── Method/mode helpers ─────────────────────────────────────────── */

/* Panel symbol (single glyph) for each mode. */
static const char *mode_symbol(int mode) {
    switch (mode) {
        case 1:  return "注";   /* Zhuyin */
        case 2:  return "速";   /* Quick */
        case 3:  return "列";   /* Array */
        case 4:  return "五";   /* CJ5 */
        case 5:  return "標";   /* SimplexPunc */
        case 6:  return "語";   /* Voice */
        default: return "倉";   /* Cangjie */
    }
}

/* Readable label for each mode — shown by the GNOME panel indicator. */
static const char *mode_label(int mode) {
    switch (mode) {
        case 1:  return "注音 Zhuyin";
        case 2:  return "速成 Quick";
        case 3:  return "行列 Array";
        case 4:  return "倉頡五代 CJ5";
        case 5:  return "標點簡易 SimplexPunc";
        case 6:  return "台語語音 Voice";
        default: return "倉頡 Cangjie";
    }
}

/* Map a Ctrl+Alt+<digit> keyval to a mode; -1 if the digit is unassigned.
   Digits follow gcin's gtab.list key_ch column (1/2/3/8); 4/5 extend it for
   Quick and SimplexPunc, which gcin leaves on '-'. */
static int digit_to_mode(guint keyval) {
    switch (keyval) {
        case '1': return 0;   /* 倉頡 Cangjie */
        case '2': return 4;   /* 倉五 CJ5 */
        case '3': return 1;   /* 注音 Zhuyin */
        case '4': return 2;   /* 速成 Quick */
        case '5': return 5;   /* 標點簡易 SimplexPunc */
        case '8': return 3;   /* 行列 Array */
        case '0': return MODE_VOICE;  /* 台語語音 Voice (Breeze-ASR-26) */
        default:  return -1;
    }
}

/* Panel glyph for the live state: 英 in English; in voice mode the dynamic
   recording/thinking glyph; otherwise the method symbol. */
static const char *active_symbol(GcinEngine *e) {
    if (!e->chinese_mode) return "英";
    if (e->mode == MODE_VOICE) {
        switch (e->voice_status) {
            case VOICE_RECORDING: return "🎤";
            case VOICE_THINKING:  return "…";
            default:              return "語";
        }
    }
    return mode_symbol(e->mode);
}

static const char *active_label(GcinEngine *e) {
    if (!e->chinese_mode) return "英文 English";
    return mode_label(e->mode);
}

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

/* ── Panel property (unified engine only) ────────────────────────── */

/* Lazily create the IBusProperty + list. We keep our own ref on both so the
   pointer stays valid for later ibus_engine_update_property() calls. */
static void ensure_property(GcinEngine *e) {
    if (e->prop) return;
    e->prop = ibus_property_new(
        "gcin-method", PROP_TYPE_NORMAL,
        ibus_text_new_from_string(mode_symbol(e->mode)),  /* label */
        NULL,                                             /* icon */
        ibus_text_new_from_string("gcin-everywhere — Ctrl+Alt+digit to switch method"),
        TRUE, TRUE, PROP_STATE_UNCHECKED, NULL);
    g_object_ref_sink(e->prop);
    e->props = ibus_prop_list_new();
    g_object_ref_sink(e->props);
    ibus_prop_list_append(e->props, e->prop);  /* takes its own ref on prop */
    ibus_property_set_symbol(e->prop, ibus_text_new_from_string(mode_symbol(e->mode)));
}

/* Publish the current method to a state file the GNOME Shell extension watches.
   GNOME ignores IBus property symbol updates, so the panel never reflects the
   live method; the bundled extension reads this file instead and mirrors it in
   the top bar. Path: $XDG_RUNTIME_DIR/gcin-everywhere/state. Contents while the
   engine is active: "<glyph>\t<label>"; empty when disabled, so the extension
   hides its indicator (it only shows for the unified switcher). Harmless on
   desktops with no extension — the IBus property above still drives their panels. */
static void write_state(GcinEngine *e, gboolean active) {
    if (!e->allow_switch) return;
    char *dir  = g_build_filename(g_get_user_runtime_dir(), "gcin-everywhere", NULL);
    g_mkdir_with_parents(dir, 0700);
    char *path = g_build_filename(dir, "state", NULL);
    char *content;
    if (active) {
        content = g_strdup_printf("%s\t%s", active_symbol(e), active_label(e));
    } else {
        content = g_strdup("");
    }
    g_file_set_contents(path, content, -1, NULL);
    g_free(content);
    g_free(path);
    g_free(dir);
}

/* Push the current state to the panel: the active method's glyph in Chinese
   mode, or 英 when toggled to English passthrough (Ctrl+Space). */
static void update_property(GcinEngine *e) {
    if (!e->allow_switch || !e->prop) return;
    const char *sym = active_symbol(e);
    ibus_property_set_symbol(e->prop, ibus_text_new_from_string(sym));
    ibus_property_set_label(e->prop, ibus_text_new_from_string(sym));
    ibus_engine_update_property((IBusEngine *)e, e->prop);
    write_state(e, TRUE);  /* mirror to the GNOME extension's state file */
}

/* ── Voice mode: gcin-voiced socket client ───────────────────────────
   The recognizer runs out-of-process (gcin-voiced). The engine sends tiny
   control commands and receives transcript events asynchronously via a GLib
   GSource on the socket fd, so process_key_event NEVER blocks on inference. */

/* Extract a JSON string value for "key" from a flat one-line object. Handles the
   escapes the daemon can emit (\" \\ \/ \n \t \r). The daemon dumps with
   ensure_ascii=False, so UTF-8 bytes appear literally (no \uXXXX to decode). */
static int json_get_str(const char *line, const char *key, char *out, int outlen) {
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(line, pat);
    if (!p) return 0;
    p += strlen(pat);
    while (*p == ' ' || *p == '\t' || *p == ':') p++;
    if (*p != '"') return 0;
    p++;
    int i = 0;
    while (*p && *p != '"' && i < outlen - 1) {
        if (*p == '\\') {
            p++;
            switch (*p) {
                case 'n': out[i++] = '\n'; break;
                case 't': out[i++] = '\t'; break;
                case 'r': out[i++] = '\r'; break;
                case '"': out[i++] = '"';  break;
                case '\\': out[i++] = '\\'; break;
                case '/': out[i++] = '/';  break;
                default:  if (*p) out[i++] = *p; break;
            }
            if (*p) p++;
        } else {
            out[i++] = *p++;
        }
    }
    out[i] = '\0';
    return 1;
}

/* Show the pending transcript in the preedit (underlined), or hide if empty. */
static void voice_update_preedit(GcinEngine *e) {
    IBusEngine *ie = (IBusEngine *)e;
    if (e->voice_text[0]) {
        IBusText *pre = ibus_text_new_from_string(e->voice_text);
        ibus_text_append_attribute(pre, IBUS_ATTR_TYPE_UNDERLINE,
                                   IBUS_ATTR_UNDERLINE_SINGLE, 0, -1);
        ibus_engine_update_preedit_text(ie, pre,
                                        g_utf8_strlen(e->voice_text, -1), TRUE);
    } else {
        ibus_engine_hide_preedit_text(ie);
    }
}

/* Apply one daemon event line to the engine's voice state + panel. */
static void voiced_handle_line(GcinEngine *e, const char *line) {
    char ev[32];
    if (!json_get_str(line, "event", ev, sizeof(ev))) return;
    if (!strcmp(ev, "recording")) {
        e->voice_status = VOICE_RECORDING;
    } else if (!strcmp(ev, "thinking")) {
        e->voice_status = VOICE_THINKING;
    } else if (!strcmp(ev, "transcript")) {
        char text[1024] = "";
        json_get_str(line, "text", text, sizeof(text));
        if (text[0]) {
            g_strlcpy(e->voice_text, text, sizeof(e->voice_text));
            e->voice_status = VOICE_PENDING;
        } else {
            e->voice_text[0] = '\0';      /* empty/too-short utterance */
            e->voice_status = VOICE_IDLE;
        }
        voice_update_preedit(e);
    } else if (!strcmp(ev, "error")) {
        e->voice_text[0] = '\0';
        e->voice_status = VOICE_IDLE;
        voice_update_preedit(e);
    } /* "ready" needs no UI change */
    update_property(e);
}

static void voiced_disconnect(GcinEngine *e) {
    if (e->voiced_watch) { g_source_remove(e->voiced_watch); e->voiced_watch = 0; }
    if (e->voiced_chan) {
        g_io_channel_shutdown(e->voiced_chan, FALSE, NULL);
        g_io_channel_unref(e->voiced_chan);
        e->voiced_chan = NULL;
    }
    if (e->voiced_fd >= 0) { close(e->voiced_fd); e->voiced_fd = -1; }
    e->voiced_buflen = 0;
}

/* GSource callback: drain the socket, split into lines, dispatch each event. */
static gboolean voiced_event_cb(GIOChannel *src, GIOCondition cond, gpointer data) {
    (void)src;
    GcinEngine *e = (GcinEngine *)data;
    if (cond & (G_IO_HUP | G_IO_ERR)) { voiced_disconnect(e); return FALSE; }
    char tmp[2048];
    ssize_t n = read(e->voiced_fd, tmp, sizeof(tmp));
    if (n <= 0) { voiced_disconnect(e); return FALSE; }
    for (ssize_t i = 0; i < n; i++) {
        if (tmp[i] == '\n') {
            e->voiced_buf[e->voiced_buflen] = '\0';
            voiced_handle_line(e, e->voiced_buf);
            e->voiced_buflen = 0;
        } else if (e->voiced_buflen < (int)sizeof(e->voiced_buf) - 1) {
            e->voiced_buf[e->voiced_buflen++] = tmp[i];
        } else {
            e->voiced_buflen = 0;         /* overlong line: drop, resync on \n */
        }
    }
    return TRUE;
}

/* Connect to $XDG_RUNTIME_DIR/gcin-everywhere/voiced.sock and watch it. */
static int voiced_connect(GcinEngine *e) {
    if (e->voiced_fd >= 0) return 0;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    int len = snprintf(addr.sun_path, sizeof(addr.sun_path),
                       "%s/gcin-everywhere/voiced.sock", g_get_user_runtime_dir());
    if (len < 0 || len >= (int)sizeof(addr.sun_path)) return -1;  /* path too long */
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;                        /* daemon not running */
    }
    e->voiced_fd   = fd;
    e->voiced_chan = g_io_channel_unix_new(fd);
    g_io_channel_set_encoding(e->voiced_chan, NULL, NULL);  /* binary */
    e->voiced_watch = g_io_add_watch(e->voiced_chan,
                                     G_IO_IN | G_IO_HUP | G_IO_ERR,
                                     voiced_event_cb, e);
    e->voiced_buflen = 0;
    return 0;
}

/* Fire-and-forget a one-line command, connecting lazily. */
static void voiced_send(GcinEngine *e, const char *cmd) {
    if (e->voiced_fd < 0 && voiced_connect(e) < 0) return;
    if (write(e->voiced_fd, cmd, strlen(cmd)) < 0) voiced_disconnect(e);
}

/* Voice-mode key handling. Returns TRUE if the key was consumed. PTT key is
   Space (reaches the engine reliably — unlike a global chord the desktop could
   grab); Enter commits the pending transcript, Esc/Backspace discards. */
static gboolean handle_voice_key(GcinEngine *e, guint keyval) {
    switch (e->voice_status) {
        case VOICE_RECORDING:
            if (keyval == IBUS_space) { voiced_send(e, "{\"cmd\":\"stop\"}\n"); return TRUE; }
            if (keyval == IBUS_Escape) {
                voiced_send(e, "{\"cmd\":\"cancel\"}\n");
                e->voice_status = VOICE_IDLE;
                update_property(e);
                return TRUE;
            }
            return TRUE;                  /* swallow keys while the mic is live */
        case VOICE_THINKING:
            return TRUE;                  /* swallow keys while transcribing */
        case VOICE_PENDING:
            if (keyval == IBUS_Return || keyval == IBUS_KP_Enter) {
                on_commit(e->voice_text, (IBusEngine *)e);
                e->voice_text[0] = '\0';
                e->voice_status = VOICE_IDLE;
                voice_update_preedit(e);
                update_property(e);
                return TRUE;
            }
            if (keyval == IBUS_Escape || keyval == IBUS_BackSpace) {
                e->voice_text[0] = '\0';
                e->voice_status = VOICE_IDLE;
                voice_update_preedit(e);
                update_property(e);
                return TRUE;
            }
            if (keyval == IBUS_space) {   /* re-record: discard + start over */
                e->voice_text[0] = '\0';
                voice_update_preedit(e);
                voiced_send(e, "{\"cmd\":\"start\"}\n");
                return TRUE;
            }
            return TRUE;                  /* protect the pending preedit */
        default:                          /* VOICE_IDLE */
            if (keyval == IBUS_space) { voiced_send(e, "{\"cmd\":\"start\"}\n"); return TRUE; }
            return FALSE;                 /* idle: let other keys reach the app */
    }
}

/* ── Key event ───────────────────────────────────────────────────── */

static gboolean gcin_engine_process_key_event(IBusEngine *iengine,
        guint keyval, guint keycode, guint modifiers) {
    (void)keycode;
    if (modifiers & IBUS_RELEASE_MASK) return FALSE;
    GcinEngine *e = (GcinEngine *)iengine;

    /* Ctrl+Space (no Shift/Alt): in the unified engine, toggle Chinese <-> English
       passthrough in place — the gcin-native English toggle (gcin_im_toggle). The
       previously selected method (e->mode) is preserved, so toggling back resumes
       it. Handled BEFORE the chinese_mode early-return so it can also turn Chinese
       back on. The six single-method engines don't toggle — they pass Ctrl+Space
       through to the desktop (so GNOME source-switching still works there). */
    if (keyval == IBUS_space &&
        (modifiers & (IBUS_CONTROL_MASK | IBUS_MOD1_MASK | IBUS_SHIFT_MASK))
            == IBUS_CONTROL_MASK) {
        if (!e->allow_switch) return FALSE;
        e->chinese_mode = !e->chinese_mode;
        gcin_core_reset();                /* discard any pending composition */
        ibus_engine_hide_preedit_text(iengine);
        ibus_engine_hide_lookup_table(iengine);
        update_property(e);
        return TRUE;
    }

    /* Ctrl+Alt+<digit>: switch active input method in place — mirrors gcin
       eve.cpp:1240. Active only in the unified gcin-everywhere engine. Must run
       before the chinese_mode early-return and the Ctrl/Alt phrase intercepts.
       Selecting a method also re-enables Chinese if we were in English. An
       unassigned digit (or any other key under Ctrl+Alt) falls through. */
    if (e->allow_switch &&
        (modifiers & (IBUS_CONTROL_MASK | IBUS_MOD1_MASK))
            == (IBUS_CONTROL_MASK | IBUS_MOD1_MASK)) {
        int new_mode = digit_to_mode(keyval);
        if (new_mode < 0) return FALSE;
        if (new_mode != e->mode || !e->chinese_mode) {
            /* Leaving voice mode: abort any live recording and drop the preedit. */
            if (e->mode == MODE_VOICE && new_mode != MODE_VOICE) {
                if (e->voice_status == VOICE_RECORDING)
                    voiced_send(e, "{\"cmd\":\"cancel\"}\n");
                e->voice_text[0] = '\0';
                e->voice_status = VOICE_IDLE;
            }
            gcin_core_reset();            /* discard any pending composition */
            ibus_engine_hide_preedit_text(iengine);
            ibus_engine_hide_lookup_table(iengine);
            e->mode = new_mode;
            e->chinese_mode = TRUE;
            /* Entering voice mode: connect the daemon and warm the model. */
            if (new_mode == MODE_VOICE) {
                e->voice_status = VOICE_IDLE;
                e->voice_text[0] = '\0';
                voiced_connect(e);
                voiced_send(e, "{\"cmd\":\"ping\"}\n");
            }
            update_property(e);
        }
        return TRUE;
    }

    /* English passthrough: let every key reach the application untouched. */
    if (!e->chinese_mode) return FALSE;

    /* Voice mode: keys drive the gcin-voiced socket client (PTT / commit /
       discard), never the table/phonetic core. Transcripts arrive async on the
       socket GSource. Handled before full-width/phrase intercepts. */
    if (e->mode == MODE_VOICE)
        return handle_voice_key(e, keyval);

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
    /* The unified switcher engine: starts in Cangjie, switches via Ctrl+Alt+digit. */
    if (name && g_str_has_suffix(name, "everywhere")) {
        ge->allow_switch = TRUE;
        ge->mode = 0;
        ensure_property(ge);
        ibus_engine_register_properties(e, ge->props);
        update_property(ge);
    }
    /* Single-method engines: mode fixed from the name suffix, no runtime switching. */
    else if (name && g_str_has_suffix(name, "zhuyin"))      ge->mode = 1;
    else if (name && g_str_has_suffix(name, "quick"))  ge->mode = 2;
    else if (name && g_str_has_suffix(name, "array"))  ge->mode = 3;
    else if (name && g_str_has_suffix(name, "cj5"))           ge->mode = 4;
    else if (name && g_str_has_suffix(name, "simplex-punc"))  ge->mode = 5;
    else                                                       ge->mode = 0;
    gcin_core_reset();
}
static void gcin_engine_disable(IBusEngine *e) {
    GcinEngine *ge = (GcinEngine *)e;
    /* Switching away from the unified engine — clear the indicator state. */
    if (ge->allow_switch) write_state(ge, FALSE);
    /* Drop the voice socket; the daemon cancels any recording on disconnect. */
    voiced_disconnect(ge);
    ge->voice_status = VOICE_IDLE;
    ge->voice_text[0] = '\0';
}

/* IBus clears panel properties on focus change; re-register ours on focus-in.
   The unified engine also resets to English on every focus-in: each newly-focused
   text field (a different window, a different field, or re-entering one) starts in
   English passthrough. IBus exposes focus, not window identity, so this fires on any
   focus gain — the classic per-context IME behavior. The selected method (ge->mode)
   is preserved, so Ctrl+Space / Ctrl+Alt+digit resumes it; update_property() then
   shows 英 in the panel and state file. */
static void gcin_engine_focus_in(IBusEngine *e) {
    GcinEngine *ge = (GcinEngine *)e;
    if (ge->allow_switch) {
        /* Reset to English: abort a live recording and drop any pending transcript. */
        if (ge->mode == MODE_VOICE) {
            if (ge->voice_status == VOICE_RECORDING)
                voiced_send(ge, "{\"cmd\":\"cancel\"}\n");
            ge->voice_status = VOICE_IDLE;
            ge->voice_text[0] = '\0';
        }
        ge->chinese_mode = FALSE;
        gcin_core_reset();
        ibus_engine_hide_preedit_text(e);
        ibus_engine_hide_lookup_table(e);
        if (ge->props) {
            ibus_engine_register_properties(e, ge->props);
            update_property(ge);
        }
    }
}

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
    ec->focus_in           = gcin_engine_focus_in;
    ec->focus_out          = gcin_engine_focus_out;
}

static void gcin_engine_init(GcinEngine *e) {
    e->table        = ibus_lookup_table_new(10, 0, TRUE, TRUE);
    e->chinese_mode = TRUE;
    e->mode         = 0;  /* set correctly in gcin_engine_enable() */
    e->allow_switch = FALSE;
    e->prop         = NULL;
    e->props        = NULL;
    e->voiced_fd    = -1;
    e->voiced_chan  = NULL;
    e->voiced_watch = 0;
    e->voice_status = VOICE_IDLE;
    e->voice_text[0] = '\0';
    e->voiced_buflen = 0;
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
    ibus_factory_add_engine(factory, "gcin-everywhere",    GCIN_TYPE_ENGINE);
    ibus_bus_request_name(bus, "org.freedesktop.IBus.Gcin", 0);
    ibus_main();
    return 0;
}
