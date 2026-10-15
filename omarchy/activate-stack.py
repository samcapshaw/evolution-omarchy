#!/usr/bin/env python3
"""Explicitly activate a built user-local stack; close Evolution first."""
import argparse
from datetime import datetime, timezone
import os
from pathlib import Path
import shlex
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--prefix', type=Path, required=True)
parser.add_argument('--apply', action='store_true', help='back up configuration and activate the stack')
args = parser.parse_args()
prefix = args.prefix.resolve()
home = Path.home()
if not prefix.is_relative_to(home) or prefix == home:
    parser.error('use a dedicated prefix inside your home directory')
services = ['evolution-source-registry', 'evolution-calendar-factory',
            'evolution-addressbook-factory', 'evolution-user-prompter']
for binary in [prefix/'bin/evolution', *(prefix/'libexec'/name for name in services)]:
    if not binary.is_file() or not os.access(binary, os.X_OK):
        parser.error('the app and matching Data Server stack must be built first')
schemas = prefix/'share/glib-2.0/schemas'
if not (schemas/'gschemas.compiled').is_file():
    parser.error('compiled schemas are missing')
if not args.apply:
    print('Stack present. Close Evolution and pass --apply to back up and activate it.')
    raise SystemExit(0)
if subprocess.run(['pgrep', '-x', 'evolution'], stdout=subprocess.DEVNULL).returncode == 0:
    parser.error('close Evolution and wait for it to finish exiting before activation')
stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
backup = home/'.local/share/mail-migration-backups'/('stack-'+stamp)
backup.mkdir(parents=True, mode=0o700)
for folder in ['.config/evolution', '.local/share/evolution/calendar',
               '.local/share/evolution/addressbook', '.cache/evolution/calendar',
               '.cache/evolution/addressbook']:
    source = home/folder
    if source.exists():
        shutil.copytree(source, backup/'data'/folder, dirs_exist_ok=True)
settings = backup/'settings.dconf'
settings.touch(mode=0o600)
settings.write_bytes(subprocess.check_output(['dconf', 'dump', '/org/gnome/evolution/']))
(backup/'runtime-prefix.txt').write_text(str(prefix)+'\n')
standard = home/'.local/opt/evolution-omarchy'
if not standard.exists() and not standard.is_symlink():
    standard.parent.mkdir(parents=True, exist_ok=True)
    standard.symlink_to(prefix)
    (backup/'created-standard-link').touch()
library_path = str(prefix/'lib')+':'+str(prefix/'lib/evolution-data-server')
wrapper = prefix/'bin/omarchy-mail'
wrapper.write_text('#!/bin/sh\n# Evolution for Omarchy managed launcher\n'
                   'export LD_LIBRARY_PATH='+shlex.quote(library_path)+'\n'
                   'export GSETTINGS_SCHEMA_DIR='+shlex.quote(str(schemas))+'\n'
                   'exec '+shlex.quote(str(prefix/'bin/evolution'))+' "$@"\n')
wrapper.chmod(0o755)
changed = []
for name in services:
    target = home/'.config/systemd/user'/(name+'.service.d')/'omarchy-mail.conf'
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists():
        shutil.copy2(target, backup/(name+'.conf'))
    # systemd arguments use double quotes and escape backslashes/quotes/percent specifiers.
    def quote(value):
        return '"'+str(value).replace('\\', '\\\\').replace('"', '\\"').replace('%', '%%')+'"'
    target.write_text('[Service]\nExecStart=\nExecStart='+quote(prefix/'libexec'/name)+'\n'
                      'Environment='+quote('LD_LIBRARY_PATH='+library_path)+'\n'
                      'Environment='+quote('GSETTINGS_SCHEMA_DIR='+str(schemas))+'\n')
    changed.append(str(target))
launcher = home/'.local/bin/evolution'
launcher.parent.mkdir(parents=True, exist_ok=True)
if launcher.exists() or launcher.is_symlink():
    shutil.copy2(launcher, backup/'evolution-wrapper', follow_symlinks=True)
launcher.write_text('#!/bin/sh\nexec '+shlex.quote(str(wrapper))+' "$@"\n')
launcher.chmod(0o755)
shutil.copy2(Path(__file__).parent/'omamail.css', home/'.config/evolution/omamail.css')
(backup/'changed-files.txt').write_text('\n'.join(changed)+'\n')
subprocess.run(['systemctl', '--user', 'daemon-reload'], check=True)
subprocess.run(['systemctl', '--user', 'stop', *(name+'.service' for name in services)], check=True)
subprocess.run(['systemctl', '--user', 'start', 'evolution-source-registry.service'], check=True)
print('Activated the local stack. Private recovery backup: '+str(backup))
print('Use ~/.local/bin/evolution. Mail, calendars, credentials and folder rules were retained.')
