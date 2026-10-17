#!/usr/bin/env python3
"""End-to-end hunger (food) system + inventory-drop check for CurseStone.

Needs ops.txt entries for HungerOne (and HungerPersist) so it can run
/kill and /gamemode. One real protocol-84 client drives every path over
the wire:

hunger core:
  0. deterministic start: /kill + respawn (20 hp, food 20, clean inv)
  1. full-hunger gate: eating at 20 food is refused (no eat echo)
  2. sprint + movement burns exhaustion -> saturation -> food -> 0
     (exhaustion attribute display clamp <= 5.0)
  3. starvation at 0 food deals 1 damage after 4 s (difficulty >= 1)
  4. eating bread: food +5, saturation +6.0, eat echo, slot consumed
  5. eat cooldown: a second eat within 1.6 s does nothing
  6. after the cooldown the next eat works again
  7. food sweep: EVERY registered food is stocked and eaten; the exact
     hunger delta and (after a clean drain) the exact saturation value
     are asserted; stew residues swap in a bowl
  8. hunger cap: eating past 20 clamps to 20
  9. always-edible: golden apple is allowed at 20 food, saturation rises
 10. gate re-check: bread is refused again at 20 food
 11. creative: sprinting does not drain hunger
 12. respawn reset: after /kill + respawn food=20, saturation=5, exh=0
 13. persistence: disconnect + reconnect restores food/saturation and
     every stocked item

inventory drop/sync (regression for the PC inventory-UI drop bug):
 14. move within inventory: empty + fill slot writes never spawn a drop
 15. drag-out drop: emptied slot + DropItem spawns the entity, item is
     NOT lost (the pre-fix server voided it)
 16. close with cursor item: floating leftovers come back (no loss)
 17. item creation from nothing: a survival write of an item the client
     never took out is rejected and the slot resynced
 18. plain Q drop with no UI open still works from the inventory

Usage:
    python3 tools/hunger_check.py [--host 127.0.0.1] [--port 19132]
"""
import argparse
import struct
import sys
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from fall_damage_check import FallClient, GROUND_EYE  # noqa: E402

TEXT_PACKET = 0x07
START_GAME = 0x09
ENTITY_EVENT = 0x18
UPDATE_ATTRIBUTES = 0x1A
MOB_EQUIPMENT = 0x1B
ADD_ITEM_ENTITY = 0x0D
CONTAINER_SET_SLOT = 0x2C
CONTAINER_SET_CONTENT = 0x2E
CONTAINER_CLOSE = 0x2B
DROP_ITEM = 0x29
PLAYER_ACTION = 0x20
MOVE_PLAYER = 0x10
ACTION_START_SPRINT = 9
EYE = 1.62

START_X, START_Z = 129.0, 128.4
BREAD = 297
BOWL = 281
DIAMOND_BLOCK = 57
GOLDEN_APPLE = 322
GOLDEN_CARROT = 396
SUGAR = 353  # marker item for the drop scenarios (never eaten here)

# Every registered food: (id, name, food points, saturation, residue id).
# Values mirror the ItemXxx constructors exactly (ItemFood(id, food,
# saturation, always_edible, residue)).
FOODS = [
    (260, "apple", 4, 2.4, 0),
    (282, "mushroom_stew", 6, 7.2, BOWL),
    (297, "bread", 5, 6.0, 0),
    (319, "raw_porkchop", 3, 0.6, 0),
    (320, "cooked_porkchop", 8, 12.8, 0),
    (322, "golden_apple", 4, 9.6, 0),
    (349, "raw_fish", 2, 0.4, 0),
    (350, "cooked_fish", 5, 6.0, 0),
    (357, "cookie", 2, 0.4, 0),
    (360, "melon_slice", 2, 1.2, 0),
    (363, "raw_beef", 3, 1.8, 0),
    (364, "steak", 8, 12.8, 0),
    (365, "raw_chicken", 2, 1.2, 0),
    (366, "cooked_chicken", 6, 7.2, 0),
    (367, "rotten_flesh", 4, 0.8, 0),
    (375, "spider_eye", 2, 3.2, 0),
    (391, "carrot", 3, 4.8, 0),
    (393, "baked_potato", 5, 7.2, 0),
    (396, "golden_carrot", 6, 14.4, 0),
    (400, "pumpkin_pie", 8, 4.8, 0),
    (411, "raw_rabbit", 3, 1.8, 0),
    (412, "cooked_rabbit", 5, 6.0, 0),
    (413, "rabbit_stew", 10, 12.0, BOWL),
    (423, "raw_mutton", 2, 1.2, 0),
    (424, "cooked_mutton", 6, 9.6, 0),
    (457, "beetroot", 1, 1.2, 0),
    (459, "beetroot_soup", 6, 7.2, BOWL),
    (466, "enchanted_golden_apple", 4, 9.6, 0),
]


def bestr(s: str) -> bytes:
    raw = s.encode()
    return struct.pack(">H", len(raw)) + raw


class HungerClient(FallClient):
    """FallClient + hunger attributes + inventory/drop observers."""

    def __init__(self, host, port, name):
        super().__init__(host, port, name)
        self.attrs = {}          # latest attribute values by name
        self.eid = None          # own runtime id (START_GAME)
        self.attr_eids = []      # eid field of every UpdateAttributesPacket
        self.eat_echoes = 0      # EntityEvent USE_ITEM(9) seen (eat echo)
        self.inv_counts = {}     # item id -> total units (window-0 pushes)
        self.slot_writes = []    # server slot confirms/reverts (win 0)
        self.drops = 0           # AddItemEntity packets seen
        self.server_pos = None   # (x, y, z) learned from a snap-back

    def classify(self, payload: bytes):
        if payload and payload[0] == START_GAME and len(payload) >= 22:
            # [pid][seed i32][dim u8][gen i32][gamemode i32][eid i64BE]
            self.eid = struct.unpack(">q", payload[14:22])[0]
        elif payload and payload[0] == UPDATE_ATTRIBUTES and len(payload) >= 11:
            self.attr_eids.append(struct.unpack(">q", payload[1:9])[0])
            count = struct.unpack(">H", payload[9:11])[0]
            off = 11
            for _ in range(count):
                if off + 14 > len(payload):
                    break
                # entry: min f32, max f32, value f32, name str16
                value = struct.unpack(">f", payload[off + 8:off + 12])[0]
                nlen = struct.unpack(">H", payload[off + 12:off + 14])[0]
                name = payload[off + 14:off + 14 + nlen].decode("utf-8", "replace")
                off += 14 + nlen
                self.attrs[name] = value
        elif payload and payload[0] == ENTITY_EVENT and len(payload) >= 10:
            if payload[9] == 9:
                self.eat_echoes += 1
        elif payload and payload[0] == CONTAINER_SET_CONTENT and len(payload) >= 3:
            self.parse_inventory(payload)
        elif payload and payload[0] == CONTAINER_SET_SLOT and len(payload) >= 6:
            self.parse_slot(payload)
        elif payload and payload[0] == ADD_ITEM_ENTITY:
            self.drops += 1
        elif payload and payload[0] == MOVE_PLAYER and len(payload) >= 34:
            # Server->client MovePlayer: a MODE_RESET reveals the true
            # server-side position (used to sync before sprinting).
            mode = payload[33]
            if mode == 1:
                x, y, z = struct.unpack(">fff", payload[9:21])
                self.server_pos = (x, y, z)
        FallClient.classify(self, payload)

    def parse_inventory(self, payload: bytes):
        """Count every item id in a ContainerSetContent window-0 push."""
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

    def parse_slot(self, payload: bytes):
        """Record window-0 slot confirms/reverts sent by the server."""
        win = payload[1]
        if win != 0:
            return
        slot = struct.unpack(">h", payload[2:4])[0]
        # windowid[1] slot[2:4] hotbar[4:6] itemId[6:8] ...
        iid = struct.unpack(">h", payload[6:8])[0]
        cnt = 0
        if iid > 0 and len(payload) >= 9:
            cnt = payload[8]
        self.slot_writes.append((slot, iid, cnt))

    # ---- helpers ----
    def creative_take(self, slot, item_id, count=1):
        """ContainerSetSlot (window 0): server puts the item in `slot`."""
        cs = bytes([CONTAINER_SET_SLOT])
        cs += bytes([0])                      # windowid 0 (inventory)
        cs += struct.pack(">h", slot)         # slot
        cs += struct.pack(">h", 0)            # hotbarSlot
        cs += struct.pack(">h", item_id)      # item id
        cs += bytes([count])                  # count
        cs += struct.pack(">h", 0)            # damage
        cs += struct.pack("<H", 0)            # no nbt
        self.send_game(cs)

    def slot_write(self, slot, item_id, count, damage=0):
        """A real client slot write (the gesture the UI sends on drag)."""
        cs = bytes([CONTAINER_SET_SLOT])
        cs += bytes([0])
        cs += struct.pack(">h", slot)
        cs += struct.pack(">h", 0)
        cs += struct.pack(">h", item_id)
        cs += bytes([max(0, count)])
        cs += struct.pack(">h", damage)
        cs += struct.pack("<H", 0)
        self.send_game(cs)

    def say(self, msg):
        self.send_game(bytes([TEXT_PACKET, 1]) + bestr(self.name) + bestr(msg))

    def mob_equipment(self, hotbar, item_id, count=1):
        me = bytes([MOB_EQUIPMENT])
        me += struct.pack(">q", 1)          # eid (server re-stamps it)
        me += struct.pack(">h", item_id)    # slot: id
        me += bytes([count])                # count
        me += struct.pack(">h", 0)          # damage
        me += struct.pack("<H", 0)          # no nbt
        me += bytes([hotbar])               # hotbarSlot
        me += bytes([hotbar])               # selectedSlot
        self.send_game(me)

    def entity_event(self, event):
        ee = bytes([ENTITY_EVENT]) + struct.pack(">q", 1) + bytes([event])
        self.send_game(ee)

    def container_close(self, windowid=0):
        self.send_game(bytes([CONTAINER_CLOSE, windowid]))

    def drop_item(self, item_id, count, dtype=0):
        di = bytes([DROP_ITEM, dtype])
        di += struct.pack(">h", item_id)
        di += bytes([count])
        di += struct.pack(">h", 0)
        di += struct.pack("<H", 0)
        self.send_game(di)

    def sprint_action(self, action):
        pa = bytes([PLAYER_ACTION])
        pa += struct.pack(">q", 1)          # eid
        pa += struct.pack(">i", action)
        pa += struct.pack(">iii", 0, 0, 0)  # block pos (unused)
        pa += struct.pack(">i", 0)          # face
        self.send_game(pa)

    def sprint_move(self, packets=60, step=19.0):
        """Sprint back and forth near the spawn while sending moves.

        The anti-cheat rejects moves more than ~20 blocks from the
        server-side position (learn it from a snap-back first). The run
        ping-pongs inside the loaded spawn area (chunk generation would
        stall the session) and sends without pacing so the per-tick
        exhaustion batches cross the sprint floor (food <= 6) downwards.
        """
        if self.server_pos is None:
            self.move(START_X + 100, GROUND_EYE + 10, START_Z, True)
            self.watch(0.6)
        sx, sy, sz = self.server_pos or (START_X, GROUND_EYE - EYE, START_Z)
        x = sx
        direction = 1
        for _ in range(packets):
            x += direction * step
            self.move(x, sy + EYE, sz, True)
            if abs(x - sx) > 80:
                direction = -1


def hunger_login(host, port, name):
    for attempt in range(3):
        c = HungerClient(host, port, name)
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
    name = "HungerOne"
    c = hunger_login(host, port, name)
    if not c:
        return 1
    failures = []

    def check(label, ok, detail=""):
        print(f"{'PASS' if ok else 'FAIL'}: {label} {detail}")
        if not ok:
            failures.append(label)

    def wait_attr(attr, value, seconds, tol=0.0):
        """Poll watch() until attrs[attr] reaches value (or timeout)."""
        deadline = time.time() + seconds
        while time.time() < deadline:
            v = c.attrs.get(attr)
            if v is not None and abs(v - value) <= tol:
                return v
            c.watch(0.3)
        return c.attrs.get(attr)

    def wait_pred(pred, seconds):
        deadline = time.time() + seconds
        while time.time() < deadline:
            if pred():
                return True
            c.watch(0.3)
        return False

    def drain_to_zero(client=None, seconds=6.0):
        """Sprint-burn everything: saturation then food down to 0.

        Repeats the sprint batch until the bar is really empty — a single
        batch can stop one point short depending on tick alignment.
        """
        cl = client or c
        for _ in range(4):
            if cl.attrs.get("player.hunger") == 0.0:
                return 0.0
            cl.sprint_action(ACTION_START_SPRINT)
            cl.sprint_move(packets=60)
            deadline = time.time() + seconds
            while time.time() < deadline:
                if cl.attrs.get("player.hunger") == 0.0:
                    break
                cl.watch(0.3)
        return cl.attrs.get("player.hunger")

    def stock_items(client, items):
        """Creative-stock (slot, item_id, count) tuples in one toggle.

        mob_equipment(i) runs BEFORE each take on purpose: the creative
        take re-links hotbar_[heldIndex] = takeSlot (setHeldSlot), so
        taking with held index == slot keeps every hotbar link identity
        and the later mob_equipment(i) really equips slot i.
        """
        client.say("/gamemode 1")
        time.sleep(0.8)
        for slot, item_id, count in items:
            client.mob_equipment(slot, item_id)
            time.sleep(0.1)
            client.creative_take(slot, item_id, count)
            time.sleep(0.15)
        time.sleep(0.4)
        client.say("/gamemode 0")
        time.sleep(0.8)
        client.watch(0.5)

    def resync_inventory():
        """Force a full authoritative inventory push (setGamemode does
        one on every switch) so inv_counts reflects the server state."""
        c.say("/gamemode 1")
        time.sleep(0.5)
        c.say("/gamemode 0")
        time.sleep(0.5)
        c.watch(0.8)

    # --- 0. deterministic start: kill + respawn ---
    # Respawn sets food=20/sat=5/exh=0 and pushes all attributes, so the
    # run never depends on whatever the previous run's .dat had saved.
    ok = c.reset_state()
    check("deterministic start (kill + respawn)", ok)
    if not ok:
        print("FAILED: " + ", ".join(failures))
        return 1
    check("respawn resets food to 20", c.attrs.get("player.hunger") == 20.0,
          f"hunger={c.attrs.get('player.hunger')}")
    check("respawn resets saturation to 5",
          abs((c.attrs.get("player.saturation") or 0) - 5.0) < 0.01,
          f"sat={c.attrs.get('player.saturation')}")

    # --- 1. full-hunger gate: eating at 20 food must be refused ---
    stock_items(c, [(0, BREAD, 3)])
    c.mob_equipment(0, BREAD)
    c.watch(1.0)
    before_echoes = c.eat_echoes
    c.entity_event(9)
    c.watch(1.2)
    check("full-hunger gate refuses eat",
          c.eat_echoes == before_echoes and c.inv_counts.get(BREAD, 0) == 3,
          f"echoes={c.eat_echoes} bread={c.inv_counts.get(BREAD)}")

    # --- 2. sprint burns exhaustion -> saturation -> food -> 0 ---
    hunger = drain_to_zero()
    sat = c.attrs.get("player.saturation")
    exh = c.attrs.get("player.exhaustion")
    check("hunger attributes pushed", hunger is not None and sat is not None,
          f"hunger={hunger} saturation={sat} exhaustion={exh}")
    check("attribute packets carry the local player runtime id",
          c.eid is not None and c.attr_eids and
          all(e == c.eid for e in c.attr_eids),
          f"eid={c.eid} attr_eids={c.attr_eids[-4:]}")
    check("sprint drains food to 0", hunger == 0.0, f"hunger={hunger}")
    check("exhaustion display clamp <= 5.0",
          exh is not None and 0.0 <= exh <= 5.0, f"exhaustion={exh}")

    # --- 3. starvation at 0 food deals 1 damage after 4 s (difficulty 1) ---
    c.watch(7.0)
    check("starvation damages the player", c.min_health < 20.0,
          f"health={c.health} min={c.min_health}")

    # --- 4. eating bread restores 5 food + 6 saturation + echo + consume ---
    c.mob_equipment(0, BREAD)
    time.sleep(0.3)
    echoes = c.eat_echoes
    c.entity_event(9)
    hunger = wait_attr("player.hunger", 5.0, 4.0)
    sat = wait_attr("player.saturation", 6.0, 2.0, tol=0.01)
    check("eat bread: hunger +5", hunger == 5.0, f"hunger={hunger}")
    check("eat bread: saturation +6.0", abs((sat or 0) - 6.0) < 0.01,
          f"sat={sat}")
    check("eat bread: server echoes event 9", c.eat_echoes > echoes,
          f"echoes={c.eat_echoes}")
    check("eat bread: slot consumed", c.inv_counts.get(BREAD) == 2,
          f"bread={c.inv_counts.get(BREAD)}")

    # --- 5. eat cooldown: second eat within 1.6 s does nothing ---
    time.sleep(0.2)
    c.entity_event(9)
    c.watch(0.5)
    check("eat cooldown blocks the second eat",
          c.attrs.get("player.hunger") == 5.0,
          f"hunger={c.attrs.get('player.hunger')}")

    # --- 6. after the cooldown the next eat works ---
    time.sleep(1.8)
    for _ in range(3):
        c.entity_event(9)
        hunger = wait_attr("player.hunger", 10.0, 2.0)
        if hunger == 10.0:
            break
        time.sleep(0.5)
    check("eat works after cooldown", hunger == 10.0, f"hunger={hunger}")

    # --- 7. food sweep: every registered food, exact deltas ---
    # Stock all foods into hotbar slots 0..8 in batches; each eat asserts
    # the exact hunger delta (and the exact saturation after a clean
    # drain, since movement exhaustion otherwise perturbs it).
    print(f"-- food sweep: {len(FOODS)} foods --")
    sweep_fail = 0
    batch = 9
    last_eat = 0.0
    for start in range(0, len(FOODS), batch):
        group = FOODS[start:start + batch]
        stock_items(c, [(i, entry[0], 1) for i, entry in enumerate(group)])
        for i, (fid, fname, fval, fsat, residue) in enumerate(group):
            # one finished eat per 1.6 s client animation — wait it out
            # or the server silently drops the eat event.
            wait = 1.7 - (time.time() - last_eat)
            if wait > 0:
                time.sleep(wait)
            hunger_now = c.attrs.get("player.hunger")
            if hunger_now is None or hunger_now > 10.0 or hunger_now >= 20.0:
                drained = drain_to_zero()
                hunger_now = c.attrs.get("player.hunger") or 0.0
                clean_base = drained == 0.0
            else:
                clean_base = False
            c.mob_equipment(i, fid)
            time.sleep(0.25)
            echoes = c.eat_echoes
            c.entity_event(9)
            last_eat = time.time()
            target = min(20.0, (hunger_now or 0.0) + fval)
            got = wait_attr("player.hunger", target, 3.0, tol=0.01)
            ok = got is not None and abs(got - target) < 0.01
            detail = f"hunger {hunger_now}->{got} (want {target})"
            if clean_base:
                satv = wait_attr("player.saturation", fsat, 1.5, tol=0.05)
                ok = ok and satv is not None and abs(satv - fsat) <= 0.05
                detail += f" sat={satv} (want {fsat})"
            want_counts = c.inv_counts.get(fid, 0)
            ok = ok and want_counts == 0
            if residue:
                ok = ok and c.inv_counts.get(residue, 0) >= 1
            sweep_fail += 0 if ok else 1
            print(f"{'PASS' if ok else 'FAIL'}: eat {fname}"
                  f"({'+' + str(fval) if ok else ' -> ' + detail})")
    check("food sweep: exact values for every food", sweep_fail == 0,
          f"{sweep_fail} mismatches")

    # --- 8. hunger cap: eating past 20 clamps to 20 ---
    # NOTE: the creative stocking phase pins food to 20 / saturation to 5
    # (creative freeze) — drain AFTER it, or the survival eats below get
    # refused by the full-hunger gate.
    drain_to_zero()
    stock_items(c, [(0, 364, 1), (1, 320, 1), (2, 400, 1),
                    (3, GOLDEN_APPLE, 1), (4, BREAD, 1)])
    drain_to_zero()
    c.mob_equipment(0, 364)
    time.sleep(0.2)
    c.entity_event(9)
    got = wait_attr("player.hunger", 8.0, 3.0, tol=0.01)
    time.sleep(1.8)
    c.mob_equipment(1, 320)
    time.sleep(0.2)
    c.entity_event(9)
    got = wait_attr("player.hunger", 16.0, 3.0, tol=0.01)
    time.sleep(1.8)
    c.mob_equipment(2, 400)
    time.sleep(0.2)
    c.entity_event(9)
    got = wait_attr("player.hunger", 20.0, 3.0, tol=0.01)
    check("hunger cap clamps at 20 (16+8 -> 20)", got == 20.0,
          f"hunger={got}")

    # --- 9. always-edible: golden apple allowed at 20 food ---
    # The pie eat may have landed < 1.6 s ago — wait the cooldown out or
    # the server silently drops this eat.
    time.sleep(1.7)
    c.mob_equipment(3, GOLDEN_APPLE)
    time.sleep(0.3)
    echoes = c.eat_echoes
    c.entity_event(9)
    ate = wait_pred(lambda: c.eat_echoes > echoes, 3.0)
    consumed = wait_pred(lambda: c.inv_counts.get(GOLDEN_APPLE, 0) == 0, 2.0)
    got = wait_attr("player.saturation", 20.0, 1.5, tol=0.05)
    check("golden apple edible at full hunger (always-edible)",
          ate and consumed and c.attrs.get("player.hunger") == 20.0,
          f"hunger={c.attrs.get('player.hunger')} echoes={c.eat_echoes}"
          f" gapple={c.inv_counts.get(GOLDEN_APPLE, 0)}")
    check("saturation capped at 20 (20 + 9.6 -> 20)", got == 20.0,
          f"sat={got}")

    # --- 10. gate re-check: bread refused again at 20 food ---
    time.sleep(1.7)   # the golden apple eat just consumed the window
    before = c.inv_counts.get(BREAD, 0)
    c.mob_equipment(4, BREAD)
    time.sleep(0.3)
    echoes = c.eat_echoes
    c.entity_event(9)
    c.watch(1.5)
    check("gate re-check: bread refused at 20 food",
          c.eat_echoes == echoes and c.inv_counts.get(BREAD, 0) == before,
          f"bread={c.inv_counts.get(BREAD)} echoes={c.eat_echoes}"
          f" (was {echoes})")

    # --- 11. creative: sprinting does not drain hunger ---
    c.say("/gamemode 1")
    time.sleep(0.8)
    c.sprint_action(ACTION_START_SPRINT)
    c.sprint_move(packets=20)
    time.sleep(0.5)
    check("creative sprint does not drain food",
          c.attrs.get("player.hunger") == 20.0,
          f"hunger={c.attrs.get('player.hunger')}")
    c.say("/gamemode 0")
    time.sleep(0.8)

    # --- 12/13. inventory drop + sync scenarios (PC inventory-UI bug) ---
    # Sugar is the marker item: nothing else in this suite stocks or eats
    # it, so the counts below are immune to leftovers from the sweep.
    stock_items(c, [(0, SUGAR, 3), (1, SUGAR, 1)])

    # 14. move within inventory: empty slot 0, fill slot 5 with the same
    # stack. No drop entity may spawn and nothing may be lost.
    c.drops = 0
    c.slot_write(0, 0, 0)          # take the 3 sugar "on cursor"
    time.sleep(0.2)
    c.slot_write(5, SUGAR, 3)      # place them in slot 5
    c.watch(1.0)
    check("inventory move: no phantom drop", c.drops == 0,
          f"drops={c.drops}")
    resync_inventory()
    check("inventory move: item preserved",
          c.inv_counts.get(SUGAR, 0) == 4,
          f"sugar={c.inv_counts.get(SUGAR)}")

    # 15. drag-out drop: empty the slot, then DropItem — the exact PC
    # gesture that used to void the item. The entity must spawn and the
    # stack must survive (floating buffer holds the rest until the
    # window closes).
    c.drops = 0
    c.slot_write(5, 0, 0)          # cursor: 3 sugar
    time.sleep(0.2)
    c.drop_item(SUGAR, 1)          # drop ONE sugar out of the UI
    c.watch(1.0)
    check("drag-out drop: entity spawns", c.drops == 1, f"drops={c.drops}")
    resync_inventory()
    check("drag-out drop: stack not voided (1 in inv + 2 on cursor)",
          c.inv_counts.get(SUGAR, 0) == 1,
          f"sugar={c.inv_counts.get(SUGAR)}")

    # 16. close the window with 2 sugar still "on cursor": the floating
    # leftovers must come back — zero-loss cleanup.
    c.container_close(0)
    c.watch(1.5)
    check("close with cursor item: returned, not lost",
          c.inv_counts.get(SUGAR, 0) == 3 and c.drops == 1,
          f"sugar={c.inv_counts.get(SUGAR)} drops={c.drops}")

    # 17. item creation from nothing: a survival write of an item the
    # client never took out must be rejected + the slot resynced.
    c.drops = 0
    c.slot_writes = []
    c.slot_write(3, DIAMOND_BLOCK, 64)
    c.watch(1.0)
    reverted = any(s == 3 and i != DIAMOND_BLOCK for s, i, _ in c.slot_writes)
    check("creation from nothing: write rejected + slot resynced",
          reverted and c.drops == 0,
          f"reverts={c.slot_writes} drops={c.drops}")
    check("creation from nothing: inventory untouched",
          DIAMOND_BLOCK not in c.inv_counts,
          f"inv={c.inv_counts.get(DIAMOND_BLOCK, 0)}")

    # 18. plain Q drop (UI closed): still served from the inventory.
    c.drops = 0
    resync_inventory()
    before = c.inv_counts.get(SUGAR, 0)
    c.drop_item(SUGAR, 1)
    c.watch(1.0)
    resync_inventory()
    check("plain Q drop works (no UI open)",
          c.drops == 1 and c.inv_counts.get(SUGAR, 0) == before - 1,
          f"drops={c.drops} sugar={c.inv_counts.get(SUGAR)} (was {before})")

    # --- 12. respawn reset: food/sat/exhaustion back to defaults ---
    ok = c.reset_state()
    check("respawn reset (kill + respawn)", ok)
    if ok:
        check("respawn food == 20", c.attrs.get("player.hunger") == 20.0,
              f"hunger={c.attrs.get('player.hunger')}")
        check("respawn saturation == 5",
              abs((c.attrs.get("player.saturation") or 0) - 5.0) < 0.01,
              f"sat={c.attrs.get('player.saturation')}")
        check("respawn exhaustion == 0",
              (c.attrs.get("player.exhaustion") or 0) == 0.0,
              f"exh={c.attrs.get('player.exhaustion')}")

    # --- 13. persistence: disconnect + reconnect ---
    p = hunger_login(host, port, "HungerPersist")
    if not p:
        check("persistence login", False)
    else:
        p.reset_state()
        # stock marker items, then set a specific food level
        stock_items(p, [(0, BREAD, 2), (1, GOLDEN_CARROT, 1)])
        drain_to_zero(p)
        p.mob_equipment(0, BREAD)
        time.sleep(0.3)
        p.entity_event(9)
        got = None
        deadline = time.time() + 4.0
        while time.time() < deadline:
            got = p.attrs.get("player.hunger")
            if got == 5.0:
                break
            p.watch(0.3)
        check("persistence setup: food set to 5", got == 5.0, f"hunger={got}")

        # disconnect (socket close -> the duplicate-name login kicks the
        # old session, which saves first), then reconnect with the same
        # name and compare.
        p.sock.close()
        time.sleep(2.0)
        p2 = hunger_login(host, port, "HungerPersist")
        if not p2:
            check("persistence reconnect", False)
        else:
            # doFirstSpawn pushes all attributes; the respawn-snap may
            # also be needed if the spawn push is missed.
            deadline = time.time() + 10.0
            hunger = None
            while time.time() < deadline:
                hunger = p2.attrs.get("player.hunger")
                if hunger is not None and hunger < 20.0:
                    break
                p2.watch(0.4)
            check("persistence: food level restored (5)",
                  hunger == 5.0, f"hunger={hunger}")
            sat = p2.attrs.get("player.saturation")
            check("persistence: saturation restored (6.0)",
                  sat is not None and abs(sat - 6.0) <= 0.05, f"sat={sat}")
            bread_ok = wait_pred(lambda: p2.inv_counts.get(BREAD, 0) == 1, 6.0)
            carrot = p2.inv_counts.get(GOLDEN_CARROT, 0)
            check("persistence: items restored", bread_ok and carrot == 1,
                  f"bread={p2.inv_counts.get(BREAD)} gcarrot={carrot}")

    print(f"\n{'FAILED: ' + ', '.join(failures) if failures else 'ALL HUNGER CHECKS PASSED'}")
    return 1 if failures else 0


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19132)
    args = ap.parse_args()
    sys.exit(run(args.host, args.port))
