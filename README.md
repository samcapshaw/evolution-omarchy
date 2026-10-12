# Evolution for Omarchy

An unofficial Evolution 3.60.2 fork with an Apple Mail inspired layout and a Tokyo Night palette. Upstream: [GNOME Evolution](https://gitlab.gnome.org/GNOME/evolution), mirrored at [GNOME/evolution](https://github.com/GNOME/evolution).

## Changes

- Unified **All Inboxes** under the first **Mailboxes** section. Account inboxes are discovered automatically; their subfolders are excluded.
- Local folders move to **Local Storage** at the bottom, with a clearly named **Local Inbox**.
- Side-by-side reading pane with padded message rows, fine separators, emphasized sender/subject, and up to two lines of body preview when supplied by the mail backend.
- Compact subject/sender/date reader header and plain compose/reply/forward/archive/delete controls that remain visible in fullscreen.
- Search/filter controls stay hidden until Search or Ctrl+F; restored active searches remain visible, with extra room around the controls and list.
- Delete and Backspace remove the selected message. Backspace no longer runs Evolution's smart backward navigation.
- Deletion holds the viewport through asynchronous folder updates and delayed cursor scrolling. Clicking, scrolling, or navigating in the list resumes normal movement.
- Sidebar button or Ctrl+Shift+B folds folders into a Mail/Contacts/Calendar icon rail.
- All Sent combines discovered sent folders alongside All Inboxes.
- **Top** header button and **Alt+Home** scroll straight to the top without changing the selected message. Default wide views sort newest first.
- Month is the calendar default, with Week/Month header controls, clearer grid lines, a soft blue today highlight and full weekend columns.
- Calendar checkbox selections use Evolution's native persistent settings. The setup script never changes selected calendars.
- App-specific stylesheet and matching envelope icon, with a dedicated `org.omarchy.Mail` desktop identity so dock pins launch the custom build; desktop-wide themes are untouched.
- User-local installation alongside the packaged Evolution. The RSS module is omitted because it otherwise installs a provider globally.

Unread messages have a blue dot and stronger text. Status markers are painted in a separate margin while the native text and tree cell structure stays intact; sender, subject, and preview share one left edge. The toolbar flag button (Ctrl+Alt+L) toggles follow-up flags; the Flagged mailbox gathers flagged messages across accounts. Replies default to HTML with your response above the quoted original, without a format-change prompt.

The compose window uses a compact toolbar with attachment and formatting controls on the left and Send on the right. Address fields use subtle dividers; formatting toolbars are hidden initially and can be toggled from the header. The toolbar stays available in fullscreen.

The top toolbar includes an Unread toggle: click once to show unread mail in the current mailbox, and again to restore all messages. It stays synchronized with Evolution’s existing filter controls.

## Build and install

On Omarchy/Arch, install Evolution and its build dependencies first:

```sh
omarchy pkg add evolution evolution-ews cmake ninja intltool glib2-devel
```

Evolution's upstream CMake checks report any additional development dependencies required by your system. The custom app and system Evolution Data Server/EWS modules must have compatible versions; this branch targets 3.60.2.

```sh
git clone --branch omarchy https://github.com/samcapshaw/evolution-omarchy.git
cd evolution-omarchy
./omarchy/build.sh
./omarchy/install.py
```

Close Evolution before installing. The default prefix is `~/.local/opt/evolution-omarchy`. Set `EVOLUTION_OMARCHY_PREFIX` consistently for both commands to use another absolute path.

Installation copies only the staged user-local prefix, never staged `/usr` files. It installs a wrapper at `~/.local/bin/evolution`, a desktop launcher override, stylesheet, icon and **All Inboxes** / **All Sent** rules. Compatible packaged EWS/Microsoft365 configuration modules are linked when present. Account setup uses Evolution's normal Google/Microsoft sign-in; no account profiles or credentials are included here.

To apply the initial layout defaults once:

```sh
./omarchy/preferences.py
```

The preferences script backs up existing Evolution settings before applying defaults. Do not run it on every launch: later user choices should remain persistent. Configure individual calendars normally in Evolution.

On desktops whose PATH prioritizes `/usr/bin`, launching plain `evolution` in a terminal still opens the packaged app. Use the desktop launcher or `~/.local/bin/evolution` explicitly.

## Validation

The matching source changes were compiled and exercised on an Omarchy desktop with Google Workspace and Microsoft365 mail/calendar backends. Unified mail and calendar views were inspected, calendar checkbox choices matched after reopening, and the custom viewer loaded without missing-library errors. Flagging was checked in both directions using synthetic local mail. The editor sandbox includes the custom library path so HTML replies can load their quoted content. The unread toolbar toggle was exercised against three synthetic messages: one unread message remained when enabled, and all three returned when disabled. No test email was sent. This is a personal desktop fork, not an upstream-supported release.

## Recovery and updates

The packaged app remains `/usr/bin/evolution`. Remove the local desktop override and wrapper to restore that launcher. Accounts and cached mail remain in Evolution's native locations. Rebuild and check the fork when system Evolution/Data Server versions change.

The original project README is preserved in `README.upstream.md`. Original source licenses remain in `COPYING*` and individual file headers. This fork is not affiliated with Apple or GNOME.
