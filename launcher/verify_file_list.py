"""Check the launcher's file list yourself (no extra packages, Python 3.8+).

    python verify_file_list.py                 -> is the server's file list signed with the owner's key?
    python verify_file_list.py "<game folder>"  -> ...and does every file in that folder match it (SHA-256)?

This is the same check launcher build 26 makes before it downloads anything: the text below
("PSO-FILES-1", then every file as path<TAB>size<TAB>sha256, sorted, then the launcher the server hands out)
must carry an ECDSA P-256 signature made with the owner's private key. The public key is FILES_PUB in
psolauncher.c and below.
"""
import hashlib, json, os, ssl, sys, urllib.request

SERVER = "play.pso-ue4.com"
FILES_PUB = "05a82c0ae69e0a1ef691234ccd3069454327cd2844d405ba8a34e925f172e9174e27c188d95e48f007531b11cee723ae2bc5c75833fc68cee2340ad2d652ee3f"

P = 0xffffffff00000001000000000000000000000000ffffffffffffffffffffffff
N = 0xffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551
B = 0x5ac635d8aa3a93e7b3ebbd55769886bc651d06b0cc53b0f63bce3c3e27d2604b
G = (0x6b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296,
     0x4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5)

def add(a, b):
    if a is None: return b
    if b is None: return a
    if a[0] == b[0]:
        if (a[1] + b[1]) % P == 0: return None
        l = (3 * a[0] * a[0] - 3) * pow(2 * a[1], -1, P) % P
    else:
        l = (b[1] - a[1]) * pow(b[0] - a[0], -1, P) % P
    x = (l * l - a[0] - b[0]) % P
    return (x, (l * (a[0] - x) - a[1]) % P)

def mul(k, pt):
    r = None
    while k:
        if k & 1: r = add(r, pt)
        pt = add(pt, pt); k >>= 1
    return r

def ecdsa_ok(pub, msg, sig):
    x, y = int(pub[:64], 16), int(pub[64:], 16)
    r, s = int(sig[:64], 16), int(sig[64:], 16)
    if not (0 < r < N and 0 < s < N) or (y * y - (x ** 3 - 3 * x + B)) % P:
        return False
    e = int(hashlib.sha256(msg).hexdigest(), 16)
    w = pow(s, -1, N)
    pt = add(mul(e * w % N, G), mul(r * w % N, (x, y)))
    return pt is not None and pt[0] % N == r

def main():
    ctx = ssl.create_default_context()                       # the certificate is checked, like the launcher does
    man = json.loads(urllib.request.urlopen(f"https://{SERVER}/launcher/manifest", context=ctx, timeout=30).read())
    files = sorted(man["files"], key=lambda f: f["p"])
    text = "PSO-FILES-1\n" + "".join(f"{f['p']}\t{f['s']}\t{f['h2']}\n" for f in files)
    if man.get("self"):
        text += f"@launcher\t{man['self']['s']}\t{man['self']['h2']}\n"
    ok = bool(man.get("sig")) and ecdsa_ok(FILES_PUB, text.encode(), man["sig"])
    print(f"version {man['version']}: {len(files)} files, signature {'VALID' if ok else 'MISSING OR INVALID'}")
    if man.get("self"):
        print(f"launcher the server hands out: {man['self']['s']} bytes, SHA-256 {man['self']['h2']}")
    if len(sys.argv) > 1 and ok:
        bad = 0
        for f in files:
            p = os.path.join(sys.argv[1], *f["p"].split("/"))
            if not os.path.isfile(p):
                print("  missing:", f["p"]); bad += 1; continue
            h = hashlib.sha256()
            with open(p, "rb") as fh:
                for chunk in iter(lambda: fh.read(1 << 20), b""):
                    h.update(chunk)
            if h.hexdigest() != f["h2"]:
                print("  DIFFERENT:", f["p"]); bad += 1
        print("all files match the signed list" if not bad else f"{bad} file(s) don't match")
    return 0 if ok else 1

if __name__ == "__main__":
    sys.exit(main())
