# PuTTyMOD

PuTTyMOD is a personal fork of [PuTTY](https://www.chiark.greenend.org.uk/~sgtatham/putty/)
0.83 — same SSH/Telnet/SFTP core, cryptography and overall architecture
as upstream, with a set of local UX patches layered on top: per-session
distro detection with coloured icons, online/offline status dots, one-click
colour themes, encrypted auto-login password, password-protected session
export/import, and a runtime interface-language switcher (25 languages).

See [FEATURES.md](FEATURES.md) for the full list of what's different from
stock PuTTY, and [debian/](debian) for the Debian packaging.

## Building

This fork changes nothing about how PuTTY itself is built — the original
upstream build instructions in [README](README) still apply as-is (CMake,
same targets, same platform requirements).

## Licence

PuTTyMOD is a modified/redistributed copy of PuTTY and stays under the
same [MIT licence](LICENCE) as upstream, copyright Simon Tatham and
contributors, plus this fork's own changes — see
[debian/copyright](debian/copyright) for the full per-file breakdown.
