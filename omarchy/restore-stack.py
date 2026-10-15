#!/usr/bin/env python3
"""Restore the previous launcher and service overrides; leave account data intact."""
import argparse
from pathlib import Path
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--backup', type=Path, required=True)
parser.add_argument('--apply', action='store_true')
args = parser.parse_args()
home = Path.home()
backup = args.backup.resolve()
root = home/'.local/share/mail-migration-backups'
if not backup.is_relative_to(root) or not (backup/'changed-files.txt').is_file():
    parser.error('choose a backup created by activate-stack.py')
if not args.apply:
    print('Backup found. Close Evolution and pass --apply to restore the previous launcher/services.')
    raise SystemExit(0)
if subprocess.run(['pgrep', '-x', 'evolution'], stdout=subprocess.DEVNULL).returncode == 0:
    parser.error('close Evolution and wait for it to finish exiting first')
services = ['evolution-source-registry', 'evolution-calendar-factory',
            'evolution-addressbook-factory', 'evolution-user-prompter']
subprocess.run(['systemctl', '--user', 'stop', *(name+'.service' for name in services)], check=True)
for name in services:
    target = home/'.config/systemd/user'/(name+'.service.d')/'omarchy-mail.conf'
    saved = backup/(name+'.conf')
    if saved.exists():
        shutil.copy2(saved, target)
    else:
        target.unlink(missing_ok=True)
if (backup/'evolution-wrapper').exists():
    shutil.copy2(backup/'evolution-wrapper', home/'.local/bin/evolution')
standard = home/'.local/opt/evolution-omarchy'
if (backup/'created-standard-link').exists() and standard.is_symlink():
    expected = Path((backup/'runtime-prefix.txt').read_text().strip())
    if standard.resolve() == expected:
        standard.unlink()
saved_css = backup/'data/.config/evolution/omamail.css'
if saved_css.exists():
    shutil.copy2(saved_css, home/'.config/evolution/omamail.css')
subprocess.run(['systemctl', '--user', 'daemon-reload'], check=True)
subprocess.run(['systemctl', '--user', 'start', 'evolution-source-registry.service'], check=True)
print('Previous launcher and services restored. Account data and newer messages were retained.')
print('Private settings and calendar/address-book backups remain available for manual recovery.')
