#include <ibus.h>
#include "../gcin-core/gcin-core.h"

typedef struct _GcinEngine      GcinEngine;
typedef struct _GcinEngineClass GcinEngineClass;

struct _GcinEngine {
    IBusEngine       parent;
    IBusLookupTable *table;
    int              mode;         /* 0=Cangjie, 1=Zhuyin */
    gboolean         chinese_mode;
};
struct _GcinEngineClass { IBusEngineClass parent; };

G_DEFINE_TYPE(GcinEngine, gcin_engine, IBUS_TYPE_ENGINE)
#define GCIN_TYPE_ENGINE (gcin_engine_get_type())

static gboolean gcin_engine_process_key_event(IBusEngine *e,
        guint keyval, guint keycode, guint modifiers) {
    (void)keyval; (void)keycode; (void)modifiers;
    return FALSE; /* Phase 3/4: route to feedkey_gtab / feedkey_pho */
}

static void gcin_engine_enable(IBusEngine *e)    { (void)e; }
static void gcin_engine_disable(IBusEngine *e)   { (void)e; }
static void gcin_engine_reset(IBusEngine *e)     { (void)e; gcin_core_reset(); }
static void gcin_engine_focus_out(IBusEngine *e) { (void)e; gcin_core_reset(); }

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
    e->mode         = 0;
    g_object_ref_sink(e->table);
}

int main(int argc, char **argv) {
    ibus_init();
    IBusBus *bus = ibus_bus_new();
    g_assert(ibus_bus_is_connected(bus));
    IBusFactory *factory = ibus_factory_new(ibus_bus_get_connection(bus));
    ibus_factory_add_engine(factory, "gcin-cangjie", GCIN_TYPE_ENGINE);
    ibus_factory_add_engine(factory, "gcin-zhuyin",  GCIN_TYPE_ENGINE);
    ibus_bus_request_name(bus, "org.freedesktop.IBus.Gcin", 0);
    gcin_core_init("/usr/share/gcin");
    ibus_main();
    return 0;
}
