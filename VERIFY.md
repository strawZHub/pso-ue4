# Verify the downloads match this source

You don't have to trust anyone blindly. Each release lists the SHA-256 of its files.

## Check a file you were given (no coding needed)

Windows, in a terminal, in the folder with the file:

```
certutil -hashfile PSOLauncher.exe SHA256
certutil -hashfile xinput1_3.dll SHA256
```

If the hash matches the one listed here (and on the release), your file is exactly the published one.

## Launcher - build 26 (current)

```
SHA256:  3688a1ad7f4ea6386e26d59c916aac70eff382ab66dee4d4fd3e3a87db30cec8
file:    PSOLauncher.exe  (1,371,648 bytes)
built from: launcher/ at this commit + the launcher's pictures (art.h, app.ico)
compiler: ziglang 0.17.1
```

Build 26 checks the server's certificate and the owner's signature on the file list before downloading anything
(README, "Security"). `launcher/verify_file_list.py` makes the same check from Python.

Build 25 (previous): `c9777ab5e964871fe5511a0b2ffb9c94c8081c29e413470a66e0a908c9f5c4e1`, 1,368,064 bytes.

The launcher's pictures (the big header image, background, logo and icon) are not in this repository, so a
rebuild from this source with your own pictures will differ from the release only in those embedded pictures.
Everything the launcher *does* is the code in `launcher/`.

## Add-on (xinput1_3.dll)

The add-on build is **reproducible**: compile `addon/` with the exact compiler and you get a byte-for-byte
identical file.

1. Install the exact compiler:  `pip install ziglang==0.17.1`
2. From the repo root, run `build-addon.bat`  (or the one command inside it).
3. Compare the printed SHA256 to the add-on hash of the matching release.

The code in `addon/` at this commit builds to:

```
SHA256:  b6202fd850ff0d92b978e59f04bb72c032375c2a5c2e46d0a52d5f67527c1b4f
file:    xinput1_3.dll
```

Note: the add-on code in `addon/` is from an earlier release; an update matching the add-on players run today
is coming in a later commit. Until then, the hash of the current add-on will not match a build of this folder.

## What's not here

The match-server build of the add-on (run on the community's servers, not by players) is a separate, larger
binary and is not in this repo. See the README for the full list of what the add-on and launcher do.
