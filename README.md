<div align="center">

# Dungeon Runners Addons

[![Windows Installer](https://img.shields.io/badge/%E2%80%8B-Installer-2563EB?style=for-the-badge&logo=data%3Aimage%2Fsvg%2Bxml%3Bbase64%2CPHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCAyNCAyNCI%2BPHBhdGggZmlsbD0id2hpdGUiIGQ9Ik0wIDBoMTF2MTFIMHpNMTMgMGgxMXYxMUgxM3pNMCAxM2gxMXYxMUgwek0xMyAxM2gxMXYxMUgxM3oiLz48L3N2Zz4%3D)](https://github.com/MaxmilianBaron/Dungeon-Runners-Addons/releases/latest/download/Dungeon-Runners-Addons.zip "Windows Installer")
[![Mac Installer](https://img.shields.io/badge/%E2%80%8B-Installer-2563EB?style=for-the-badge&logo=apple&logoColor=white)](https://github.com/MaxmilianBaron/Dungeon-Runners-Addons/releases/latest/download/Dungeon-Runners-Addons-Mac.zip "Mac Installer")
[![Linux Installer](https://img.shields.io/badge/%E2%80%8B-Installer-2563EB?style=for-the-badge&logo=linux&logoColor=white)](https://github.com/MaxmilianBaron/Dungeon-Runners-Addons/releases/latest/download/Dungeon-Runners-Addons-Linux.zip "Linux Installer")

</div>

**Install** — Close the game → extract the installer ZIP → run the file below → select the folder containing `DungeonRunners.exe`.

`Windows`: `Install.cmd` · `Mac`: `Install.command` · `Linux`: `sh Install.sh`

**Update** — For existing installations. Close the game → run the included `Update` file in the game's `Addons` folder. Updates installed addons and adds new ones, preserving settings and history.

**Settings** — `ESC` → `Addons`

- **Damage Meter** — Damage Done/Taken, DPS, skill details, crit statistics, history and `/g` reports from combat observed by your client.
- **Hide Gold Labels** — Hides gold labels without affecting pickup.
- **Cooldown Timers** — Skill, `Buff` and `Curse` timers with a hotbar flash when ready.
- **Nameplates** — Toggle HP/Mana, names and Posse for yourself and other players, plus Bling Gnome and Flaming Buddy HP bars.
- **Better Character Sheet** — Shows Weapon Crit, Magic Crit, Stun Resist, Movement and resist chances.
- **Mythic Drop Sounds** — Sound and chat alerts for your and party Mythic drops, with volume and custom WAV/MP3 support.

<details>
<summary>Mac / Linux setup</summary>

**Mac** — `macOS 10.15+`, with the game already working through [CrossOver](https://www.codeweavers.com/crossover) or [Wine](https://www.winehq.org/). Keep using your existing game launcher.

**Linux** — `Python 3.8+`. Run `sh Install.sh` from a terminal in the extracted installer folder, without `sudo`.

**Update** — In the game's `Addons` folder, open `Update.command` on Mac or run `sh Update.sh` on Linux. If missing, run the installer again.

**Addons not showing?** — Open the game's Wine/CrossOver configuration → `Libraries` → set `d3d9` to `Native then Builtin`. In launchers with environment settings, add `d3d9=n,b` to `WINEDLLOVERRIDES`, preserving existing overrides.

</details>
