# PuTTyMOD — what's different from upstream PuTTY

PuTTyMOD is a personal fork of [PuTTY](https://www.chiark.greenend.org.uk/~sgtatham/putty/)
0.83. All SSH/Telnet/SFTP/serial protocol code, cryptography, and the
overall architecture are unmodified upstream PuTTY, copyright Simon
Tatham and contributors (see `LICENCE`). Everything below is local,
on top of that base — this file exists so nothing gets lost or
misremembered as the project grows.

## Branding

- App-facing name changed from "PuTTY" to **"PuTTyMOD"** throughout
  (About box, window/dialog titles, error boxes, `--version`) via a
  single `be_list()` change in `unix/CMakeLists.txt`. The upstream
  copyright notice in the About box/licence text is deliberately
  **unchanged** — required by the MIT licence, and removing it was
  explicitly identified as the actual problem, not keeping it.
- Only the GTK GUI client is patched/rebuilt/rebranded. `plink`,
  `pscp`, `psftp`, `puttygen`, `pageant`, `psusan` are untouched
  upstream code and are not shipped by this fork's own package (see
  Packaging below) — no need to duplicate what Debian's own
  `putty-tools` already provides.

## Interface language (25 languages)

Own runtime `I_()` lookup-table system (`unix/i18n.c`/`.h`, **not**
gettext) — one generated C table per language
(`unix/i18n_table_<lang>.h`, built from `po/<lang>.po` via a local
`po2c.py` script), 405 msgids each. Languages: English (needs no
table) plus 24 translated — RU, ES, FR, DE, IT, PT, PL, NL, SV, UK,
CS, RO, TR, VI, ID, EL, ZH_CN, ZH_TW, JA, KO, AR, HE, HI, TH. Picked
from a dedicated **Design → Language** settings panel. Arabic/Hebrew
render correctly right-to-left with no special-casing needed — Pango
bidi-renders it, and embedded English technical terms (SSH, RSA, …)
come out as correct LTR runs for free. Unrecognised strings silently
pass through untranslated rather than crashing.

An **"Apply & Restart"** button on the Language panel saves the
choice and `execv()`s the running process in place
(`restart_self_with_new_language()`), so a language change is visible
immediately instead of only "next launch".

## Per-session distro detection + coloured icon

`unix/distro_detect.c` opens a raw TCP probe to the session's
host:port and classifies the pre-auth SSH banner by substring into
one of ~38 OS/distro icons (`unix/distro_icons.h`), stored as
`DistroIcon=` in the session file on connect. The saved-sessions list
also gets a hand-drawn cairo gradient tinted with that distro's brand
colour behind each row, icon and text redrawn crisply on top so
they're never dimmed.

> **Known open issue**: 37 of the 38 icons are now clean — 36 traced
> to free/open icon catalogues (dashboard-icons, Simple Icons,
> selfhst/icons, papirus-icon-theme), plus `astra`, which turned out
> to be an actual commercial trademark with no reuse licence (Astra
> Linux's own press kit doesn't grant one), so it was replaced with
> original artwork instead. Only `dropbear` remains unresolved. Must
> be resolved (redrawn or dropped) before any public/archive
> distribution — see `debian/copyright` and `debian/README.Debian`.

## Online/offline status per session

A small green/red dot composited onto each session's icon, reflecting
whether `host:port` is actually reachable — checked over async GIO
(never blocks the UI): TCP connect **and** a real SSH banner read (not
just a successful handshake — some of the user's VDS providers put
multiple unrelated customer boxes behind one shared IP, answering TCP
on any port regardless of which specific VM is up, so a banner check
was required to avoid false positives). Re-checked every 15 seconds
while the dialog is open. A separate `GNetworkMonitor`-based check
detects the *local* machine losing internet: all dots force red
immediately, a styled "No internet connection" badge appears next to
Open/Cancel, and connectivity coming back triggers an instant re-check
of every session rather than waiting for the next tick.

## One-click colour themes ("Оформление"/Design → Themes)

16 built-in presets (Dracula, Nord, Solarized Dark/Light, Gruvbox
Dark, One Dark, Monokai, Tokyo Night, Catppuccin Mocha/Latte, Ayu
Dark, Material Palenight, Everforest Dark, Rose Pine, Night Owl,
GitHub Dark) plus user-saved custom presets
(`~/.putty/custom-themes`). Applying one sets both the settings
dialog's own accent/background/text colours **and** the terminal's
core colour slots (default/bold fg+bg, cursor) in one click.

## Design section (profile-independent settings)

A whole settings category, applied identically to every saved session
regardless of which profile made the connection, persisted to
`~/.putty/global-appearance`:
- **Design/Themes** — one-click presets, above.
- **Design/Terminal** — live preview, font, cursor style/blink,
  background opacity %, full ANSI colour grid.
- **Design/Window** — the *settings dialog's own* chrome: accent/
  background/text colour via native colour pickers (deliberately not
  raw hex entry — "an ordinary person doesn't configure with codes"),
  separate dialog font and opacity.
- **Design/Language** — see above.

Dialog windows get an RGBA visual + a `.putty-dialog` CSS class
(`our_dialog_new()`, `unix/utils/our_dialog.c`) so this theming can
render as genuinely translucent where a compositor is present. The
terminal window has its own, independent translucency (`CONF_bg_alpha`,
default 0.82, only the background fills — text/cursor stay fully
opaque so glyphs stay crisp).

## Encrypted auto-login password

AES-256-CBC (`unix/pwcrypt.c`), keyed per-Unix-user
(`~/.putty/.pwkey`, mode 0600). The session file never holds
plaintext. The password editbox shows a grey 8-dot placeholder when a
password is stored, without ever redisplaying the real value. Ignored
automatically if a private key is configured for that session.

## Session export/import (`.pex` files)

`unix/session_export.c` bundles one or more saved sessions' raw
on-disk settings files into a single self-describing `.pex` file
(`PUTTY-EXPORT-2 PLAIN` or `... ENC` — one extension covers both).
Password-protected mode derives a key via **Argon2id** (the same KDF
this build already uses for encrypted `.ppk` v3 private keys) with a
random salt, then AES-256-CBC encrypts the container; params/salt/IV
are stored in the plaintext header so import can always re-derive the
same key. Export always shows a mandatory (non-conditional) red
warning about what happens without a password. Import detects
session-name collisions and asks for confirmation before overwriting
anything, all-or-nothing.

Packaged with its own file type: `.pex` files get a proper
`application/x-puttymod-export` MIME type and icon set (see
Packaging), so they show up correctly in a file manager instead of as
a blank/generic file.

## Session list UX

- **Search/filter** box above the saved-sessions list, case-
  insensitive ASCII substring match.
- **Save/Load/Delete** flash the clicked button's own label green
  with a checkmark for ~900ms instead of a separate, easy-to-miss
  status line.
- The built-in "Default Settings" row gets its own grey "?" icon
  instead of showing nothing (it has no host to auto-detect against).

## First-run experience

On the very first launch for a Unix user (detected by the absence of
`~/.putty/global-appearance`):
- Seeds the **Tokyo Night** colour theme and **English** interface
  language as the effective defaults (rather than plain compiled-in
  PuTTY defaults), persisted immediately.
- Shows a one-time **welcome dialog** explaining that sessions/
  passwords/settings all stay local, and where to change language/
  theme later. Never shown again once `global-appearance` exists.

## Packaging

A real `.deb` (`puttymod`, package name lowercase per Debian
convention), built with `debhelper` + `dh`/CMake buildsystem,
`Standards-Version` 4.7.2, DEP-5 `debian/copyright`, a hand-written
man page, a `.desktop` file, and proper `hicolor`-theme icon
installation (both the app icon and the `.pex` MIME icon — no more ad
hoc `~/.local/share` installs). Ships only the GUI client as
`/usr/bin/puttymod` (never `/usr/bin/putty`), so it installs alongside
the official `putty`/`putty-tools` packages with zero file overlap.
`lintian --pedantic` is clean (zero real errors/warnings).

Currently a **native** source package (`3.0 (native)`) since there's
no separate upstream tarball yet; converting to a normal upstream-
tarball + `debian/`-patches layout is planned once this project has
real GitHub releases to package against.

**Not yet resolved, tracked as a real blocker for any public/shared
distribution** (a personal local install is unaffected): the
distro-icon licensing gap above, see `debian/copyright` and
`debian/README.Debian` for the formal marker.

## Credits — third-party icon sources

Most of the distro/OS icons in `unix/distro_icons.h` are not
original artwork. 36 of the 38 have been traced to these free/open
icon projects (full per-icon breakdown in `debian/copyright`):

- [dashboard-icons](https://github.com/homarr-labs/dashboard-icons)
  by Bjorn Lammers, Meier Lukas, Thomas Camlong, and Homarr Labs —
  Apache License 2.0 — 31 icons.
- [Simple Icons](https://simpleicons.org) contributors — CC0 1.0
  Universal — 3 icons (elementary, popos, slackware).
- [selfhst/icons](https://github.com/selfhst/icons) contributors —
  Creative Commons Attribution 4.0 International — 1 icon (freebsd).
- [Papirus Icon Theme](https://github.com/PapirusDevelopmentTeam/papirus-icon-theme)
  by the Papirus Development Team — GNU GPL v3 or later — 1 icon
  (raspbian).

The `question` and `astra` icons are original artwork by this fork's
author. `astra` specifically replaced a copy of Astra Linux's actual
trademarked logo, once it became clear their own official press kit
grants no reuse licence — the replacement is a plain blue "A" on a
white badge, deliberately not resembling the real trademark.

The remaining 1 (dropbear) has no confirmed source yet — see the
note above.
