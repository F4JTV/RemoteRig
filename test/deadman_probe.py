# -*- coding: utf-8 -*-
"""Disparition silencieuse : la prise reste ouverte, mais plus rien n'arrive.
C'est le cas d'un telephone eteint ou d'un Wi-Fi coupe. TCP ne s'en apercoit
pas avant des dizaines de minutes ; le poste ne doit pas rester en emission."""
import hashlib, hmac, json, socket, struct, time

HOST, TCP = "127.0.0.1", 17800
PASSWORD = "secret"

def frame(o):
    b = json.dumps(o).encode(); return struct.pack(">I", len(b)) + b

def read_frame(s, t=3.0):
    s.settimeout(t); h = b""
    while len(h) < 4:
        c = s.recv(4 - len(h))
        if not c: return None
        h += c
    n = struct.unpack(">I", h)[0]; buf = b""
    while len(buf) < n:
        c = s.recv(n - len(buf))
        if not c: return None
        buf += c
    return json.loads(buf.decode())

s = socket.create_connection((HOST, TCP), timeout=3)
ch = read_frame(s)
key = hashlib.pbkdf2_hmac("sha256", PASSWORD.encode(), bytes.fromhex(ch["salt"]), 60000, 32)
cn = b"\x33" * 16
mac = hmac.new(key, bytes.fromhex(ch["nonce"]) + cn, hashlib.sha256).digest()
s.sendall(frame({"t": "auth", "nonce": cn.hex(), "mac": mac.hex(),
                 "encrypt": False, "codec": "pcm", "bitrate": 48000}))
read_frame(s)
print("  authentifie")

s.sendall(frame({"t": "cmd", "c": "ptt", "v": True}))
print("  emission demandee, puis plus rien n'est envoye — la prise reste ouverte")

# On ne ferme pas, on ne lit pas, on n'ecrit plus : le silence complet.
for step in (3, 9, 18):
    time.sleep(step - (0 if step == 3 else (3 if step == 9 else 9)))
    print(f"  --- apres {step} s de silence ---")

print("  la station est-elle rendue ?")
try:
    s2 = socket.create_connection((HOST, TCP), timeout=3)
    ch2 = read_frame(s2)
    print("  [ok]    station disponible" if ch2 and ch2.get("t") == "challenge"
          else f"  [ECHEC] station toujours prise : {ch2}")
    s2.close()
except Exception as e:
    print("  [ECHEC]", e)
s.close()
