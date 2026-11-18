# PSO UE4 – client add-on & launcher

Open-source parts of the **PSO UE4** private-server community for *Pro Soccer Online* (UE4 Beta 1.6.4).

This repository contains **only the player-side code**:

| Folder | What it is |
|---|---|
| `addon/` | `xinput1_3.dll`: an add-on loaded by the game. It adds a dark **Moon** menu theme with an in-game Sun / Moon switch, coloured name tags and the scoreboard style, and keeps the launcher up to date. |
| `launcher/` | `PSOLauncher.exe`: installs and updates the game files from a PSO server, starts the game, hides while you play and comes back when the game closes. |

**Not included, on purpose:**
- The game itself (executable, `.pak` files, art). *Pro Soccer Online* belongs to Skywall Studios. Get it on Steam.
- The server, database, owner panel and admin tools.
- `pso_core.dll` (the private-server client DLL that `xinput1_3.dll` forwards to).

## How the add-on works

The game loads `xinput1_3.dll` from `Binaries\Win64`. All XInput functions are forwarded to `pso_core.dll` (see `addon/xinput1_3.def`), so controllers keep working.

The add-on then reads the game's UE 4.27 reflection data (UObject / UProperty layout) to find menu widgets and change their colours through the widgets' own setters on the game thread. It never touches gameplay, and it only works with the exact game build it was written for (`refl.h` checks the exe size and disables itself otherwise).

- `theme.h`: the colour engine (Sun / Moon), the in-game switch, hover handling, scoreboard and icons.
- `tags2.h`: coloured / rainbow name tags (the tag list comes from the server's `/pso-tags`).
- `refl.h`: minimal UE 4.27 reflection (object array, names, properties).
- `launcher_upd.h`: replaces an older launcher with a newer build from the server.

The setting is saved in `%LOCALAPPDATA%\UE4PSO\theme.txt`, and the log is at `%LOCALAPPDATA%\UE4PSO\pso-addon.log`.

## Building

You need [Zig](https://ziglang.org/) (used as a C compiler for Windows), or `pip install ziglang` and use `python -m ziglang`.

**Add-on**
```
cd addon
zig cc -target x86_64-windows-gnu pso_addon.c xinput1_3.def -lwinhttp -luser32 -lgdi32 -lgdiplus -lws2_32 -shared -O2 -o xinput1_3.dll
```

**Launcher**
1. Put your own `hero.jpg`, `backdrop.jpg`, `logo.png` and `app.ico` in `launcher/` and run `python make_art.py`. The images are not included.
2. Set your server address in `DEFAULT_SERVER` in `psolauncher.c`, or put `server=<address>` in a `PSOLauncher.ini` next to the exe.
3. Build:
```
cd launcher
zig cc -target x86_64-windows-gnu -municode psolauncher.c app.rc -lwinhttp -lbcrypt -luser32 -lgdi32 -lgdiplus -lole32 -lshell32 -ladvapi32 -ldwmapi -lshlwapi -lcomctl32 -luuid -Wl,--subsystem,windows -O2 -o PSOLauncher.exe
```

## License

GPL-3.0: see `LICENSE`. You can use and change this code, but anything you publish that's based on it must also be open source under GPL-3.0, with credit.

**Please don't use the name "PSO UE4" or its logo for your own fork.**

*Pro Soccer Online* is a trademark of its owners. This project isn't affiliated with or endorsed by Skywall Studios.
