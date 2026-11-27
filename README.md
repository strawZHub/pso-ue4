# PSO UE4 – client add-on & launcher

Open-source parts of the **PSO UE4** private-server community for *Pro Soccer Online* (UE4 Beta 1.6.4).

This repository contains **only the player-side code**:

| Folder | What it is |
|---|---|
| `addon/` | `xinput1_3.dll`: an add-on loaded by the game. It adds a dark **Moon** menu theme with an in-game Sun / Moon switch, coloured name tags and the scoreboard style, **goal effects** (visuals, sounds and a picker in the menu), a Goal Effects setting in Settings > Video, and keeps the launcher up to date. |
| `launcher/` | `PSOLauncher.exe` (**build 28**, the one players use): installs and updates the game files from a PSO server, starts the game, hides while you play and comes back when the game closes. **Download it from [Releases](../../releases).** |

**Not included, on purpose:**
- The game itself (executable, `.pak` files, art). *Pro Soccer Online* belongs to Skywall Studios. Get it on Steam.
- The server, database, owner panel and admin tools.
- The match-server side of the add-on. The released `xinput1_3.dll` is built from a larger private tree: the same file also runs on the community's match servers, and that server-only code (and the owner's own tools) is not in this repository. What players' games run is the code in `addon/`.
- `pso_core.dll` (the private-server client DLL that `xinput1_3.dll` forwards to).

## How the add-on works

The game loads `xinput1_3.dll` from `Binaries\Win64`. All XInput functions are forwarded to `pso_core.dll` (see `addon/xinput1_3.def`), so controllers keep working.

The add-on then reads the game's UE 4.27 reflection data (UObject / UProperty layout) to find the game's widgets and objects, and changes them through the game's own functions on the game thread. It only works with the game builds it was written for (`refl.h` checks the exe size and disables itself otherwise).

What it changes besides looks, so nobody has to guess:
- **Stamina:** the player's own stamina refills 5% faster (`stamina.h`). The match servers apply the same bonus to everyone, so nobody gains over anyone else.
- **Stutter:** the engine's memory clean-up interval is raised from 61 s to 15 min by one line in the game's `Engine.ini` (`gc_interval_setup` in `pso_addon.c`). A `pso-keep-gc.txt` file in `%LOCALAPPDATA%\UE4PSO` leaves the file alone.
- **Region label:** the Custom Lobbies lists read "NA (Virginia)" instead of "NA (Ohio)" (`region.h`).

Files:
- `theme.h`: the colour engine (Sun / Moon), the in-game switch, hover handling, scoreboard and icons.
- `tags2.h`: coloured / rainbow name tags and chat colours (the tag list comes from the server's `/pso-tags`).
- `goalfx.h`, `d3dfx.h`: the goal effects. They are drawn inside the game's own picture (a Direct3D 11 `Present` hook) and the sounds are made in code. `clown_pcm.h` is the one recorded sound, arranged from the CC0 VSCO 2 Community Edition sample library.
- `clown3d.h`: the clown effect on the goalkeeper (wig, white face, red nose).
- `goaltab.h`, `goalvid.h`, `goalsync.h`: the effect picker in the menu, the Goal Effects row in Settings > Video, and sharing each player's choice through the server.
- `region.h`: the region label.
- `stamina.h`: the stamina bonus (the file also holds the match-server half).
- `refl.h`: minimal UE 4.27 reflection (object array, names, properties).
- `uidump.h`, `watchdog.h`: troubleshooting helpers (a list of the menu's widgets on request; a note in the log if the game stops responding).
- `launcher_upd.h`: replaces an older launcher with a newer build from the server.

## What the launcher does

So nobody has to guess (everything is in `launcher/psolauncher.c`):
- Talks to the community server `play.pso-ue4.com` over HTTPS for the file list, news and launcher updates. Since build 26 the server's certificate is checked by Windows like in a browser (Let's Encrypt); if it can't be verified, the launcher downloads nothing.
- Downloads nothing unless the file list is approved (signed) by the independent reviewer, **Vxie** (see **Security** below). The server owner can upload files but can't approve them.
- Downloads the game files into the folder you choose and checks each file's SHA-256 against the signed list. Big files come from the server's Cloudflare storage (`files.pso-ue4.com`) when available, otherwise from the server itself - either way the same SHA-256 check applies.
- Reads your Steam account name, avatar, Pro Soccer Online play time and achievements from your own Steam folder (`steamstats.h`, read only) to show them in the launcher window ("Your Steam account"), only once Steam is running. It does not sign in to Steam and never asks for a password; none of that is sent anywhere.
- Sends your SteamID64 to the server only when you press **GET CODE** to link Discord (`/link/code`), together with a random per-PC code the game add-on saved, so nobody can link your account from another PC. It never asks for or reads your Steam password.
- **Ask before updating** (Settings, build 27+): updates wait for you; the launcher first lists every file that changes (with size) and only installs when you say yes - the same for a new launcher build.
- Starts the game, hides while you play, and checks for updates when the game closes. It can replace itself with a newer launcher build from the server, but only with the exact file (size and SHA-256) named in the signed list.
- On Linux/Mac (Wine, Proton) it finds the real Steam folder; it recognises Wine by the `HKCU\Software\Wine` registry key Wine creates.
- Logs to `%LOCALAPPDATA%\UE4PSO\launcher.log`.
- The font is Space Grotesk (SIL Open Font License 1.1, see the note at the top of `fonts.h`).

Settings are saved in `%LOCALAPPDATA%\UE4PSO` (`theme.txt`, `goalfx-on.txt`, ...), and the log is `pso-addon.log` in the same folder.

## Security (launcher build 26+)

- **Transport:** HTTPS with normal certificate checking (no "accept any certificate" flags, no fallback to a bare IP address).
- **Signed file list, approved by an independent reviewer (build 28+):** every file list must be signed by **Vxie** with an
  ECDSA P-256 key that only exists on his PC. The text he signs is `PSO-FILES-1`, then one line per game file
  `path<TAB>size<TAB>sha256` (sorted by path), then `@launcher<TAB>size<TAB>sha256` for the launcher the server hands out.
  The server passes his signature on as `"sig2"` in `/launcher/manifest`; the launcher rebuilds the same text and checks it with the
  only key built into it (`FILES_PUB` in `launcher/psolauncher.c`, fingerprint **EC18 3E77 6C38 2447**, also shown in the launcher's
  Settings). Without his approval nothing is downloaded and no launcher update is taken - whoever runs the server (strawZ included)
  can upload files but cannot approve them. Who approves and what is waiting: https://play.pso-ue4.com/approval
- Builds 26/27 checked the server owner's key instead (`"sig"`); that key was retired after the handover to the reviewer.
- **Files:** SHA-256 per file (SHA-1 is only kept as the file's name on the Cloudflare storage).
- **Public update log:** every signed list is recorded in [`UPDATES.md`](UPDATES.md) (what changed, with SHA-256) and the newest signed text is [`file-list.txt`](file-list.txt), so the whole update history is public.
- **Check it yourself:** `python launcher/verify_file_list.py` checks the server's list and signature with the same rules;
  `python launcher/verify_file_list.py "<your game folder>"` also checks every installed file against it.
- Reviewer's public key (X || Y): `26e9eaeccdea68d9fb4c11019308193da0c33c180d46b634a40a024c962772ea895a4c7535da4b593d2ab4ea97050ed15cbb04ff763af4785089487002a5dee9`
- Not covered: the very first download of the launcher itself - check its SHA-256 against the release (see VERIFY.md).

## Building

You need [Zig](https://ziglang.org/) (used as a C compiler for Windows), or `pip install ziglang` and use `python -m ziglang`.

**Add-on**
```
cd addon
zig cc -target x86_64-windows-gnu pso_addon.c xinput1_3.def -lwinhttp -luser32 -lgdi32 -lgdiplus -lws2_32 -lwinmm -shared -O2 -o xinput1_3.dll
```

**Launcher**
1. Put your own `hero.jpg`, `backdrop.jpg`, `logo.png` and `app.ico` in `launcher/` and run `python make_art.py`. The images are not included.
2. The server address is `DEFAULT_SERVER` in `psolauncher.c` (`play.pso-ue4.com`). For your own server, change it or put `server=<address>` in a `PSOLauncher.ini` next to the exe.
3. Build (zig 0.17.1, `pip install ziglang==0.17.1`):
```
cd launcher
python -m ziglang cc -target x86_64-windows-gnu -O2 -DUNICODE -municode -Wl,--subsystem,windows psolauncher.c app.rc -lwinhttp -lbcrypt -ldwmapi -lshell32 -lshlwapi -lole32 -lgdi32 -luser32 -lgdiplus -ladvapi32 -o PSOLauncher.exe
```
(`-Wl,--subsystem,windows` matters: without it zig makes a command-prompt program.)

## License

GPL-3.0: see `LICENSE`. You can use and change this code, but anything you publish that's based on it must also be open source under GPL-3.0, with credit.

**Please don't use the name "PSO UE4" or its logo for your own fork.**

*Pro Soccer Online* is a trademark of its owners. This project isn't affiliated with or endorsed by Skywall Studios.
