<div align="center">

# Dungeon Runners Addons

[![Windows Installer](https://raw.githubusercontent.com/MaxmilianBaron/Dungeon-Runners-Launcher/main/.github/windows-installer.svg)](https://github.com/MaxmilianBaron/Dungeon-Runners-Launcher "Windows Installer")
[![Mac Installer](https://img.shields.io/badge/%E2%80%8B-Installer-2563EB?style=for-the-badge&logo=apple&logoColor=white)](https://github.com/MaxmilianBaron/Dungeon-Runners-Launcher "Mac Installer")
[![Linux Installer](https://img.shields.io/badge/%E2%80%8B-Installer-2563EB?style=for-the-badge&logo=linux&logoColor=white)](https://github.com/MaxmilianBaron/Dungeon-Runners-Launcher "Linux Installer")
[![Steam Deck Installer](https://img.shields.io/badge/%E2%80%8B-Installer-2563EB?style=for-the-badge&logo=steamdeck&logoColor=white)](https://github.com/MaxmilianBaron/Dungeon-Runners-Launcher "Steam Deck Installer")
[![Android Installer](https://img.shields.io/badge/%E2%80%8B-Installer-2563EB?style=for-the-badge&logo=android&logoColor=white)](https://github.com/MaxmilianBaron/Dungeon-Runners-Launcher "Android Installer")

</div>

**[Dungeon Runners Launcher](https://github.com/MaxmilianBaron/Dungeon-Runners-Launcher)** — Game installation and individual addon management.

**Install** — Close the game → extract the installer ZIP → run the file below → select the folder containing `DungeonRunners.exe`.

`Windows`: `Install.cmd` · `Mac`: `Install.command` · `Linux`: `sh Install.sh`

**Update** — For existing installations. Close the game → run the included `Update` file in the game's `Addons` folder. Updates installed addons and adds new ones, preserving settings and history.

**Settings** — `ESC` → `Addons`

- **Damage Meter** — DPS, damage, skills and combat history.
- **Hide Gold Labels** — Hides gold labels without affecting pickup.
- **Cooldown Timers** — Skill, buff and curse timers.
- **Nameplates** — Player and pet health, mana and names.
- **Better Character Sheet** — Critical, movement and resistance statistics.
- **Mythic Drop Sounds** — Mythic loot sound and chat alerts.
- **Wishing Well Tracker** — Well cooldown and reminders.
- **Moveeverything** — Move and resize interface elements.
- **Cursor Circle** — Adjustable combat cursor highlight.
- **Loadouts & Sorting** — Gear and hotbar saves; Inventory and selected bank page sorting.
- **Leaderboard** — Official PvE rankings and PvP Rating; all time or this week.
- **Controller** — Gamepad movement, combat and menus.
- **Enhanced Settings** — Camera, attack controls and low-HP warning.

<details>
<summary>Mac / Linux setup</summary>

**Mac** — `macOS 10.15+`, with the game already working through [CrossOver](https://www.codeweavers.com/crossover) or [Wine](https://www.winehq.org/). Keep using your existing game launcher.

**Linux** — `Python 3.8+`. Run `sh Install.sh` from a terminal in the extracted installer folder, without `sudo`.

**Update** — In the game's `Addons` folder, open `Update.command` on Mac or run `sh Update.sh` on Linux. If missing, run the installer again.

**Addons not showing?** — Open the game's Wine/CrossOver configuration → `Libraries` → set `d3d9` to `Native then Builtin`. In launchers with environment settings, add `d3d9=n,b` to `WINEDLLOVERRIDES`, preserving existing overrides.

</details>

---

<div align="center">

[![Dungeon Runners Community — Discord](https://img.shields.io/badge/Dungeon_Runners_Community-5865F2?style=for-the-badge&logo=discord&logoColor=white)](https://discord.gg/RFpRET5ry)

</div>
