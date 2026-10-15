#!/usr/bin/env python3
"""Inspect mail polling; apply changes only when explicitly requested."""
import argparse
from pathlib import Path
from datetime import datetime, timezone
import gi

gi.require_version('EDataServer', '1.2')
from gi.repository import EDataServer

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--apply-minutes', type=int, metavar='N', help='explicitly set enabled mail accounts to check every N minutes (1–60)')
args = parser.parse_args()
if args.apply_minutes is not None and not 1 <= args.apply_minutes <= 60:
    parser.error('interval must be between 1 and 60 minutes')
registry = EDataServer.SourceRegistry.new_sync(None)
accounts = [s for s in registry.list_sources('Mail Account')
            if registry.check_enabled(s)
            and s.get_extension('Mail Account').get_backend_name() not in ('maildir', 'mbox', 'vfolder', 'none', 'rss')]
backup = None
for index, source in enumerate(accounts, 1):
    refresh = source.get_extension('Refresh')
    if args.apply_minutes is not None:
        if not source.get_writable():
            print(f'Account {index}: read-only; skipped')
            continue
        if backup is None:
            stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
            backup = Path.home()/'.local/share/evolution-omarchy/backups'/('refresh-'+stamp)
            backup.mkdir(parents=True, mode=0o700)
        # Back up the complete live source locally; never place account data in the repository.
        data, _ = source.to_string()
        path = backup / f'account-{index}.source'
        path.touch(mode=0o600)
        path.write_text(data)
        refresh.set_enabled(True)
        refresh.set_interval_minutes(args.apply_minutes)
        source.write_sync(None)
    print(f'Account {index}: polling {"on" if refresh.get_enabled() else "off"}, {refresh.get_interval_minutes()} minute(s)')
if not accounts:
    print('No enabled remote mail accounts found.')
if backup:
    print('Previous account settings backed up privately under ~/.local/share/evolution-omarchy/backups/.')
