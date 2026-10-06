# Building

Build on Windows with PowerShell 5.1+, Python 3.8+, CMake and Ninja on `PATH`, and Visual Studio Build Tools with **Desktop development with C++**, the **v141 x86/x64 and Windows XP support** components, and the Windows 10 10240 static CRT. No additional Python packages are required.

Use `game.pki` and `game.pkg` from your game installation. The supported `game.pki` SHA-1 is `09b7cc6479dba96dbc8090f65a0f5720de605329`; resource hashes are checked during generation.

From the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\build.ps1 -ClientDirectory "C:\Games\Dungeon Runners"
```

The script generates the required headers and UI resources, then builds both DLLs for x86 with the Windows 5.1 target and checks their imports against the XP SP3 / XP x64 SP2 contract. Output defaults to `.build\native`; override it with `-OutputDirectory` outside the game directory. Linux and macOS use the same DLLs through Wine/CrossOver. The leaderboard uses checksum-pinned Mbed TLS 3.6.7 and bundled Mozilla certificates with TLS 1.2 and certificate verification, independently of Windows TLS defaults. The first build downloads the pinned TLS sources; subsequent builds reuse the verified archive.

For local testing, install the release once, close the game, then copy these build outputs:

| Build output | Destination in the game folder |
| --- | --- |
| `d3d9.dll` | `d3d9.dll` |
| `Addons.dll`, `ui.bin`, `ui-resources.json` | `Addons\Runtime\` |
| `Addons\<addon>\addon.ini` | `Addons\<addon>\addon.ini` |
| `Addons\Licenses\*.txt` | `Addons\Licenses\` (keep existing files) |

Third-party notices are also embedded in `Addons.dll` as RCDATA resource 301, including the Mbed TLS and Mozilla certificate licenses and source locations.

Generated headers, provenance files and compiler intermediates stay in the build directory. Game packages and generated assets are not included in the source repository.
