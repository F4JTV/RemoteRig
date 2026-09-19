# -*- coding: utf-8 -*-
"""The rig imposes a mode: the published state must be the rig's, not the request.

Asks for modes that Hamlib's dummy rig does not keep, and checks that the state
sent to the client is what the rig reports. Against the dummy, every request
should come back as FM: the rig ignored it. A client showing the request instead
would be confidently wrong — which is what happens on a real rig below 10 MHz,
where automatic sideband selection overrides USB with LSB."""
import hashlib, hmac, json, socket, struct, time

HOST, TCP = "127.0.0.1", 17800
def frame(o):
    b=json.dumps(o).encode(); return struct.pack(">I",len(b))+b
def rf(s,t=4.0):
    s.settimeout(t); h=b""
    while len(h)<4:
        c=s.recv(4-len(h))
        if not c: return None
        h+=c
    n=struct.unpack(">I",h)[0]; b=b""
    while len(b)<n:
        c=s.recv(n-len(b))
        if not c: return None
        b+=c
    return json.loads(b.decode())

s=socket.create_connection((HOST,TCP),timeout=4); ch=rf(s)
k=hashlib.pbkdf2_hmac("sha256",b"secret",bytes.fromhex(ch["salt"]),60000,32)
cn=b"\x66"*16
m=hmac.new(k,bytes.fromhex(ch["nonce"])+cn,hashlib.sha256).digest()
s.sendall(frame({"t":"auth","nonce":cn.hex(),"mac":m.hex(),"encrypt":False,"codec":"pcm","bitrate":48000}))
rep=rf(s)
st=rep.get("s") or rep.get("state") or {}
print("  mode au depart :", st.get("mode"))

for want in ("USB", "CW", "LSB"):
    s.sendall(frame({"t":"cmd","c":"mode","v":want,"pb":0}))
    got=None
    deadline=time.time()+2.0
    s.settimeout(0.4)
    while time.time()<deadline:
        try: msg=rf(s,0.4)
        except Exception: continue
        if msg and msg.get("t")=="state":
            got=(msg.get("s") or {}).get("mode")
            break
    print(f"  demande {want:<4} -> etat publie {got}")
s.close()
