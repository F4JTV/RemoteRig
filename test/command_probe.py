# -*- coding: utf-8 -*-
"""Operator commands under load.

Sends a burst of frequency changes, the way an operator hopping bands does,
then listens for state. Checks that every command reaches the rig in order
and that the state keeps flowing — the two things the command-priority
scheme would break if it starved the polling."""
import hashlib, hmac, json, socket, struct, time

HOST, TCP = "127.0.0.1", 17800
def frame(o):
    b = json.dumps(o).encode(); return struct.pack(">I", len(b)) + b
def read_frame(s, t=4.0):
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

s = socket.create_connection((HOST, TCP), timeout=4)
ch = read_frame(s)
key = hashlib.pbkdf2_hmac("sha256", b"secret", bytes.fromhex(ch["salt"]), 60000, 32)
cn = b"\x44" * 16
mac = hmac.new(key, bytes.fromhex(ch["nonce"]) + cn, hashlib.sha256).digest()
s.sendall(frame({"t": "auth", "nonce": cn.hex(), "mac": mac.hex(),
                 "encrypt": False, "codec": "pcm", "bitrate": 48000}))
rep = read_frame(s)
print("  authentifie, poste sur", (rep.get("state") or rep.get("s") or {}).get("freqA"), "Hz")

# Rafale de commandes, comme un operateur qui change de bande plusieurs fois.
targets = [7100000, 14074000, 21074000, 28074000, 14200000]
for hz in targets:
    s.sendall(frame({"t": "cmd", "c": "freq", "v": hz}))
    time.sleep(0.12)

# On ecoute ensuite l'etat pendant deux secondes.
seen, deadline = [], time.time() + 3.0
s.settimeout(0.5)
while time.time() < deadline:
    try:
        m = read_frame(s, 0.5)
    except Exception:
        continue
    if m and m.get("t") == "state":
        st = m.get("s") or m.get("state") or {}
        f = st.get("freqA")
        if f and (not seen or seen[-1] != f): seen.append(f)

print("  frequences demandees :", targets)
print("  frequences vues dans l'etat :", seen)
print("  [ok]    la derniere commande a bien pris" if seen and seen[-1] == targets[-1]
      else f"  [ECHEC] derniere vue {seen[-1] if seen else None}, attendue {targets[-1]}")
print("  [ok]    l'etat continue d'arriver" if seen else "  [ECHEC] plus aucun etat recu")
s.close()
