"""Creates art.h (the launcher's pictures) and checks app.ico is present.

Put your own images next to this script, then run:  python make_art.py
    hero.jpg      big picture at the top of the launcher (about 1280x560)
    backdrop.jpg  background behind the window content (small, gets blurred/dark)
    logo.png      square logo (128x128)
    app.ico       the launcher's icon
"""
import os, sys
here = os.path.dirname(os.path.abspath(__file__))
parts = [("HERO_JPG", "hero.jpg"), ("BACKDROP_JPG", "backdrop.jpg"), ("LOGO_PNG", "logo.png")]
out = []
for name, fn in parts:
    p = os.path.join(here, fn)
    if not os.path.exists(p):
        sys.exit(f"missing {fn}")
    b = open(p, "rb").read()
    out.append(f"static const unsigned char {name}[{len(b)}] = {{{','.join(map(str, b))}}};\n")
open(os.path.join(here, "art.h"), "w").write("".join(out))
if not os.path.exists(os.path.join(here, "app.ico")):
    print("note: add app.ico (the launcher icon) before building")
print("art.h written")
