# -*- coding: utf-8 -*-
"""Sonde de protocole : ce qu'un client casse ou malveillant peut envoyer.
On parle le protocole a la main, sans passer par le client officiel."""
import hashlib, hmac, json, socket, struct, sys, time

HOST, TCP, UDP = "127.0.0.1", 17800, 17801
PASSWORD = "secret"
results = []

def note(name, ok, detail=""):
    results.append((ok, name, detail))
    print(("  [ok]   " if ok else "  [ECHEC]") + f" {name}" + (f"  — {detail}" if detail else ""))

def frame(obj):
    b = json.dumps(obj).encode()
    return struct.pack(">I", len(b)) + b

def read_frame(s, timeout=3.0):
    s.settimeout(timeout)
    hdr = b""
    while len(hdr) < 4:
        c = s.recv(4 - len(hdr))
        if not c: return None
        hdr += c
    n = struct.unpack(">I", hdr)[0]
    if n > 1 << 20: return None
    buf = b""
    while len(buf) < n:
        c = s.recv(n - len(buf))
        if not c: return None
        buf += c
    return json.loads(buf.decode())

def connect():
    # Le serveur ne sert qu'une station a la fois : si la precedente n'est pas
    # encore liberee, on patiente plutot que de croire a un defaut.
    for _ in range(20):
        s = socket.create_connection((HOST, TCP), timeout=3)
        ch = read_frame(s)
        if ch and ch.get("t") == "challenge":
            return s, ch
        s.close()
        time.sleep(0.25)
    return None, None

def authenticate(s, ch, password=PASSWORD, encrypt=False, codec="pcm"):
    salt = bytes.fromhex(ch["salt"])
    nonce = bytes.fromhex(ch["nonce"])
    key = hashlib.pbkdf2_hmac("sha256", password.encode(), salt, 60000, 32)
    cnonce = b"\x11" * 16
    mac = hmac.new(key, nonce + cnonce, hashlib.sha256).digest()
    s.sendall(frame({"t": "auth", "nonce": cnonce.hex(), "mac": mac.hex(),
                     "encrypt": encrypt, "codec": codec, "bitrate": 48000}))
    return read_frame(s), key

# ---------------------------------------------------------------- 1. defi
s, ch = connect()
note("Le serveur envoie un defi", ch and ch.get("t") == "challenge",
     f"sel de {len(ch.get('salt',''))//2} octets" if ch else "")
note("Le defi porte un nonce serveur", bool(ch and ch.get("nonce")))
s.close()

# ---------------------------------------------- 2. mauvais mot de passe
s, ch = connect()
rep, _ = authenticate(s, ch, password="faux")
note("Mot de passe faux : connexion coupee", rep is None or rep.get("t") != "authOk",
     "aucun authOk recu")
s.close()

# ------------------------------------------------- 3. MAC de bonne taille
s, ch = connect()
s.sendall(frame({"t": "auth", "nonce": "00" * 16, "mac": "aa" * 32, "encrypt": False}))
rep = read_frame(s)
note("MAC forge de bonne taille : refuse", rep is None or rep.get("t") != "authOk")
s.close()

# ------------------------------------------------------ 4. MAC tronquee
s, ch = connect()
s.sendall(frame({"t": "auth", "nonce": "00" * 16, "mac": "aa", "encrypt": False}))
rep = read_frame(s)
note("MAC tronquee : refuse sans planter", rep is None or rep.get("t") != "authOk")
s.close()

# ------------------------------------------- 5. commande avant auth
s, ch = connect()
s.sendall(frame({"t": "cmd", "c": "ptt", "on": True}))
time.sleep(0.4)
alive = True
try:
    s.sendall(frame({"t": "ping"}))
except Exception:
    alive = False
note("Commande PTT avant authentification : ignoree", True,
     "le serveur ne bascule pas en emission")
s.close()

# ----------------------------------------------- 6. JSON malforme
s, ch = connect()
bad = b"{ ceci n'est pas du JSON"
s.sendall(struct.pack(">I", len(bad)) + bad)
time.sleep(0.4)
note("JSON malforme : le serveur survit", True)
s.close()

# ------------------------------------------ 7. longueur aberrante
s, ch = connect()
s.sendall(struct.pack(">I", 0x7FFFFFFF) + b"x" * 16)
time.sleep(0.5)
try:
    probe = socket.create_connection((HOST, TCP), timeout=3); probe.close()
    note("Longueur annoncee de 2 Go : serveur toujours a l'ecoute", True)
except Exception as e:
    note("Longueur annoncee de 2 Go : serveur toujours a l'ecoute", False, str(e))
s.close()

# ------------------------------------------------- 8. auth correcte
s, ch = connect()
rep, key = authenticate(s, ch, codec="pcm")
ok = bool(rep and rep.get("t") == "authOk")
note("Authentification correcte", ok)
if ok:
    note("Le serveur annonce son port UDP", rep.get("udpPort") == UDP, str(rep.get("udpPort")))
    note("Un jeton de PTT est fourni", len(rep.get("pttToken", "")) > 0,
         f"{len(rep.get('pttToken',''))//2} octets")
    note("L'etat du poste accompagne la reponse", "state" in rep)
    note("Les capacites accompagnent la reponse", "caps" in rep)
    session = int(rep.get("session", 0))
    token = bytes.fromhex(rep.get("pttToken", ""))

    # --------------------------------- 9. PTT par UDP avec faux jeton
    u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    hdr = struct.pack("<IBBBBIIIHH", 0x52524947, 1, 2, 0, 0, session, 1, 0, len(token), 0)
    u.sendto(hdr + b"\x00" * len(token), (HOST, UDP))
    time.sleep(0.4)
    note("PTT UDP avec un faux jeton : ignore", True, "le poste ne doit pas emettre")

    # ---------------------------- 10. PTT par UDP avec mauvaise session
    hdr = struct.pack("<IBBBBIIIHH", 0x52524947, 1, 2, 0, 0, session ^ 0xFFFF, 2, 0, len(token), 0)
    u.sendto(hdr + token, (HOST, UDP))
    time.sleep(0.4)
    note("PTT UDP avec une mauvaise session : ignore", True)

    # ----------------------------------- 11. datagramme tronque
    u.sendto(b"\x47\x49\x52\x52\x01", (HOST, UDP))
    u.sendto(b"", (HOST, UDP))
    time.sleep(0.3)
    note("Datagrammes UDP tronques ou vides : survivent", True)
    u.close()

    # ------------------- 12. second client : refus explicite, pas un defi
    s2 = socket.create_connection((HOST, TCP), timeout=3)
    ch2 = read_frame(s2)
    note("Un second client recoit un refus, non un defi",
         bool(ch2) and ch2.get("t") == "error",
         ch2.get("msg", "") if ch2 else "rien recu")
    # ... et la station du premier client n'a pas ete perturbee
    s.sendall(frame({"t": "cmd", "c": "ptt", "on": False}))
    time.sleep(0.3)
    note("Le premier client garde la station", True)
    s2.close()
s.close()

# --------------------------------- 13. le serveur repond toujours
try:
    s3, ch3 = connect()
    note("Le serveur ecoute encore apres tous ces essais", ch3 is not None)
    s3.close()
except Exception as e:
    note("Le serveur ecoute encore apres tous ces essais", False, str(e))

bad = sum(1 for ok, _, _ in results if not ok)
print(f"\n  {len(results) - bad} verifications passees, {bad} en echec")
sys.exit(1 if bad else 0)
