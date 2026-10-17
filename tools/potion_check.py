#!/usr/bin/env python3
"""End-to-end potion (drink + splash + effects) check for CurseStone.

Needs ops.txt entries for PotionOne, PotionTwo, and PotionVictim (the
melee scenarios need /gamemode on the attacker and a real second
client). One or two real protocol-84 clients drive every path over the
wire. Health scenarios drain food below the 18-regen floor (no natural
regen can perturb them) but keep it above the 0-starvation floor.

drinkable potions (id 373):
  0. deterministic start: /kill + respawn (20 hp, clean effects)
  1. water bottle (meta 0) is NOT drinkable: no effect packet, no
     bottle residue, inventory untouched
  2. swiftness (meta 14): MobEffect ADD speed amp 0 dur 3600,
     movementSpeed attribute 0.1 * 1.2, bottle residue appears
  3. drink cooldown: a second drink within 1.6 s does nothing
  4. slowness after cooldown: ADD slowness; speed still combined
     (movementSpeed = 0.1 * 1.2 * 0.85)
splash potion (id 438) — thrown BEFORE the sprint drain so the bottle
breaks on the known-flat spawn column inside the 6-block catch radius:
  5. thrown down: AddEntityPacket type 86, RemoveEntityPacket, and the
     thrower catches swiftness II through the area (effect upgraded to
     amp 1, movementSpeed = 0.1 * 1.4 * 0.85)

drinkable potions continue (food drained below the regen floor first):
  6. harming (meta 23): -6 hp instantly (magic damage, relative assert)
  7. healing (meta 21): +4 hp instantly (relative assert)
  8. poison (meta 25): ADD packet + a -1 hp tick (1.25 s interval)
  9. regeneration II (meta 30): ADD packet; the health gain is asserted
     on the victim, where poison cannot perturb the math

persistence:
 10. a fresh drinkable Speed II refreshes an amp-1 effect, then the
     reconnect restores poison + speed II from the .dat and the server
     re-sends MobEffect ADD at first spawn

death:
 11. /kill clears every effect: no poison ADD after respawn and the
     health bar stays stable afterwards

melee modifiers (two clients):
 12. strength (meta 31): a punch deals 1 + 3 = 4 (20 -> 16)
 13. weakness (meta 34): with strength still up the punch deals
     max(1 + 3 - 4, 0) = 0 (health holds)

Usage:
    python3 tools/potion_check.py [--host 127.0.0.1] [--port 19132]
"""
import argparse
import struct
import sys
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from fall_damage_check import FallClient  # noqa: E402

TEXT_PACKET = 0x07
ADD_PLAYER = 0x0A
ADD_ENTITY = 0x0B
REMOVE_ENTITY = 0x0C
START_GAME = 0x09
MOVE_PLAYER = 0x10
LEVEL_EVENT = 0x16
ENTITY_EVENT = 0x18
MOB_EFFECT = 0x19
UPDATE_ATTRIBUTES = 0x1A
MOB_EQUIPMENT = 0x1B
INTERACT = 0x1E
USE_ITEM = 0x1F
PLAYER_ACTION = 0x20
SET_ENTITY_DATA = 0x22
CONTAINER_SET_SLOT = 0x2C
CONTAINER_SET_CONTENT = 0x2E
META_IDX_POTION_ID = 16   # DATA_POTION_ID (short) on the thrown bottle
META_IDX_POTION_COLOR = 7 # DATA_POTION_COLOR (int RGB tint)
META_IDX_POTION_AMBIENT = 8
ACTION_START_SPRINT = 9
EYE = 1.62

POTION = 373
GLASS_BOTTLE = 374
SPLASH_POTION = 438
APPLE = 260

# Potion data values used by the scenarios.
META_WATER = 0
META_SWIFTNESS = 14
META_SWIFTNESS_TWO = 16
META_SLOWNESS = 17
META_HEALING = 21
META_HARMING = 23
META_POISON = 25
META_REGENERATION_TWO = 30
META_STRENGTH = 31
META_WEAKNESS = 34


def bestr(s: str) -> bytes:
    raw = s.encode()
    return struct.pack(">H", len(raw)) + raw


def parse_metadata(payload: bytes, off: int):
    """Decodes a Binary::writeMetadata blob: {index: (type, value)}.

    Metadata VALUES are little-endian on protocol 84 (writeLShort /
    writeLInt / writeLFloat / writeLLong); strings carry an
    little-endian length. Parsing stops safely at the 0x7F terminator
    or at an unknown type.
    """
    out = {}
    while off < len(payload):
        key = payload[off]
        off += 1
        if key == 0x7F:
            return out, off
        t = key >> 5
        idx = key & 0x1F
        if t == 0:
            out[idx] = (t, payload[off])
            off += 1
        elif t in (1, 2, 3):
            n = 2 if t == 1 else 4
            out[idx] = (t, int.from_bytes(payload[off:off + n], "little"))
            off += n
        elif t == 4:
            n = int.from_bytes(payload[off:off + 2], "little")
            out[idx] = (t, payload[off + 2:off + 2 + n])
            off += 2 + n
        elif t == 6:
            off += 12
        elif t == 8:
            out[idx] = (t, int.from_bytes(payload[off:off + 8], "little"))
            off += 8
        else:
            return out, off  # rotation/slot/...: not needed here
    return out, off


class PotionClient(FallClient):
    """FallClient + potion effect/attribute/inventory observers."""

    def __init__(self, host, port, name):
        super().__init__(host, port, name)
        self.attrs = {}          # latest attribute values by name
        self.effect_pkts = []    # (eventId, effectId, amplifier, duration)
        self.entity86 = 0        # AddEntityPacket type-86 count
        self.entity86_eid = None
        self.removed = []        # RemoveEntityPacket eids
        self.victim_eid = None   # other-player eid from ADD_PLAYER
        self.other_eids = {}     # username -> eid (ADD_PLAYER announce)
        self.eat_echoes = 0
        self.inv_counts = {}     # item id -> units (window-0 pushes)
        self.eid = None          # own runtime id (START_GAME)
        self.effect_eids = []    # eid field of every MobEffectPacket (1:1 with effect_pkts)
        self.attr_eids = []      # eid field of every UpdateAttributesPacket
        self.eat_echo_eids = []  # eid of every eat-completion echo (event 9)
        self.potion_meta = None  # DATA_POTION_ID seen in the type-86 ADD metadata
        self.color_meta = {}     # eid -> latest (color, ambient) tint metadata
        self.splash_events = []  # (x, y, z, data) of every shatter event (evid 2002)

    def classify(self, payload: bytes):
        if payload and payload[0] == START_GAME and len(payload) >= 22:
            # [pid][seed i32][dim u8][gen i32][gamemode i32][eid i64BE]
            self.eid = struct.unpack(">q", payload[14:22])[0]
        if payload and payload[0] == MOB_EFFECT and len(payload) >= 17:
            # eid[1:9] eventId[9] effectId[10] amp[11] particles[12] dur[13:17]
            self.effect_eids.append(struct.unpack(">q", payload[1:9])[0])
            self.effect_pkts.append((payload[9], payload[10], payload[11],
                                      struct.unpack(">i", payload[13:17])[0]))
        elif payload and payload[0] == UPDATE_ATTRIBUTES and len(payload) >= 11:
            self.attr_eids.append(struct.unpack(">q", payload[1:9])[0])
            count = struct.unpack(">H", payload[9:11])[0]
            off = 11
            for _ in range(count):
                if off + 14 > len(payload):
                    break
                value = struct.unpack(">f", payload[off + 8:off + 12])[0]
                nlen = struct.unpack(">H", payload[off + 12:off + 14])[0]
                name = payload[off + 14:off + 14 + nlen].decode("utf-8", "replace")
                off += 14 + nlen
                self.attrs[name] = value
        elif payload and payload[0] == ADD_ENTITY and len(payload) >= 13:
            etype = struct.unpack(">i", payload[9:13])[0]
            if etype == 86:
                self.entity86 += 1
                self.entity86_eid = struct.unpack(">q", payload[1:9])[0]
                # Fixed prefix ends at byte 49; the potion id rides in
                # the ADD metadata itself (reference ThrownPotion).
                if len(payload) >= 49:
                    meta, _ = parse_metadata(payload, 49)
                    if META_IDX_POTION_ID in meta:
                        self.potion_meta = meta[META_IDX_POTION_ID][1]
        elif payload and payload[0] == SET_ENTITY_DATA and len(payload) >= 9:
            seid = struct.unpack(">q", payload[1:9])[0]
            meta, _ = parse_metadata(payload, 9)
            if META_IDX_POTION_COLOR in meta or META_IDX_POTION_AMBIENT in meta:
                color = meta.get(META_IDX_POTION_COLOR, (0, 0))[1]
                ambient = meta.get(META_IDX_POTION_AMBIENT, (0, 0))[1]
                self.color_meta[seid] = (color, ambient)
        elif payload and payload[0] == REMOVE_ENTITY and len(payload) >= 9:
            self.removed.append(struct.unpack(">q", payload[1:9])[0])
        elif payload and payload[0] == LEVEL_EVENT and len(payload) >= 19:
            evid = struct.unpack(">h", payload[1:3])[0]
            if evid == 2002:
                fx, fy, fz = struct.unpack(">fff", payload[3:15])
                data = struct.unpack(">i", payload[15:19])[0]
                self.splash_events.append((fx, fy, fz, data))
        elif payload and payload[0] == ADD_PLAYER and len(payload) > 40:
            # [pid][uuid 16B][username str][eid i64BE] — uuid ends at 17.
            nlen = struct.unpack(">H", payload[17:19])[0]
            off = 19 + nlen
            if off + 8 <= len(payload):
                eid = struct.unpack(">q", payload[off:off + 8])[0]
                uname = payload[19:19 + nlen].decode("utf-8", "replace")
                if uname != self.name:
                    self.other_eids[uname] = eid
                    self.victim_eid = eid
        elif payload and payload[0] == ENTITY_EVENT and len(payload) >= 10:
            if payload[9] == 9:
                self.eat_echoes += 1
                self.eat_echo_eids.append(struct.unpack(">q", payload[1:9])[0])
        elif payload and payload[0] == CONTAINER_SET_CONTENT and len(payload) >= 3:
            self.parse_inventory(payload)
        elif payload and payload[0] == MOVE_PLAYER and len(payload) >= 34:
            # Server->client MovePlayer MODE_RESET reveals the true
            # server-side position.
            mode = payload[33]
            if mode == 1:
                x, y, z = struct.unpack(">fff", payload[9:21])
                self.server_pos = (x, y, z)
        FallClient.classify(self, payload)

    def parse_inventory(self, payload: bytes):
        win = payload[1]
        if win != 0:
            return
        count = struct.unpack(">h", payload[2:4])[0]
        off = 4
        totals = {}
        for _ in range(count):
            if off + 2 > len(payload):
                return
            sid = struct.unpack(">h", payload[off:off + 2])[0]
            off += 2
            if sid == 0:
                continue
            if off + 5 > len(payload):
                return
            cnt = payload[off]
            nlen = struct.unpack("<H", payload[off + 3:off + 5])[0]
            off += 5 + nlen
            totals[sid] = totals.get(sid, 0) + cnt
        self.inv_counts = totals

    # ---- helpers ----
    def creative_take(self, slot, item_id, count=1, meta=0):
        cs = bytes([CONTAINER_SET_SLOT, 0])
        cs += struct.pack(">h", slot)
        cs += struct.pack(">h", 0)
        cs += struct.pack(">h", item_id)
        cs += bytes([count])
        cs += struct.pack(">h", meta)
        cs += struct.pack("<H", 0)
        self.send_game(cs)

    def say(self, msg):
        self.send_game(bytes([TEXT_PACKET, 1]) + bestr(self.name) + bestr(msg))

    def mob_equipment(self, hotbar, item_id, meta=0):
        me = bytes([MOB_EQUIPMENT])
        me += struct.pack(">q", 1)
        me += struct.pack(">h", item_id)
        me += bytes([1])
        me += struct.pack(">h", meta)
        me += struct.pack("<H", 0)
        me += bytes([hotbar, hotbar])
        self.send_game(me)

    def entity_event(self, event):
        self.send_game(bytes([ENTITY_EVENT]) + struct.pack(">q", 1) + bytes([event]))

    def use_item_air(self, item_id, meta=0):
        ui = bytes([USE_ITEM])
        ui += struct.pack(">iii", 0, 0, 0)
        ui += bytes([0xFF])
        ui += struct.pack(">fff", 0.0, 0.0, 0.0)
        ui += struct.pack(">fff", *self.stand_pos())
        ui += struct.pack(">i", 0)
        ui += struct.pack(">h", item_id)
        ui += bytes([1])
        ui += struct.pack(">h", meta)
        ui += struct.pack("<H", 0)
        self.send_game(ui)

    def aim(self, yaw, pitch):
        x, y, z = self.stand_pos()
        mp = bytes([MOVE_PLAYER]) + struct.pack(">q", 1)
        mp += struct.pack(">fff", x, y, z)
        mp += struct.pack(">fff", yaw, yaw, pitch)
        mp += bytes([0, 1])
        self.send_game(mp)

    def interact(self, action, target_eid):
        it = bytes([INTERACT, action])
        it += struct.pack(">q", target_eid)
        self.send_game(it)

    def stand_pos(self):
        """Current standing eye position (server-learned when possible)."""
        if self.server_pos is not None:
            return self.server_pos
        return (129.0, self.ground_y + EYE, 128.4)

    def sprint_action(self, action):
        pa = bytes([PLAYER_ACTION])
        pa += struct.pack(">q", 1)
        pa += struct.pack(">i", action)
        pa += struct.pack(">iii", 0, 0, 0)
        pa += struct.pack(">i", 0)
        self.send_game(pa)

    def sprint_move(self, packets=60, step=18.0):
        """Sprint back and forth near the spawn (hunger drain driver).
        Each 10-packet cycle walks `step` x5 one way and x5 back, so the
        round ENDS where it started: the throw scenarios that follow need
        known-flat terrain under the player."""
        if self.server_pos is None:
            self.move(129.0, self.ground_y + EYE + 10, 128.4, True)
            self.watch(0.6)
        sx, sy, sz = self.server_pos or (129.0, self.ground_y + EYE, 128.4)
        x = sx
        direction = 1
        for i in range(packets):
            x += direction * step
            self.move(x, sy, sz, True)
            if (i + 1) % 5 == 0:
                direction = -direction

    def drain_food_to(self, apple_at=None):
        """Sprint-burn food below the regen floor (18). When the drain
        bottoms out at exactly 0 and the caller stocked an apple (slot
        0), eat it to park food at a known level (4). Any level in
        1..17 is equally safe for the health scenarios (no natural
        regen, no starvation), so a partial drain is accepted as is."""
        for _ in range(10):
            food = self.attrs.get("player.hunger")
            if food is not None and food <= 17.0:
                break
            self.sprint_action(ACTION_START_SPRINT)
            self.sprint_move(packets=60)
            deadline = time.time() + 6.0
            while time.time() < deadline:
                food = self.attrs.get("player.hunger")
                if food is not None and food <= 17.0:
                    break
                self.watch(0.3)
        food = self.attrs.get("player.hunger") or 0.0
        if apple_at is not None and food == 0.0:
            self.mob_equipment(0, APPLE)
            time.sleep(0.3)
            self.entity_event(9)
            deadline = time.time() + 4.0
            while time.time() < deadline:
                if self.attrs.get("player.hunger") == float(apple_at):
                    break
                self.watch(0.3)

    def effect_pkts_for(self, effect_id, since=0):
        return [p for p in self.effect_pkts[since:] if p[1] == effect_id]


def potion_login(host, port, name):
    for attempt in range(3):
        c = PotionClient(host, port, name)
        if c.handshake():
            print(f"[{name}] handshake retry {attempt + 1}")
            continue
        if c.login():
            print(f"[{name}] login retry {attempt + 1}")
            continue
        if not c.expect("start_game", seconds=6.0):
            print(f"[{name}] spawn retry {attempt + 1}")
            continue
        c.expect("chunks_streamed", seconds=5.0)
        c.watch(1.0)
        return c
    print(f"FAIL[{name}]: could not complete login")
    return None


def run(host: str, port: int) -> int:
    name = "PotionOne"
    c = potion_login(host, port, name)
    if not c:
        return 1
    failures = []

    def check(label, ok, detail=""):
        print(f"{'PASS' if ok else 'FAIL'}: {label} {detail}")
        if not ok:
            failures.append(label)

    def wait_pred(pred, seconds, client=None):
        """Poll until pred() holds. Polls on `client` when given so the
        post-reconnect scenarios drive their own sockets (the original
        client c is disconnected by then)."""
        cl = client or c
        deadline = time.time() + seconds
        while time.time() < deadline:
            if pred():
                return True
            cl.watch(0.3)
        return False

    def stock_items(client, items):
        """Creative-stock (slot, id, count, meta) tuples in one toggle.
        The take can race the /gamemode 1 processing and get rejected by
        the survival slot-validation, so after the toggle back the
        window-0 resync is checked and the whole batch is retried when
        any item is missing."""
        for _ in range(3):
            client.say("/gamemode 1")
            time.sleep(0.8)
            for entry in items:
                slot, item_id, count = entry[0], entry[1], entry[2]
                meta = entry[3] if len(entry) > 3 else 0
                client.mob_equipment(slot, item_id, meta)
                time.sleep(0.1)
                client.creative_take(slot, item_id, count, meta)
                time.sleep(0.15)
            time.sleep(0.4)
            client.say("/gamemode 0")
            time.sleep(0.8)
            client.watch(0.5)
            want = {}
            for entry in items:
                want[entry[1]] = want.get(entry[1], 0) + entry[2]
            if all(client.inv_counts.get(k, 0) >= v for k, v in want.items()):
                return
        print("WARN: stock_items could not verify every item")

    def resync_inventory():
        c.say("/gamemode 1")
        time.sleep(0.5)
        c.say("/gamemode 0")
        time.sleep(0.5)
        c.watch(0.8)

    def drink(slot, meta, wait_after=1.0, client=None):
        """Equip the stocked potion and finish drinking."""
        cl = client or c
        cl.mob_equipment(slot, POTION, meta)
        time.sleep(0.3)
        since = len(cl.effect_pkts)
        cl.entity_event(9)
        cl.watch(wait_after)
        return since

    # --- 0. deterministic start ---
    ok = c.reset_state()
    check("deterministic start (kill + respawn)", ok)
    if not ok:
        print("FAILED: " + ", ".join(failures))
        return 1

    # --- 1. water bottle is not drinkable ---
    stock_items(c, [(0, POTION, 1, META_WATER)])
    before = len(c.effect_pkts)
    c.mob_equipment(0, POTION, META_WATER)
    time.sleep(0.3)
    c.entity_event(9)
    c.watch(1.2)
    check("water bottle: no effect packet", len(c.effect_pkts) == before,
          f"packets={len(c.effect_pkts) - before}")
    resync_inventory()
    check("water bottle: no residue bottle", c.inv_counts.get(GLASS_BOTTLE, 0) == 0,
          f"bottles={c.inv_counts.get(GLASS_BOTTLE, 0)}")

    # --- 2/3/4. swiftness, cooldown, slowness (all stocked up front) ---
    stock_items(c, [(0, POTION, 1, META_SWIFTNESS), (1, POTION, 1, META_SLOWNESS)])
    since = drink(0, META_SWIFTNESS)
    adds = [p for p in c.effect_pkts[since:] if p[0] == 1 and p[1] == 1]
    check("swiftness ADD speed amp0 dur3600",
          any(p[2] == 0 and p[3] == 3600 for p in adds),
          f"pkts={c.effect_pkts[since:]}")
    spd = c.attrs.get("generic.movementSpeed")
    check("swiftness movementSpeed = 0.12",
          spd is not None and abs(spd - 0.12) < 0.001, f"speed={spd}")
    check("MobEffect self packets carry the real runtime id",
          c.eid is not None and c.effect_eids[since:] and
          all(e == c.eid for e in c.effect_eids[since:]),
          f"eids={c.effect_eids[since:]}")
    check("UpdateAttributes self packets carry the real runtime id",
          c.eid is not None and c.attr_eids and
          all(e == c.eid for e in c.attr_eids),
          f"eids={c.attr_eids[-4:]}")
    check("eat echo addressed to the real entity id",
          c.eid is not None and c.eat_echo_eids and c.eat_echo_eids[-1] == c.eid,
          f"eid={c.eid} echo={c.eat_echo_eids[-1:]}")
    check("self receives the potion tint metadata",
          c.eid is not None and c.color_meta.get(c.eid, (0, 0))[0] != 0,
          f"tint={c.color_meta.get(c.eid)}")
    # Cooldown: the second drink lands within the 1.6 s window.
    since = drink(1, META_SLOWNESS, wait_after=0.6)
    check("drink cooldown blocks the second drink",
          not c.effect_pkts_for(2, since), f"pkts={c.effect_pkts[since:]}")
    time.sleep(1.6)
    since = drink(1, META_SLOWNESS)
    adds = [p for p in c.effect_pkts[since:] if p[0] == 1 and p[1] == 2]
    check("slowness ADD amp0 dur1800 after cooldown",
          any(p[2] == 0 and p[3] == 1800 for p in adds),
          f"pkts={c.effect_pkts[since:]}")
    spd = c.attrs.get("generic.movementSpeed")
    check("speed+slowness movementSpeed = 0.102",
          spd is not None and abs(spd - 0.102) < 0.001, f"speed={spd}")
    resync_inventory()
    check("swiftness left a glass bottle", c.inv_counts.get(GLASS_BOTTLE, 0) >= 1,
          f"bottles={c.inv_counts.get(GLASS_BOTTLE, 0)}")

    # --- health-scenario stock: everything in ONE creative toggle.
    # Slots 5..7 hold splash potions for the throw retry loop (some
    # spawn columns sit over water/caves and the bottle breaks outside
    # the catch radius; a couple of side-steps find solid ground).
    # Slot 8's drinkable Speed II is used right before the reconnect so
    # the persistence check has a fresh amp-1 effect.
    stock_items(c, [(0, APPLE, 1),
                    (1, POTION, 1, META_HARMING),
                    (2, POTION, 1, META_HEALING),
                    (3, POTION, 1, META_POISON),
                    (4, POTION, 1, META_REGENERATION_TWO),
                    (5, SPLASH_POTION, 1, META_SWIFTNESS_TWO),
                    (6, SPLASH_POTION, 1, META_SWIFTNESS_TWO),
                    (7, SPLASH_POTION, 1, META_SWIFTNESS_TWO),
                    (8, POTION, 1, META_SWIFTNESS_TWO)])

    # --- 5. splash potion FIRST (before any sprint drain): the thrower
    # still stands at the spawn snap, so the bottle usually breaks at
    # the feet. When the spawn column is over deep terrain the bottle
    # breaks outside the 6-block catch radius, so the throw retries on
    # a side-stepped position until the area effect lands.
    splash_slots = [5, 6, 7]
    caught = False
    nudged = 0.0
    for slot in splash_slots:
        since_pkts = len(c.effect_pkts)
        c.mob_equipment(slot, SPLASH_POTION, META_SWIFTNESS_TWO)
        time.sleep(0.3)
        c.aim(90.0, 88.0)  # look straight down
        c.use_item_air(SPLASH_POTION, META_SWIFTNESS_TWO)
        wait_pred(lambda: c.entity86 >= 1, 3.0)
        wait_pred(lambda: c.entity86_eid is not None and c.entity86_eid in c.removed, 4.0)
        caught = wait_pred(lambda: any(p[0] in (1, 2) and p[1] == 1 and p[2] == 1
                                       for p in c.effect_pkts[since_pkts:]), 4.0)
        if caught:
            break
        # Step onto a different column. server_pos has no RESET to learn
        # from, so the walk target is tracked locally; the offset grows
        # every attempt (each hop stays under the 20-block move limit) to
        # escape wide caves or lakes under the spawn area.
        nudged += 18.0
        sx, sy, sz = c.server_pos or (129.0, c.ground_y + EYE, 128.4)
        c.server_pos = (sx + nudged, sy, sz)
        c.move(sx + nudged, sy, sz, True)
        c.watch(0.6)
    check("splash throw spawns entity type 86", c.entity86 >= 1, f"count={c.entity86}")
    check("splash bottle breaks (removed)",
          c.entity86_eid is not None and c.entity86_eid in c.removed)
    check("splash catches swiftness II (amp 1 add/modify)", caught,
          f"pkts={c.effect_pkts[since_pkts:]}")
    check("splash ADD carries DATA_POTION_ID = thrown meta",
          c.potion_meta == META_SWIFTNESS_TWO, f"meta={c.potion_meta}")
    check("splash break sends the shatter event (glass smash + swirl)",
          len(c.splash_events) >= 1,
          f"events={c.splash_events[:2]}")
    if c.splash_events:
        check("shatter event carries an opaque ARGB color",
              (c.splash_events[-1][3] & 0xFF000000) == 0xFF000000 and
              (c.splash_events[-1][3] & 0x00FFFFFF) != 0,
              f"data=0x{c.splash_events[-1][3] & 0xFFFFFFFF:08x}")
    spd = c.attrs.get("generic.movementSpeed")
    check("swiftness II movementSpeed = 0.119",
          caught and spd is not None and abs(spd - 0.119) < 0.002, f"speed={spd}")

    # --- drain food below the regen floor (no sprint-drain before the
    # splash: the throw needs the flat spawn terrain) ---
    c.drain_food_to(apple_at=4)
    time.sleep(1.8)  # clear the eat cooldown before the first potion
    food = c.attrs.get("player.hunger") or 0.0
    check("food below the regen floor for health scenarios",
          0.0 < food <= 17.0, f"food={food}")

    # --- 6. harming: instant magic damage -6 (food may already have
    # costed a starvation point during the drain, so compare relatively) ---
    hp_before = c.attrs.get("generic.health") or 20.0
    drink(1, META_HARMING)
    hp = c.attrs.get("generic.health")
    check("harming potion deals 6 magic damage",
          hp is not None and abs(hp - (hp_before - 6.0)) < 0.01,
          f"health {hp_before} -> {hp}")

    # --- 7. healing: instant heal +4 ---
    time.sleep(1.8)
    hp_before = c.attrs.get("generic.health") or 0.0
    drink(2, META_HEALING)
    hp = c.attrs.get("generic.health")
    check("healing potion heals 4",
          hp is not None and abs(hp - (hp_before + 4.0)) < 0.01,
          f"health {hp_before} -> {hp}")

    # --- 8. poison: ADD packet + a -1 hp tick ---
    time.sleep(1.8)
    hp_before = c.attrs.get("generic.health") or 0.0
    drink(3, META_POISON, wait_after=3.5)
    adds = c.effect_pkts_for(19)
    check("poison ADD amp0 dur900", any(p[0] == 1 and p[2] == 0 and p[3] == 900
                                        for p in adds), f"pkts={adds}")
    hp = c.attrs.get("generic.health")
    check("poison deals its first tick", hp is not None and hp < hp_before,
          f"health {hp_before} -> {hp}")

    # --- 9. regeneration II: the ADD packet lands (the health gain is
    # asserted on the victim, where poison cannot perturb the math) ---
    time.sleep(1.8)
    drink(4, META_REGENERATION_TWO, wait_after=1.0)
    adds = [p for p in c.effect_pkts_for(10) if p[2] == 1]
    check("regeneration II ADD amp1", len(adds) >= 1, f"pkts={adds}")

    # --- 10. persistence: reconnect restores the active effects ---
    # A fresh drinkable Speed II (slot 8) refreshes an amp-1 effect, so
    # the relogin check does not depend on the splash timing. Socket
    # close + same-name relogin: the duplicate-name kick saves first.
    time.sleep(1.8)
    drink(8, META_SWIFTNESS_TWO, wait_after=1.0)
    time.sleep(1.0)
    c.sock.close()
    time.sleep(2.0)
    c2 = potion_login(host, port, name)
    if not c2:
        check("persistence: reconnect", False)
        print("FAILED: " + ", ".join(failures))
        return 1
    ok = wait_pred(lambda: c2.effect_pkts_for(19), 6.0, client=c2)
    check("poison restored after reconnect (ADD re-sent)", ok,
          f"pkts={c2.effect_pkts_for(19)}")
    restored_eids = [c2.effect_eids[i] for i, p in enumerate(c2.effect_pkts)
                     if p[0] == 1 and p[1] in (19, 1)]
    check("restored ADDs carry the real entity id",
          c2.eid is not None and restored_eids and
          all(e == c2.eid for e in restored_eids),
          f"eids={restored_eids}")
    check("speed II restored after reconnect", wait_pred(
        lambda: any(p[2] == 1 for p in c2.effect_pkts_for(1)), 3.0, client=c2))

    # --- 11. death clears every effect ---
    c2.say("/gamemode 0")
    time.sleep(0.6)
    c2.say(f"/kill {name}")
    ok = wait_pred(lambda: c2.health <= 1.0, 5.0, client=c2)
    check("kill lands", ok, f"health={c2.health}")
    c2.player_action(7)  # ACTION_SPAWN_SAME_DIMENSION (fall_damage_check id)
    ok = wait_pred(lambda: c2.health >= 19.0, 8.0, client=c2)
    check("respawn restores health", ok, f"health={c2.health}")
    base = len(c2.effect_pkts)
    c2.watch(3.0)
    check("no poison after death (effects cleared)",
          not any(p[0] == 1 and p[1] == 19 for p in c2.effect_pkts[base:]) and
          c2.health >= 19.0, f"health={c2.health}")

    # --- 12/13. melee modifiers: attacker + victim ---
    # The victim logs in FIRST so the attacker (joining later) receives
    # the victim's ADD_PLAYER during its own first spawn (the joiner is
    # told about every already-spawned player — no join-order race).
    vic = potion_login(host, port, "PotionVictim")
    atk = potion_login(host, port, "PotionTwo")
    if not atk or not vic:
        check("melee: second client login", False)
        print("FAILED: " + ", ".join(failures))
        return 1
    ok = wait_pred(lambda: atk.other_eids.get("PotionVictim") is not None, 6.0,
                   client=atk)
    check("attacker sees the victim eid", ok,
          f"eids={atk.other_eids}")
    victim_eid = atk.other_eids.get("PotionVictim")
    if ok:
        # The victim stocks its apple + a regeneration II potion first
        # (creative toggle), then pins food to 4 by draining: natural
        # regen cannot heal the punches back.
        stock_items(vic, [(0, APPLE, 1), (1, POTION, 1, META_REGENERATION_TWO)])
        vic.drain_food_to(apple_at=4)
        vfood = vic.attrs.get("player.hunger") or 0.0
        check("victim food below the regen floor", 0.0 < vfood <= 17.0,
              f"food={vfood}")
        # Strength: punch deals 1 + 3 = 4.
        stock_items(atk, [(0, POTION, 1, META_STRENGTH)])
        atk.mob_equipment(0, POTION, META_STRENGTH)
        time.sleep(0.3)
        atk.entity_event(9)
        atk.watch(1.2)
        check("strength ADD packet", len(atk.effect_pkts_for(5)) >= 1)
        # The victim (a viewer) must receive the tint metadata for the
        # attacker — the properties the client draws swirl particles
        # from. The attacker's eid comes from its own START_GAME (the
        # late-joiner ADD_PLAYER can be refused by the reliable window
        # while the harness client is not draining).
        atk_eid_on_vic = atk.eid
        ok = wait_pred(lambda: atk_eid_on_vic is not None and
                       vic.color_meta.get(atk_eid_on_vic, (0, 0))[0] != 0, 4.0,
                       client=vic)
        check("viewer receives DATA_POTION_COLOR tint for the attacker", ok,
              f"atk_eid={atk.eid} tint={vic.color_meta}")
        hp_before = vic.attrs.get("generic.health") or 20.0
        atk.interact(2, victim_eid)
        ok = wait_pred(lambda: (vic.attrs.get("generic.health") or 99) < hp_before, 3.0,
                       client=vic)
        hp = vic.attrs.get("generic.health")
        check("strength punch deals 4 (20 -> 16)", ok and hp is not None and abs(hp - 16.0) < 0.01,
              f"health={hp}")
        # Weakness: with strength still up the punch deals max(1+3-4, 0).
        time.sleep(1.8)
        stock_items(atk, [(0, POTION, 1, META_WEAKNESS)])
        atk.mob_equipment(0, POTION, META_WEAKNESS)
        time.sleep(0.3)
        atk.entity_event(9)
        atk.watch(1.2)
        check("weakness ADD packet", len(atk.effect_pkts_for(18)) >= 1)
        hp_before = vic.attrs.get("generic.health") or 0.0
        atk.interact(2, victim_eid)
        vic.watch(1.5)
        hp = vic.attrs.get("generic.health")
        check("weakness punch deals 0 (health holds)",
              hp is not None and abs(hp - hp_before) < 0.01,
              f"health {hp_before} -> {hp}")
        # Regeneration II: the damaged victim heals +1 within the 1 s
        # interval (clean: no poison running on the victim).
        time.sleep(1.8)
        vic.mob_equipment(1, POTION, META_REGENERATION_TWO)
        time.sleep(0.3)
        vic.entity_event(9)
        ok = wait_pred(lambda: (vic.attrs.get("generic.health") or 0) > hp, 3.0, client=vic)
        hp2 = vic.attrs.get("generic.health")
        check("regeneration II heals the victim +1", ok and hp2 is not None,
              f"health {hp} -> {hp2}")

    print()
    if failures:
        print(f"FAILED ({len(failures)}): " + ", ".join(failures))
        return 1
    print("ALL PASSED")
    return 0


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19132)
    args = ap.parse_args()
    sys.exit(run(args.host, args.port))
