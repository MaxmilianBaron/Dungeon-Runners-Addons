# Building

Build on Windows with PowerShell 5.1+, Python 3.8+ on `PATH`, and Visual Studio 2022 Build Tools with **Desktop development with C++** (MSVC x86/x64 and Windows SDK). No additional Python packages are required.

Use `game.pki` and `game.pkg` from your game installation. The supported `game.pki` SHA-1 is `09b7cc6479dba96dbc8090f65a0f5720de605329`; resource hashes are checked during generation.

From the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\build.ps1 -ClientDirectory "C:\Games\Dungeon Runners"
```

The script generates the required headers and UI resources, then builds both DLLs for x86. Output defaults to `.build\native`; override it with `-OutputDirectory` outside the game directory. Linux and macOS use the same DLLs through Wine/CrossOver.

For local testing, install the release once, close the game, then copy these build outputs:

| Build output | Destination in the game folder |
| --- | --- |
| `d3d9.dll` | `d3d9.dll` |
| `Addons.dll`, `ui.bin`, `ui-resources.json` | `Addons\Runtime\` |
| `Addons\<addon>\addon.ini` | `Addons\<addon>\addon.ini` |

Generated headers, provenance files and compiler intermediates stay in the build directory. Game packages and generated assets are not included in the source repository.
