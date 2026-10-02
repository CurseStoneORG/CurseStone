#!/usr/bin/env python3
"""Reproduce la secuencia del log que precedió al SIGSEGV:

  - entrar al mundo
  - abrir mesa de crafteo
  - colocar una tanda de bloques (id=17, LOG)
  - mandar ContainerSetSlot con el item decrementado
  - cambiar de slot de hotbar (0,5,6,5) repetidamente

Se usa contra el build con ASAN para obtener symbolized frames.
"""
import base64
import json
import socket
import struct
import sys
import time
import zlib

MAGIC = b'\x00\xff\xff\x00\xfe\xfe\xfe\xfe\xfd\xfd\xfd\xfd\x12\x34\x56\x78'
IP, PORT = '127.0.0.1', 19132
NAME = sys.argv[1] if len(sys.argv) > 1 else 'Dr1xy'


def wb(b): return struct.pack('>B', b)
def ws(b): return struct.pack('>H', b)
def wi(b): return struct.pack('>i', b)
def wl(b): return struct.pack('>Q', b)
def wf(f): return struct.pack('>f', f)
def lt3(b): return struct.pack('<I', b)[:3]
def addr(ip, port):
    p = [int(x) for x in ip.split('.')]
    return bytes([4]) + bytes([255 - p[0], 255 - p[1], 255 - p[2], 255 - p[3]]) + ws(port)


def b64u(o):
    return base64.urlsafe_b64encode(
        json.dumps(o, separators=(',', ':')).encode()).decode().rstrip('=')


s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.settimeout(0.5)
s.sendto(b'\x05' + MAGIC + bytes([6]) + b'\x00' * (1492 - 18), (IP, PORT))
s.recvfrom(2048)
cid = 0x4242
s.sendto(b'\x07' + MAGIC + addr(IP, PORT) + ws(1492) + wl(cid), (IP, PORT))
s.recvfrom(2048)

seq = 0


def encap(p, rel=3):
    b = bytearray([(rel << 5) & 0xe0])
    b += ws((len(p) * 8) & 0xffff)          # longitud en BITS
    if rel >= 2 and rel != 5: b += lt3(seq)
    if rel <= 3 and rel != 2: b += lt3(seq)
    b += bytes([0])
    b += p
    return bytes(b)


def send(p, rel=3):
    global seq
    s.sendto(wb(0x84) + bytes([seq & 0xff, (seq >> 8) & 0xff, (seq >> 16) & 0xff])
             + encap(p, rel), (IP, PORT))
    seq += 1


def batch(pkt):
    inner = struct.pack('>I', len(pkt)) + pkt
    c = zlib.compress(inner)
    return wb(0x06) + struct.pack('>I', len(c)) + c


def drain(sec=1.0):
    got = []
    t0 = time.time()
    while time.time() - t0 < sec:
        try:
            d, _ = s.recvfrom(4096)
        except socket.timeout:
            continue
        if not (d[0] & 0x80):
            continue
        i = 4
        while i < len(d):
            flags = d[i]
            ln = struct.unpack('>H', d[i + 1:i + 3])[0] // 8
            hdr = 4
            if ((flags & 0xe0) >> 5) >= 2: hdr += 3
            if ((flags & 0xe0) >> 5) <= 3: hdr += 4
            got.append(d[i + hdr:i + hdr + ln])
            i += hdr + ln
    return got


# ---- login (protocolo de juego 84, cuerpo legacy) ----
ping = int(time.time() * 1000)
send(wb(0x09) + wl(cid) + wl(ping) + bytes([0]))
time.sleep(0.3)
send(wb(0x13) + addr(IP, PORT) + b'\x00' * 60 + wl(ping + 1) + wl(ping + 2))
time.sleep(0.3)
chain = json.dumps({"chain": [b64u({"alg": "ES384"}) + "." + b64u({
    "displayName": NAME,
    "identity": "11111111-2222-3333-4444-555555555555",
    "extraData": {}}) + ".sig"]}, separators=(',', ':')).encode()
cdata = (b64u({"alg": "ES384"}) + "." + b64u({
    "DisplayName": NAME, "Identity": "11111111-2222-3333-4444-555555555555",
    "DeviceModel": "Linux", "DeviceOS": 7, "LanguageCode": "en_US",
    "SkinId": "Standard_Custom"}) + ".sig").encode()
raw = struct.pack('<I', len(chain)) + chain + struct.pack('<I', len(cdata)) + cdata
body = struct.pack('>I', 84) + struct.pack('>I', len(zlib.compress(raw))) + zlib.compress(raw)
send(batch(wb(0x01) + body))
drain(5)
print("[1] login OK")

# ---- moverse cerca del spawn para que el chunk quede residente ----
send(batch(wb(0x10) + wl(0) + wf(127.5) + wf(128.0) + wf(127.5)
           + wf(0.0) + wf(0.0) + wf(0.0) + wb(0) + wb(1)))
time.sleep(2)
drain(2)


def use_item(x, y, z, face, item_id, count=1):
    slot = ws(item_id) + wb(count) + ws(0) + struct.pack('<H', 0)
    send(batch(wb(0x1f) + wi(x) + wi(y) + wi(z) + wb(face)
               + wf(.5) + wf(.5) + wf(.5) + wf(float(x)) + wf(float(y)) + wf(float(z))
               + wi(0) + slot))


def mob_equipment(slot, selected, item_id=17, count=1):
    """MOB_EQUIPMENT (0x1b): eid i64 | Slot | byte hotbarSlot | byte selectedSlot"""
    sl = ws(item_id) + wb(count) + ws(0) + struct.pack('<H', 0)
    send(batch(wb(0x1b) + wl(0) + sl + wb(slot) + wb(selected)))


def set_slot(window, slot, item_id, count):
    # 0x2c = CONTAINER_SET_SLOT_PACKET (Info.h). Ojo: 0x2e es
    # CONTAINER_SET_CONTENT, no el set-slot.
    s2 = ws(item_id) + wb(count) + ws(0) + struct.pack('<H', 0)
    send(batch(wb(0x2c) + wb(window) + ws(slot) + ws(slot) + s2))


# ---- andar en pasos (anti-cheat: max 20 bloques por tick) cargando
# ---- chunks, y colocar bloques por el camino como en el log ----
count = [20]


def step_to(x, y, z):
    """Mueve al jugador en pasos de <=20 bloques (limite anti-cheat)."""
    px, py, pz = 127.5, 128.0, 127.5
    while True:
        dx, dy, dz = x - px, y - py, z - pz
        d = (dx * dx + dy * dy + dz * dz) ** 0.5
        if d < 1.0:
            break
        k = min(1.0, 18.0 / d)
        px += dx * k; py += dy * k; pz += dz * k
        send(batch(wb(0x10) + wl(0) + wf(px) + wf(py) + wf(pz)
                   + wf(0.0) + wf(0.0) + wf(0.0) + wb(0) + wb(1)))
        time.sleep(0.30)
        drain(0.15)
    # Asentar: el servidor recien stream-ea los chunks en onUpdate.
    for _ in range(8):
        time.sleep(0.35)
        drain(0.20)
    return px, py, pz


print("[2] caminando hacia 163,73,93 (carga chunks)")
step_to(163.0, 73.0, 93.0)
drain(1.5)

# ---- mesa de crafteo ----
use_item(160, 74, 93, 2, 0)
time.sleep(0.5); drain(0.5)
print("[3] crafting table click")

# ---- tanda de colocaciones + ContainerSetSlot + hotbar, igual que el log ----
coords = [(166, 73, 97), (166, 73, 95), (165, 73, 95), (163, 73, 95),
          (164, 73, 93), (163, 73, 94), (163, 73, 93), (163, 73, 92),
          (163, 73, 91), (163, 73, 90)]
placed = 0
for (x, y, z) in coords:
    use_item(x, y, z, 1, 17, count[0])
    time.sleep(0.5); drain(0.4)
    count[0] -= 1
    set_slot(0, 5, 17, count[0])
    mob_equipment(5, 5)
    time.sleep(0.2); drain(0.3)
    placed += 1
print("[4] %d/use_item pedidos de colocacion, hotbar=5" % placed)

# ---- el patron final del log: 0 -> 6 -> 5 ----
for target in (0, 5, 6, 5, 0, 5, 6, 5):
    mob_equipment(target, target)
    time.sleep(0.4); drain(0.3)
print("[5] ciclo de hotbar 0/5/6/5 hecho")

# ---- tiles: cartel + cofre, y luego alejarse para forzar la eviccion
# ---- del chunk (y con ella el unload de las tiles del chunk) ----
use_item(162, 73, 92, 1, 323, 1)     # SIGN
time.sleep(0.4); drain(0.3)
use_item(162, 73, 90, 1, 54, 1)      # CHEST
time.sleep(0.4); drain(0.3)
print("[6] cartel + cofre colocados")

print("[7] alejandose para forzar unload de chunks (GC)")
step_to(400.0, 73.0, 400.0)
drain(2.0)
print("[8] volviendo (los tiles deberian rehidratarse)")
step_to(163.0, 73.0, 93.0)
drain(2.0)

# ---- cofre: abrir y cerrar (ContainerClose 0x2b) ----
use_item(162, 73, 90, 2, 0)
time.sleep(0.5); drain(0.5)
send(batch(wb(0x2b) + wb(1)))
time.sleep(0.5); drain(0.5)
print("[9] cofre abierto y cerrado")

# ---- mas colocaciones sobre el area recien rehidratada ----
for i in range(14):
    use_item(161 + (i % 6), 73, 88 + i, 1, 17, 1)
    time.sleep(0.2)
    mob_equipment(i % 9, i % 9)
    drain(0.2)
print("[10] done")
