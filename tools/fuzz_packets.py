#!/usr/bin/env python3
"""Sweep de paquetes MCPE (protocolo 84) con valores borde.

Objetivo: encontrar el SIGSEGV que el cliente real dispara mandando
paquetes "normales" (hotbar switches, ContainerSetSlot, USE_ITEM...)
contra un binario compilado con AddressSanitizer.

Cada grupo manda el packet con valores fuera de rango (slots -1/255/
32767, windowids exoticos, coords en el limite de int32, NBT truncado,
UUIDs de receta aleatorios, item ids invalidos...). Un cliente real
nunca manda eso, pero un decoder que no valida es exactamente donde
aparece un SEGV, y ASAN lo reporta con symbolized frames.

    python3 tools/fuzz_packets.py            # sweep completo
    python3 tools/fuzz_packets.py 300        # mas iteraciones

Requiere un server escuchando en 127.0.0.1:19132 (idealmente el build
con ASAN: ASAN_OPTIONS=detect_leaks=0).
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
NAME = 'Fuzz' + str(int(time.time()) % 10000)
ITERS = int(sys.argv[1]) if len(sys.argv) > 1 else 120


def wb(b): return struct.pack('>B', b & 0xFF)
def ws(b): return struct.pack('>H', b & 0xFFFF)
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
s.settimeout(0.4)
s.sendto(b'\x05' + MAGIC + bytes([6]) + b'\x00' * (1492 - 18), (IP, PORT))
try:
    s.recvfrom(2048)
except socket.timeout:
    pass
cid = 0x5151
s.sendto(b'\x07' + MAGIC + addr(IP, PORT) + ws(1492) + wl(cid), (IP, PORT))
try:
    s.recvfrom(2048)
except socket.timeout:
    pass

seq = 0


def send(p, rel=3):
    global seq
    b = bytearray([(rel << 5) & 0xe0])
    b += ws((len(p) * 8) & 0xffff)
    if rel >= 2 and rel != 5:
        b += lt3(seq)
    if rel <= 3 and rel != 2:
        b += lt3(seq)
    b += bytes([0])
    b += p
    s.sendto(wb(0x84) + bytes([seq & 0xff, (seq >> 8) & 0xff, (seq >> 16) & 0xff])
             + bytes(b), (IP, PORT))
    seq += 1


def batch(pkt):
    inner = struct.pack('>I', len(pkt)) + pkt
    c = zlib.compress(inner)
    return wb(0x06) + struct.pack('>I', len(c)) + c


def drain(sec=0.3):
    t0 = time.time()
    while time.time() - t0 < sec:
        try:
            d, _ = s.recvfrom(4096)
        except socket.timeout:
            continue
        if not d or not (d[0] & 0x80):
            continue


# ---- login ----
ping = int(time.time() * 1000)
send(wb(0x09) + wl(cid) + wl(ping) + bytes([0]))
time.sleep(0.2)
send(wb(0x13) + addr(IP, PORT) + b'\x00' * 60 + wl(ping + 1) + wl(ping + 2))
time.sleep(0.2)
chain = json.dumps({"chain": [b64u({"alg": "ES384"}) + "." + b64u({
    "displayName": NAME,
    "identity": "11111111-2222-3333-4444-555555555555",
    "extraData": {}}) + ".sig"]}, separators=(',', ':')).encode()
cdata = (b64u({"alg": "ES384"}) + "." + b64u({
    "DisplayName": NAME, "Identity": "11111111-2222-3333-4444-555555555555",
    "DeviceModel": "Linux", "DeviceOS": 7, "LanguageCode": "en_US",
    "SkinId": "Standard_Custom"}) + ".sig").encode()
raw = struct.pack('<I', len(chain)) + chain + struct.pack('<I', len(cdata)) + cdata
comp = zlib.compress(raw)
body = struct.pack('>I', 84) + struct.pack('>I', len(comp)) + comp
send(batch(wb(0x01) + body))
drain(4)
print("login OK como", NAME)

PX, PY, PZ = 127.5, 128.0, 127.5
send(batch(wb(0x10) + wl(0) + wf(PX) + wf(PY) + wf(PZ)
           + wf(0.0) + wf(0.0) + wf(0.0) + wb(0) + wb(1)))
drain(2.0)


def move(x, y, z):
    send(batch(wb(0x10) + wl(0) + wf(x) + wf(y) + wf(z)
               + wf(0.0) + wf(0.0) + wf(0.0) + wb(0) + wb(1)))


def slot_bytes(item_id, count, damage=0, nbt=b''):
    return (ws(item_id) + wb(count) + ws(damage)
            + struct.pack('<H', len(nbt)) + nbt)


def set_slot(window, slot, item_id, count, damage=0):
    send(batch(wb(0x2c) + wb(window) + ws(slot) + ws(slot)
               + slot_bytes(item_id, count, damage)))


def use_item(x, y, z, face, item_id, count=1, damage=0):
    send(batch(wb(0x1f) + wi(x) + wi(y) + wi(z) + wb(face)
               + wf(.5) + wf(.5) + wf(.5) + wf(0.0) + wf(0.0) + wf(0.0)
               + wi(0) + slot_bytes(item_id, count, damage)))


EDGE_X = [0, 1, -1, 15, 16, 127, -2048, 2047, 30000000, -30000000,
          2147483647, -2147483648]
EDGE_Y = [-2147483648, -1000, -64, -1, 0, 1, 15, 127, 255, 256, 320, 1000,
          2147483647, -2147483648]
EDGE_SLOT = [-32768, -1, 0, 5, 35, 36, 39, 40, 44, 45, 63, 255, 32767]
EDGE_WIN = [0, 1, 2, 3, 4, 5, 6, 0x78, 0x79, 0x7a, 0x7f, 0xff]
EDGE_ITEM = [0, 1, 5, 17, 54, 58, 255, 256, 259, 323, 325, 383, 511, 32767, 65535]
EDGE_FACE = [0, 1, 2, 3, 4, 5, 6, 7, 255]


def sweep_container_set_slot(i):
    w = EDGE_WIN[i % len(EDGE_WIN)]
    sl = EDGE_SLOT[(i * 5) % len(EDGE_SLOT)]
    it = EDGE_ITEM[(i * 3) % len(EDGE_ITEM)]
    cnt = [0, 1, 2, 64, 255][i % 5]
    dmg = [0, 1, 15, 255, 4096][i % 5]
    set_slot(w, sl, it, cnt, dmg)


def sweep_mob_equipment(i):
    a = i % 256
    b = (i * 7) % 256
    it = EDGE_ITEM[i % len(EDGE_ITEM)]
    send(batch(wb(0x1b) + wl(0) + slot_bytes(it, 1) + wb(a) + wb(b)))


def sweep_use_item(i):
    use_item(EDGE_X[i % len(EDGE_X)], EDGE_Y[(i * 3) % len(EDGE_Y)],
             EDGE_X[(i * 7) % len(EDGE_X)],
             EDGE_FACE[i % len(EDGE_FACE)],
             EDGE_ITEM[i % len(EDGE_ITEM)], [0, 1, 255][i % 3])


def sweep_player_action(i):
    send(batch(wb(0x20) + wb(i % 12)
               + wi(EDGE_X[(i * 3) % len(EDGE_X)])
               + wi(EDGE_Y[(i * 5) % len(EDGE_Y)])
               + wi(EDGE_X[(i * 11) % len(EDGE_X)])
               + wb(EDGE_FACE[i % len(EDGE_FACE)])))


def sweep_remove_block(i):
    send(batch(wb(0x12) + wi(EDGE_X[i % len(EDGE_X)])
               + wi(EDGE_Y[(i * 3) % len(EDGE_Y)])
               + wi(EDGE_X[(i * 7) % len(EDGE_X)])))


def sweep_interact(i):
    send(batch(wb(0x1e) + wl(0) + wb(i % 4)
               + wi(EDGE_X[i % len(EDGE_X)])
               + wi(EDGE_Y[(i * 3) % len(EDGE_Y)])
               + wi(EDGE_X[(i * 7) % len(EDGE_X)])
               + wb(EDGE_FACE[i % len(EDGE_FACE)])
               + wi(EDGE_ITEM[i % len(EDGE_ITEM)])
               + wi(0) + wi(0) + wi(0)))


def sweep_container_close(i):
    send(batch(wb(0x2b) + wb(EDGE_WIN[i % len(EDGE_WIN)])))


def sweep_container_set_content(i):
    n = [0, 1, 9, 36, 45, 255, 4096][i % 7]
    body = wb(EDGE_WIN[i % len(EDGE_WIN)]) + ws(n)
    for k in range(min(n, 64)):
        body += slot_bytes(EDGE_ITEM[(i + k) % len(EDGE_ITEM)], 1)
    send(batch(wb(0x2e) + body))


def _nbt_string(v):
    b = v.encode()
    return struct.pack('<H', len(b)) + b


def sweep_block_entity(i):
    """BlockEntityData con NBT de Sign: completo, truncado y basura."""
    x = EDGE_X[i % len(EDGE_X)]
    y = EDGE_Y[(i * 3) % len(EDGE_Y)]
    z = EDGE_X[(i * 7) % len(EDGE_X)]
    good = (b'\x0a\x00\x00' + _nbt_string("Sign")
            + _nbt_string("Text1") + struct.pack('<B', 8) + b'HOLA\x00\x00'
            + _nbt_string("Text2") + struct.pack('<B', 8) + b'\x00\x00\x00\x00'
            + _nbt_string("Text3") + struct.pack('<B', 8) + b'\x00\x00\x00\x00'
            + _nbt_string("Text4") + struct.pack('<B', 8) + b'\x00\x00\x00\x00'
            + b'\x00')
    variants = [good, good[:1], good[:5], good[:9], good[:20], good[:len(good) // 2],
                b'\x0a\x00\x00', b'\xff' * 12, good + b'\xff' * 4,
                struct.pack('<H', 65535) + b'Z' * 200 + b'\x00']
    nbt = variants[i % len(variants)]
    send(batch(wb(0x32) + wi(x) + wi(y) + wi(z) + nbt))


def sweep_crafting_event(i):
    win = EDGE_WIN[i % len(EDGE_WIN)]
    uuid = bytes([(i * 13 + k * 31) & 0xFF for k in range(16)])
    ni = [0, 1, 2, 9, 128, 129, 4096][i % 7]
    no = [0, 1, 128, 4096][i % 4]
    body = wb(win) + wi(0) + uuid + wi(ni)
    for k in range(min(ni, 128)):
        body += slot_bytes(EDGE_ITEM[(i + k) % len(EDGE_ITEM)], 1)
    body += wi(no)
    for k in range(min(no, 128)):
        body += slot_bytes(EDGE_ITEM[(i + k) % len(EDGE_ITEM)], 1)
    send(batch(wb(0x30) + body))


def sweep_animate(i):
    send(batch(wb(0x27) + wb(i % 4) + wl(0)))


def sweep_move(i):
    """Coordenadas absurdas (NaN/inf/out-of-world) en MovePlayer."""
    vals = [float('nan'), float('inf'), -float('inf'), 1e30, -1e30,
            3.4e38, -3.4e38, 0.0, -0.0, 128.0, 1e9]
    v = vals[i % len(vals)]
    send(batch(wb(0x10) + wl(0) + wf(v) + wf(vals[(i * 3) % len(vals)])
               + wf(vals[(i * 7) % len(vals)]) + wf(0.0) + wf(0.0) + wb(0) + wb(1)))


def sweep_drop_item(i):
    send(batch(wb(0x29) + wl(0) + slot_bytes(EDGE_ITEM[i % len(EDGE_ITEM)], 255)))


GROUPS = [
    sweep_container_set_slot,
    sweep_mob_equipment,
    sweep_use_item,
    sweep_player_action,
    sweep_remove_block,
    sweep_interact,
    sweep_container_close,
    sweep_container_set_content,
    sweep_block_entity,
    sweep_crafting_event,
    sweep_animate,
    sweep_move,
    sweep_drop_item,
]

# Posicion estable en el spawn (todo el sweep va alrededor de ahi).
move(127.5, 128.0, 127.5)
drain(1.0)

for i in range(ITERS):
    g = GROUPS[i % len(GROUPS)]
    try:
        g(i)
    except (struct.error, OverflowError, ValueError) as e:
        print("  [skip] %s iter %d: %s" % (g.__name__, i, e))
    if i % 10 == 0:
        drain(0.25)
        move(127.5, 128.0, 127.5)   # el anti-cheat re-setea si nos movimos
    else:
        drain(0.02)

# ---- gestion normal: colocar/quitar bloques, cofre, crafteo, hotbar ----
print("fase de gestion normal (place/break/chest/craft)")
for k in range(30):
    use_item(128 + (k % 5), 128, 128 + (k % 5), 1, 17, 1)
    set_slot(0, 5, 17, 1)
    send(batch(wb(0x1b) + wl(0) + slot_bytes(17, 1) + wb(k % 9) + wb(k % 9)))
    send(batch(wb(0x12) + wi(128 + (k % 5)) + wi(128) + wi(128 + (k % 5))))
    drain(0.08)
drain(1.0)
print("sweep completo: %d iteraciones sobre %d grupos" % (ITERS, len(GROUPS)))
