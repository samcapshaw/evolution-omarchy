#!/usr/bin/env python3
"""Install the staged app without installing staged system files or accounts."""
from pathlib import Path
import os
import re
import shlex
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET

repo = Path(__file__).resolve().parent.parent
home = Path.home()
prefix = Path(os.environ.get('EVOLUTION_OMARCHY_PREFIX', home / '.local/opt/evolution-omarchy'))
if not prefix.is_absolute() or prefix == Path('/'):
    raise SystemExit('Use an absolute, dedicated application prefix.')
cache = (repo / 'build-omarchy/CMakeCache.txt').read_text()
configured = re.search(r'^CMAKE_INSTALL_PREFIX:PATH=(.*)$', cache, re.M)
if not configured or Path(configured[1]) != prefix:
    raise SystemExit('Prefix differs from the build; use the same EVOLUTION_OMARCHY_PREFIX.')
with tempfile.TemporaryDirectory(prefix='evolution-omarchy-stage-') as stage:
    subprocess.run(['cmake', '--install', str(repo / 'build-omarchy')],
                   env={**os.environ, 'DESTDIR': stage}, check=True)
    shutil.copytree(Path(stage) / prefix.relative_to('/'), prefix, dirs_exist_ok=True)

for name in ['module-ews-configuration.so', 'module-microsoft365-configuration.so']:
    source = Path('/usr/lib/evolution/modules') / name
    target = prefix / 'lib/evolution/modules' / name
    if source.exists() and not target.exists():
        target.symlink_to(source)

wrapper = home / '.local/bin/evolution'
wrapper.parent.mkdir(parents=True, exist_ok=True)
wrapper.write_text('#!/bin/sh\nexec ' + shlex.quote(str(prefix / 'bin/evolution')) + ' "$@"\n')
wrapper.chmod(0o755)
apps = home / '.local/share/applications'
apps.mkdir(parents=True, exist_ok=True)
desktop = (prefix / 'share/applications/org.gnome.Evolution.desktop').read_text()
desktop = re.sub(r'^Exec=evolution(?=\s|$)', 'Exec="' + str(wrapper).replace('"', '\\"') + '"', desktop, flags=re.M)
desktop = re.sub(r'^Icon=.*$', 'Icon=sam-mail', desktop, flags=re.M)
(apps / 'org.gnome.Evolution.desktop').write_text(desktop)
for alternate in apps.glob('userapp-Evolution-*.desktop'):
    text = alternate.read_text()
    text = re.sub(r'^Exec=(?:/usr/bin/)?evolution(?=\s|$)', 'Exec="' + str(wrapper) + '"', text, flags=re.M)
    alternate.write_text(text)
icons = home / '.local/share/icons/hicolor/scalable/apps'
icons.mkdir(parents=True, exist_ok=True)
shutil.copy2(repo / 'omarchy/sam-mail.svg', icons / 'sam-mail.svg')
actions = home / '.local/share/icons/hicolor/scalable/actions'
actions.mkdir(parents=True, exist_ok=True)
for name in ['sidebar-show.svg', 'sidebar-show-symbolic.svg']:
    shutil.copy2(repo / 'omarchy/sidebar-show.svg', actions / name)
config = home / '.config/evolution'
config.mkdir(parents=True, exist_ok=True)
shutil.copy2(repo / 'omarchy/omamail.css', config / 'omamail.css')
rules = config / 'mail/vfolders.xml'
rules.parent.mkdir(parents=True, exist_ok=True)
if rules.exists():
    tree = ET.parse(rules)
    root = tree.getroot()
else:
    root = ET.Element('filteroptions')
    tree = ET.ElementTree(root)
ruleset = root.find('ruleset')
if ruleset is None:
    ruleset = ET.SubElement(root, 'ruleset')
changed = False
for title in ['All Inboxes', 'All Sent']:
    if any(rule.findtext('title') == title for rule in ruleset):
        continue
    if rules.exists():
        backup = rules.with_suffix('.xml.before-omarchy')
        if not backup.exists():
            shutil.copy2(rules, backup)
    rule = ET.SubElement(ruleset, 'rule', grouping='all', source='incoming', enabled='true')
    ET.SubElement(rule, 'title').text = title
    ET.SubElement(ET.SubElement(rule, 'partset'), 'part', name='all')
    ET.SubElement(rule, 'sources', {'with': 'specific', 'autoupdate': 'true'})
    changed = True
if changed:
    tree.write(rules, encoding='utf-8', xml_declaration=True)
if shutil.which('update-desktop-database'):
    subprocess.run(['update-desktop-database', str(apps)], check=True)
print('Installed. Launch ~/.local/bin/evolution or the Evolution desktop entry.')
