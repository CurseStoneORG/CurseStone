# Player mechanics events

CurseStone fires a set of gameplay events around the player mechanics that
plugins most often need to hook: exhaustion, hunger, sneaking, sprinting,
animations, game mode switches, the login handshake, player creation and
bucket operations. This document covers the ten events added under
`src/event/player/`; the earlier event surface (join, quit, chat, move,
block, inventory, damage...) is described in [PLUGINS.md](PLUGINS.md).

Every event below lives in `src/event/player/<Name>.h` under the
`cursestone::event::player` namespace, derives from `PlayerEvent` (or the
plain `Event` base when no player object exists yet) and — when noted —
from `Cancellable`. Cancelling is checked by the engine BEFORE the mechanic
is applied, so a cancelled event leaves the world, the inventory and the
player state untouched.

## Listening

From Lua:

```lua
events.on("PlayerExhaust", function(e)
    print(e.player, e.cause, e.amount)
    e.cancel()               -- drops this exhaustion increment
end)
```

From a native plugin (`plugin/NativeAPI.h`):

```c
static void onExhaust(CsEvent* ev, void* ud) {
    if (ev->cause && strcmp(ev->cause, "mining") == 0)
        ev->cancelled = true;             // no mining exhaustion today
}
// inside onLoad:
api->events->on(api, "PlayerExhaust", onExhaust, nullptr);
```

Writable fields per event are listed in the table at the end. Listener
ordering, priorities and error isolation are handled by the shared
`EventManager` — see `src/event/EventManager.h`.

## The events

### PlayerExhaustEvent

| | |
|---|---|
| Fired | BEFORE a player's exhaustion accumulator increases (attack, taking damage, mining, health regeneration, hunger potion effect, sprint movement, swimming). |
| Cancellable | yes — the whole increment is dropped. |
| Fields | `player`; `amount` (writable, exhaustion units the accumulator will receive — non-finite or non-positive values are dropped); `cause` (readable name: `attack`, `damage`, `mining`, `health_regen`, `potion`, `walking`, `sneaking`, `swimming`, `jumping`, `custom`, with a `|sprint` suffix when the sprint modifier applied). |

The cause constants live on the class: `CAUSE_ATTACK` (=1) ... `CAUSE_CUSTOM`
(=11), plus `CAUSE_FLAG_SPRINT` (=0x10000), a bitwise flag OR-ed onto a base
cause — sprint movement reports `CAUSE_WALKING | CAUSE_FLAG_SPRINT`. Read it
back with `baseCause()` and `hasSprintFlag()` (Lua/native listeners just see
the `|sprint` suffix in `cause`).

Exhaustion is its own mechanic: it is NOT hunger and NOT saturation. When
the accumulator crosses 4.0 the server drains saturation (then food); the
resulting food change surfaces as `PlayerHungerChange`, never here.

### PlayerHungerChangeEvent

| | |
|---|---|
| Fired | BEFORE the food level (0..20 drumstick bar) moves from one value to another. |
| Cancellable | yes — the previous level stays. |
| Fields | `player`; `old_level`, `new_level` (writable; the caller clamps into 0..20); `reason` (`eating`, `exhaustion`, `saturation_effect`, `creative_sync`, `login`, `respawn`, `plugin`, `unknown`). |

Re-entrancy is guarded: a listener whose side effects trigger another food
change applies it silently instead of dispatching a nested event, so event
storms are impossible.

### PlayerToggleSneakEvent / PlayerToggleSprintEvent

| | |
|---|---|
| Fired | BEFORE the sneak (or sprint) flag flips — true = starting, false = stopping. |
| Cancellable | yes — the previous state stays and the pose flags are re-broadcast so the client prediction resyncs. |
| Fields | `player`; `sneaking` / `sprinting` (boolean, the new state); native `aux1` = new state, `action` = `start`/`stop`. |

Both are independent mechanics with separate event names; duplicates never
reach listeners (a no-op toggle is swallowed by the state guard before any
dispatch). The vanilla food gate for starting a sprint (food must be above
six) is a server rule applied before the event.

### PlayerAnimationEvent

| | |
|---|---|
| Fired | on every client animation packet (protocol 84 ANIMATE): `swing_arm` (id 1) on punching/mining/placing, `wake_up` (id 3). |
| Cancellable | yes — stops the relay of the animation to the other viewers. Combat logic never consults this event. |
| Fields | `player`; `animation` (raw id), `action` (`swing_arm`, `wake_up`, `unknown`); native `aux1` = raw id. |

### PlayerGamemodeChangeEvent

| | |
|---|---|
| Fired | BEFORE a game mode switch lands (0 survival, 1 creative, 2 adventure — the modes this protocol version supports). |
| Cancellable | yes — the previous mode stays with zero side effects. |
| Fields | `player`; `old_gamemode`, `new_gamemode` (integers); native `aux1`/`aux2` and `cause` = readable mode name. |

No event fires when the requested mode equals the current one.

### PlayerPreLoginEvent

| | |
|---|---|
| Fired | mid-handshake: identity from the login packet is stored (username, uuid, client id, address, port) but the session is NOT logged in yet — before whitelist, bans and player-data loading. |
| Cancellable | yes — the connection is kicked with the configured message (a default reason applies when left empty) and never becomes a connected player. |
| Fields | `player` (username), `ip`, `port`, `client_id`; `message` is the WRITABLE kick message. |

Fires exactly once per login attempt.

### PlayerCreationEvent

| | |
|---|---|
| Fired | inside the network layer right before a `Player` object is constructed for a fresh RakNet session. |
| Cancellable | no — connection rejection belongs to `PlayerPreLoginEvent`. |
| Fields | `ip`, `port`; no player object exists yet (`player` is empty). |

Selecting a custom player implementation is a C++-side capability: install
a factory during the event and the session builds your subclass instead of
the standard player. The factory receives a fresh `PlayerConfig` and must
return a `cursestone::Player` subclass (constructor taking
`Server*, PlayerConfig` like the base); returning null falls back to the
standard implementation with a warning.

```cpp
#include "event/EventManager.h"
#include "event/player/PlayerCreationEvent.h"
#include "Player.h"

class VipPlayer : public cursestone::Player {
  public:
    using Player::Player;   // same (Server*, PlayerConfig) constructor
};

EventBus::listen<event::player::PlayerCreationEvent>(this,
    [](event::player::PlayerCreationEvent& ev) {
        ev.setPlayerFactory([](cursestone::Server* s, cursestone::PlayerConfig&& cfg) {
            return std::make_shared<VipPlayer>(s, std::move(cfg));
        });
    });
```

### PlayerBucketEvent / PlayerBucketFillEvent

| | |
|---|---|
| Fired | BEFORE a bucket operation touches the world. `PlayerBucket` = a filled bucket POURs its liquid (water id 8 / lava id 10) into the target cell; `PlayerBucketFill` = an empty bucket SCOOPS a liquid source cell and fills itself. |
| Cancellable | yes — world, inventory and the held bucket stay untouched (no partial pour, no lost or duplicated buckets) and the client target is resynced. |
| Fields | `player`; `bucket_id`/`bucket_meta`/`bucket_count` (the held BUCKET stack), `liquid_id` (8 water / 10 lava), `x`,`y`,`z` (pour: the cell receiving liquid; fill: the source cell being drained), `action` (`pour`/`fill`). |

`PlayerBucketFillEvent` derives from `PlayerBucketEvent`; register the two
names separately.

## Native `CsEvent` field map

The native snapshot reuses generic slots (`aux1`, `aux2` — appended fields,
zero unless the event documents them) plus the shared `message`/`amount`/
`cancelled`/`cause`/`action` fields:

| Event | aux1 | aux2 | cause | action | writable |
|---|---|---|---|---|---|
| PlayerExhaust | raw cause word | – | cause name | – | `amount` |
| PlayerHungerChange | old level | new level | reason | – | `amount` (new level) |
| PlayerToggleSneak | new state | – | – | start/stop | `cancelled` |
| PlayerToggleSprint | new state | – | – | start/stop | `cancelled` |
| PlayerAnimation | animation id | – | – | animation name | `cancelled` |
| PlayerGamemodeChange | old mode | new mode | mode name | – | `cancelled` |
| PlayerPreLogin | port | – | `ip:port` | – | `message`, `cancelled` |
| PlayerCreation | port | – | `ip:port` | – | – (not cancellable) |
| PlayerBucket / PlayerBucketFill | – | – | – | pour/fill | `cancelled` |

`itemId`/`itemMeta`/`itemCount` carry the bucket stack and `blockId` the
liquid id for both bucket events; `x`/`y`/`z` the target cell.

## Integration notes (server developers)

The fire points, all on the main thread:

| Event | Fire point |
|---|---|
| PlayerExhaustEvent | `Player::addExhaustion(float, int32_t cause)` — every increment |
| PlayerHungerChangeEvent | `Player::setFoodLevel(int, HungerChangeReason)` — every food write |
| PlayerToggleSneakEvent | `Player::setSneaking(bool)` |
| PlayerToggleSprintEvent | sprint branches of the player-action handler |
| PlayerAnimationEvent | `ANIMATE_PACKET` handling in `PlayerNetwork.cpp` |
| PlayerGamemodeChangeEvent | `Player::setGamemode(int)` |
| PlayerPreLoginEvent | `Player::handleLogin` after the server-full check |
| PlayerCreationEvent | `Network::createPlayerForSession` |
| PlayerBucketEvent | bucket pour branch of the block-place handler |
| PlayerBucketFillEvent | bucket scoop branch of the block-place handler |
