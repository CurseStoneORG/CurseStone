#!/usr/bin/env python3
"""End-to-end login check for a local CurseStone server (Linux).

Talks real RakNet (PocketMine-MP / RakLib, MCPE 0.15.10, protocol 84) over
UDP instead of spraying raw datagrams, so it exercises the exact path a
real client takes:

    UNCONNECTED_PING -> OPEN_CONNECTION_REQUEST_1 -> OPEN_CONNECTION_REQUEST_2
    -> CLIENT_CONNECT (0x09) -> SERVER_HANDSHAKE (0x10)
    -> CLIENT_HANDSHAKE (0x13) -> [RakLib Session opened]
    -> LoginPacket (0x01) -> PlayStatus (0x02)

Usage:
    python3 tools/linux_login_check.py [--host 127.0.0.1] [--port 19132] [--name LinuxTester]

Exit code 0 means the server opened a session AND answered the login; any
other code pinpoints the first stage that failed. Use it to reproduce
platform-specific login bugs (the historic "Linux: no session created"
report).
"""
import argparse
import base64
import hashlib
import json
import socket
import struct
import sys
import time
import zlib

MAGIC = b"\x00\xff\xff\x00\xfe\xfe\xfe\xfe\xfd\xfd\xfd\xfd\x12\x34\x56\x78"

# RakLib packet ids (PMMP/RakLib numbering, NOT generic RakNet).
UNCONNECTED_PING = 0x01
UNCONNECTED_PONG = 0x1C
OCR1 = 0x05
OCR1_REPLY = 0x06
OCR2 = 0x07
OCR2_REPLY = 0x08
CLIENT_CONNECT = 0x09
SERVER_HANDSHAKE = 0x10
CLIENT_HANDSHAKE = 0x13
ACK = 0xC0
NACK = 0xA0
DATA_FIRST = 0x80
DATA_LAST = 0x8F
BATCH_PACKET_ID = 0x06

# Game-layer ids.
LOGIN = 0x01
PLAY_STATUS = 0x02
BATCH = 0x06
START_GAME = 0x09
FULL_CHUNK_DATA = 0x34

STAGE_PING = "unconnected_ping"
STAGE_OCR1 = "open_connection_request_1"
STAGE_OCR2 = "open_connection_request_2"
STAGE_CONNECT = "client_connect"
STAGE_SESSION = "session_opened"
STAGE_LOGIN = "login_answered"
STAGE_SPAWN = "start_game"
STAGE_CHUNKS = "chunks_streamed"

ALL_STAGES = [STAGE_PING, STAGE_OCR1, STAGE_OCR2, STAGE_CONNECT,
               STAGE_SESSION, STAGE_LOGIN, STAGE_SPAWN, STAGE_CHUNKS]


def b64u(obj) -> str:
    raw = json.dumps(obj, separators=(",", ":")).encode()
    return base64.urlsafe_b64encode(raw).decode().rstrip("=")


def addr(host: str, port: int) -> bytes:
    # RakNet address: [version=4][ip0..ip3][port BE]
    return b"\x04" + socket.inet_aton(host) + struct.pack(">H", port)


class Client:
    def __init__(self, host: str, port: int, name: str):
        self.host = host
        self.port = port
        self.name = name
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        # The server streams recipes + chunks as soon as we log in; a
        # default Linux socket buffer drops most of them, which breaks
        # split reassembly. Ask for a bigger one.
        try:
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 << 20)
        except OSError:
            pass
        self.sock.settimeout(0.4)
        # Unique per instance: two harness clients in the same second must
        # not collide (the server kicks duplicate session ids).
        self.client_id = (time.time_ns() ^ (id(self) * 0x9E3779B1)) & 0xFFFFFFFF
        self.send_seq = 0
        self.message_index = 0
        self.order_index = [0] * 32   # per-channel ordering index
        self.reached = set()
        self.recv_ids = []
        self.fragments = 0
        self.splits = {}
        self.acked = set()
        self.client_ip = "127.0.0.1"
        self.client_port = 0  # filled in once we see a reply

    # ---- send helpers ------------------------------------------------
    def datagram(self, payload: bytes) -> bytes:
        return payload

    def send_raw(self, payload: bytes):
        self.sock.sendto(payload, (self.host, self.port))

    def send_encapsulated(self, inner: bytes, reliability: int = 2,
                          order_channel: int = 0):
        """Build a single-fragment DATA_PACKET (0x84) around `inner`.

        reliability 2 = RELIABLE (carries messageIndex only), which is all
        this server's decoder requires for the handshake packets.
        """
        seq = self.send_seq
        self.send_seq += 1
        flags = (reliability & 0x07) << 5
        bit_len = len(inner) * 8
        rec = bytes([flags, (bit_len >> 8) & 0xFF, bit_len & 0xFF])
        if reliability in (2, 3, 4, 6, 7):
            mi = self.message_index
            self.message_index += 1
            rec += struct.pack("<I", mi)[:3]
        if reliability in (3, 7):  # ordered
            # Each ordered packet must advance the channel's order index,
            # otherwise the peer holds it forever waiting for the gap.
            ch = order_channel & 0x1F
            oi = self.order_index[ch]
            self.order_index[ch] = oi + 1
            rec += struct.pack("<I", oi)[:3] + bytes([ch])
        return bytes([0x84, seq & 0xFF, (seq >> 8) & 0xFF,
                      (seq >> 16) & 0xFF]) + rec + inner

    # ---- receive helpers ---------------------------------------------
    def recv_one(self, budget: float = 0.4):
        try:
            data, _ = self.sock.recvfrom(4096)
        except socket.timeout:
            return None
        return data

    @staticmethod
    def parse_datagram(data: bytes):
        """Yield (payload, has_split, split_count, split_id, split_index).

        Split FRAGMENTS are reported separately so the caller can
        reassemble them instead of mistaking a fragment (whose first byte
        is arbitrary payload, e.g. terrain data) for a game packet id.
        """
        out = []
        if not data:
            return out
        pid = data[0]
        if pid < DATA_FIRST or pid > DATA_LAST or len(data) < 4:
            return out
        i = 4  # type + 3-byte LE seq
        while i < len(data):
            if i + 3 > len(data):
                break
            flags = data[i]
            reliability = (flags >> 5) & 0x07
            has_split = bool(flags & 0x10)
            bit_len = (data[i + 1] << 8) | data[i + 2]
            i += 3
            plen = (bit_len + 7) // 8
            if reliability in (2, 3, 4, 6, 7):
                i += 3
            if reliability in (1, 4):
                i += 3
            if reliability in (3, 7):
                i += 4
            split_count = split_id = split_index = 0
            if has_split:
                if i + 10 > len(data):
                    break
                split_count = int.from_bytes(data[i:i + 4], "big")
                split_id = int.from_bytes(data[i + 4:i + 6], "big")
                split_index = int.from_bytes(data[i + 6:i + 10], "big")
                i += 10
            if i + plen > len(data):
                break
            out.append((data[i:i + plen], has_split,
                        split_count, split_id, split_index))
            i += plen
        return out

    def add_fragment(self, payload: bytes, count: int, sid: int, idx: int):
        """Reassemble a split message; return the whole payload when done.

        RakNet splits large encapsulated messages (the recipe list and the
        first chunks easily exceed the MTU), so a real client must stitch
        the fragments back together before it can read the packet id.
        """
        slot = self.splits.setdefault(sid, {"count": count, "parts": {}})
        slot["parts"][idx] = payload
        self.fragments += 1
        if count <= 0 or len(slot["parts"]) < count:
            return None
        if any(k not in slot["parts"] for k in range(count)):
            return None
        joined = b"".join(slot["parts"][k] for k in range(count))
        del self.splits[sid]
        return joined

    def handle_payload(self, payload: bytes):
        """Record one fully reassembled game packet, unwrapping the batch.

        The server wraps every game packet as
            [0xFE][0x06][u32be compressedLen][zlib([u32be subLen][sub])]
        (see Player::sendPacket), so a real client must strip the batch
        before it can even read the packet id.
        """
        if not payload:
            return
        if payload[0] == 0xFE and len(payload) > 5:
            body = payload[1:]
            if body[0] == BATCH_PACKET_ID and len(body) > 5:
                clen = int.from_bytes(body[1:5], "big")
                comp = body[5:5 + clen]
                try:
                    raw = zlib.decompress(comp)
                except zlib.error:
                    return
                if len(raw) < 5:
                    return
                sublen = int.from_bytes(raw[0:4], "big")
                payload = raw[4:4 + sublen]
                if not payload:
                    return
        gp = payload[0]
        self.recv_ids.append(gp)
        if gp == SERVER_HANDSHAKE:
            self.reached.add(STAGE_CONNECT)
        elif gp == PLAY_STATUS:
            self.reached.add(STAGE_LOGIN)
        elif gp == START_GAME:
            self.reached.add(STAGE_SPAWN)
        elif gp == FULL_CHUNK_DATA:
            self.reached.add(STAGE_CHUNKS)

    def send_ack(self, seq: int):
        """Acknowledge one reliable datagram (RakNet single-record ACK).

        A real client ACKs everything it receives; without this the server
        keeps every fragment in its recovery queue and retransmits, which
        floods the socket buffer so badly that fragments are lost and a
        split message can never be completed.
        """
        self.send_raw(bytes([ACK]) + struct.pack(">H", 1) + b"\x01" +
                      bytes([seq & 0xFF, (seq >> 8) & 0xFF, (seq >> 16) & 0xFF]))

    def drain(self, seconds: float):
        """Receive for `seconds`, recording what the server said."""
        deadline = time.time() + seconds
        while time.time() < deadline:
            data = self.recv_one()
            if data is None:
                continue
            if data[0] in (ACK, NACK):
                continue
            if data[0] == UNCONNECTED_PONG:
                self.reached.add(STAGE_PING)
                continue
            if data[0] == OCR1_REPLY:
                self.reached.add(STAGE_OCR1)
                continue
            if data[0] == OCR2_REPLY:
                self.reached.add(STAGE_OCR2)
                continue
            if DATA_FIRST <= data[0] <= DATA_LAST:
                rel = (data[4] >> 5) & 0x07
                if rel in (2, 3, 4, 6, 7):
                    self.send_ack(data[1] | (data[2] << 8) | (data[3] << 16))
            for payload, is_split, count, sid, idx in self.parse_datagram(data):
                if not payload:
                    continue
                if is_split:
                    whole = self.add_fragment(payload, count, sid, idx)
                    if whole:
                        self.handle_payload(whole)
                    continue
                self.handle_payload(payload)

    def expect(self, stage: str, seconds: float = 2.0) -> bool:
        deadline = time.time() + seconds
        while stage not in self.reached and time.time() < deadline:
            self.drain(min(0.3, max(0.05, deadline - time.time())))
        return stage in self.reached

    # ---- handshake -----------------------------------------------------
    def handshake(self) -> str:
        # 1. server-list ping
        ping = bytes([UNCONNECTED_PING]) + struct.pack("<Q", int(time.time() * 1000)) + MAGIC
        self.send_raw(ping)
        if not self.expect(STAGE_PING):
            return STAGE_PING

        # 2. OPEN_CONNECTION_REQUEST_1 (padding probes the MTU)
        ocr1 = bytes([OCR1]) + MAGIC + b"\x06" + b"\x00" * 1400
        self.send_raw(ocr1)
        if not self.expect(STAGE_OCR1):
            return STAGE_OCR1

        # 3. OPEN_CONNECTION_REQUEST_2
        ocr2 = (bytes([OCR2]) + MAGIC + addr(self.host, self.port) +
                struct.pack(">H", 1464) + struct.pack(">Q", self.client_id))
        self.send_raw(ocr2)
        if not self.expect(STAGE_OCR2):
            return STAGE_OCR2

        # 4. CLIENT_CONNECT (0x09) — encapsulated, RELIABLE
        cc = bytes([CLIENT_CONNECT]) + struct.pack(">Q", self.client_id) + \
             struct.pack(">Q", int(time.time() * 1000)) + b"\x00"
        self.send_raw(self.send_encapsulated(cc, reliability=3))
        if not self.expect(STAGE_CONNECT):
            return STAGE_CONNECT

        # 5. CLIENT_HANDSHAKE (0x13) — opens the game-layer session
        ch = bytes([CLIENT_HANDSHAKE]) + addr(self.host, self.port) + struct.pack(">H", 0) + \
             struct.pack(">H", 1464) + struct.pack(">Q", self.client_id)
        self.send_raw(self.send_encapsulated(ch, reliability=3))
        # The session-open is only reported by the SERVER (we detect it
        # through its log; on the wire we just proceed). Give it a moment
        # by checking the server kept talking to us.
        time.sleep(0.5)
        self.drain(1.0)
        return ""

    def login(self) -> str:
        # A real client sends a 3-segment JWT (header.payload.signature);
        # LoginPacket::decode splits on '.' twice, so a single blob is
        # silently ignored and the username ends up empty -> kicked with
        # "disconnectionScreen.invalidName".
        # Identity is derived from the name: two harness clients must not
        # share one UUID (the server kicks duplicate-uuid sessions).
        ident_tail = hashlib.md5(self.name.encode()).hexdigest()[:12]
        identity = f"11111111-2222-3333-4444-{ident_tail}"
        chain_jwt = (b64u({"alg": "ES384"}) + "." +
                     b64u({"displayName": self.name,
                           "identity": identity,
                           "extraData": {}}) + ".sig")
        chain = json.dumps({"chain": [chain_jwt]}, separators=(",", ":")).encode()
        cdata_jwt = (b64u({"alg": "ES384"}) + "." +
                     b64u({"DisplayName": self.name,
                           "Identity": identity,
                           "DeviceModel": "Linux",
                           "DeviceOS": 7,
                           "LanguageCode": "en_US",
                           "SkinId": "Standard_Custom"}) + ".sig")
        raw = struct.pack("<I", len(chain)) + chain + struct.pack("<I", len(cdata_jwt)) + cdata_jwt.encode()
        comp = zlib.compress(raw)
        body = struct.pack(">I", 84) + struct.pack(">I", len(comp)) + comp
        self.send_raw(self.send_encapsulated(bytes([LOGIN]) + body, reliability=3))
        if not self.expect(STAGE_LOGIN, seconds=3.0):
            return STAGE_LOGIN
        return ""


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone RakNet login check")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19132)
    ap.add_argument("--name", default="LinuxTester")
    args = ap.parse_args()

    c = Client(args.host, args.port, args.name)
    failed = c.handshake()
    if failed:
        print(f"FAIL: never completed stage '{failed}'")
        print(f"stages reached: {sorted(c.reached)}")
        return 1
    failed = c.login()

    # Keep listening long enough to see the spawn + chunk streaming, which
    # is what a real client needs before it can actually play.
    if not failed:
        c.expect(STAGE_SPAWN, seconds=5.0)
        c.expect(STAGE_CHUNKS, seconds=5.0)

    print(f"stages reached  : {sorted(c.reached)}")
    print(f"game packet ids : {sorted(set(c.recv_ids))}")
    print(f"split fragments : {c.fragments}")
    if failed:
        print(f"FAIL: handshake ok but login not answered (stage '{failed}')")
        return 2
    if STAGE_SPAWN not in c.reached:
        print("FAIL: logged in but never received StartGame (no spawn)")
        return 3
    print("OK: handshake + login + spawn")
    return 0


if __name__ == "__main__":
    sys.exit(main())
