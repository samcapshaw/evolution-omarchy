#!/usr/bin/env python3
"""Apply appearance defaults once; never rewrite accounts or calendar selections."""
from pathlib import Path
import subprocess
from gi.repository import Gio

backup = Path.home() / '.config/evolution/omamail-backup/settings-before.dconf'
backup.parent.mkdir(parents=True, exist_ok=True)
if not backup.exists():
    backup.write_bytes(subprocess.check_output(['dconf', 'dump', '/org/gnome/evolution/']))
    backup.chmod(0o600)
defaults = {
    'org.gnome.evolution.shell': {
        'use-header-bar': True, 'icon-only-buttons-in-header-bar': True,
        'prefer-symbolic-icons': 'yes', 'toolbar-icon-size': 'small',
        'menubar-visible': False, 'toolbar-visible': False,
        'sidebar-visible': True, 'statusbar-visible': False,
        'buttons-visible': True, 'buttons-style': 'both',
        'buttons-hide': ['tasks', 'memos'], 'folder-bar-width': 230,
    },
    'org.gnome.evolution.mail': {
        'layout': 1, 'hpaned-size': 1420000, 'global-view-setting': False,
        'show-subject-above-sender': False, 'show-email': False,
        'show-preview-toolbar': False, 'show-to-do-bar': False,
        'show-sender-photo': False, 'headers-collapsed': True,
    },
    'org.gnome.evolution.calendar': {
        'show-tag-vpane': False, 'compress-weekend': False,
        'today-background-color': '#242e49',
    },
}
for schema, values in defaults.items():
    settings = Gio.Settings.new(schema)
    for key, value in values.items():
        signature = settings.get_value(key).get_type_string()
        from gi.repository import GLib
        settings.set_value(key, GLib.Variant(signature, value))
Gio.Settings.sync()
print('Applied appearance defaults. Existing calendar selections were preserved.')
