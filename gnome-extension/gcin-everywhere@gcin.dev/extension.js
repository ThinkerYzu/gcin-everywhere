// gcin Everywhere Indicator — GNOME Shell extension
//
// GNOME Shell's top-bar input indicator only renders an engine's static
// `symbol` from its IBus component XML; it ignores live IBusProperty symbol
// updates. Since gcin-everywhere is a single engine that switches method
// internally, GNOME would only ever show its fixed glyph (全) and the user
// can't tell which method is active.
//
// The ibus-engine-gcin engine works around this by publishing the live method
// to a small state file ($XDG_RUNTIME_DIR/gcin-everywhere/state). This
// extension watches that file and mirrors the glyph in the top panel. The
// indicator is shown only while the file is non-empty — i.e. only while the
// gcin Everywhere input source is the active one.

import St from 'gi://St';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Clutter from 'gi://Clutter';

import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import * as PanelMenu from 'resource:///org/gnome/shell/ui/panelMenu.js';
import * as PopupMenu from 'resource:///org/gnome/shell/ui/popupMenu.js';

const STATE_DIR = GLib.build_filenamev([GLib.get_user_runtime_dir(), 'gcin-everywhere']);
const STATE_FILE = GLib.build_filenamev([STATE_DIR, 'state']);

export default class GcinEverywhereExtension extends Extension {
    enable() {
        // Top-panel button holding the method glyph. Created hidden; shown only
        // when the state file reports an active method.
        this._button = new PanelMenu.Button(0.0, 'gcin Everywhere', false);
        this._label = new St.Label({
            text: '',
            y_align: Clutter.ActorAlign.CENTER,
            style_class: 'gcin-everywhere-label',
        });
        this._button.add_child(this._label);

        // A non-interactive menu line showing the full method name on click.
        this._menuItem = new PopupMenu.PopupMenuItem('', {reactive: false});
        this._button.menu.addMenuItem(this._menuItem);

        Main.panel.addToStatusArea('gcin-everywhere', this._button, 0, 'right');
        this._button.visible = false;

        // Watch the directory (robust against atomic file replacement) and
        // refresh on any change. inotify-driven — no polling.
        const dir = Gio.File.new_for_path(STATE_DIR);
        try {
            dir.make_directory_with_parents(null);
        } catch (e) {
            // Already exists (or runtime dir unavailable) — ignore.
        }
        this._monitor = dir.monitor_directory(Gio.FileMonitorFlags.NONE, null);
        this._monitorId = this._monitor.connect('changed', () => this._refresh());

        this._refresh();
    }

    _refresh() {
        let glyph = '';
        let label = '';
        try {
            const [ok, bytes] = GLib.file_get_contents(STATE_FILE);
            if (ok && bytes && bytes.length) {
                const text = new TextDecoder().decode(bytes).trim();
                if (text.length) {
                    const parts = text.split('\t');
                    glyph = parts[0] || '';
                    label = parts[1] || glyph;
                }
            }
        } catch (e) {
            // No file yet / unreadable — treat as inactive.
        }

        if (glyph) {
            this._label.text = glyph;
            this._menuItem.label.text = label;
            this._button.visible = true;
        } else {
            this._button.visible = false;
        }
    }

    disable() {
        if (this._monitor) {
            if (this._monitorId)
                this._monitor.disconnect(this._monitorId);
            this._monitor.cancel();
            this._monitor = null;
            this._monitorId = null;
        }
        if (this._button) {
            this._button.destroy();
            this._button = null;
        }
        this._label = null;
        this._menuItem = null;
    }
}
