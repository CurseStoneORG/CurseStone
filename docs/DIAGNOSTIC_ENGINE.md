# CurseStone Diagnostic Engine

The console and diagnostics layer of CurseStone v1.0.3. It borrows
the error-reporting ideas of modern compilers (GCC, Clang, Rust): the console
stays minimal while everything is fine, and the moment something fails it
renders a precise report — what failed, where, why, and how to fix it.

## Identity

The boot log opens with the stage line; the full version details (Minecraft
PE target and protocol) ride `DEBUG` so the normal console never repeats
them:

```
[17:14:40] INFO  Starting server in CurseStone v1.0.3...
```

```text
DEBUG: CurseStone v1.0.3 — Minecraft PE 0.15.x, protocol 84
DEBUG: Log file: ./server.log
```

## Message levels

| Console tag | Meaning |
| ----------- | ------- |
| `INFO`  | normal server messages |
| `CHAT`  | game chat mirror (joins, leaves, broadcasts) — never re-labeled `INFO` |
| `WARN`  | recoverable problems |
| `ERROR` | operations that failed |
| `FATAL` | critical errors that stop the boot |
| `DEBUG` | developer detail — visible only with `settings.debug-mode: true` |

Console lines carry the timestamp and a level tag:

```
[17:14:40] INFO  Loading worlds...
[17:14:40] ERROR Failed to load plugin 'ExamplePlugin2'
```

`server.log` keeps the long-standing bracketed format
(`[17:14:40] [INFO] ...`) so scripts and `grep` keep working; it is always
plain text (no ANSI codes).

## Startup stages

```
[17:14:40] INFO  Starting server in CurseStone v1.0.3...
[17:14:40] INFO  Loading config files...
[17:14:40] INFO  + server.properties loaded!
[17:14:40] INFO  + cursestone.yml loaded!
[17:14:40] INFO  Config files loaded!
[17:14:40] INFO  Network listening...
[17:14:40] INFO  listening on 0.0.0.0:19132
[17:14:40] INFO  Loading worlds...
[17:14:40] INFO  "world" loaded!
[17:14:40] INFO  Worlds loaded!
[17:14:40] INFO  Loading plugins...
[17:14:40] INFO  + WorldEditDemo v1.2.0 Enabled!
[17:14:40] INFO  Plugins loaded
[17:14:40] INFO  Server ready in 0.219s!
```

The `+` mark is reserved for successful configuration and plugin loads.
Worlds load with their quoted name, no `+`. After the ready line one blank
line separates the boot block from runtime events; a blank line also precedes
a player's leave message, closing that session block.

Configuration detail (ports, MOTD, generator, pacing budgets...) lives in
`DEBUG` — enable `settings.debug-mode` to see it. The same rule covers every
trace that left the normal console: session open/close (`[RakLib]`), the
`[NET] Session closed` statistics block, inventory/recipe sync lines,
"Closing player" teardown detail, the ops.txt count and plugin `print()`
output. Warnings and errors always print.

When recoverable problems were reported, the boot ends with:

```
[17:14:40] WARN  Startup completed with 2 issues.
```

No summary is printed when there was nothing to report.

## Diagnostics

Failures render immediately (the boot keeps going when it is safe):

```
[17:14:40] ERROR Failed to load plugin 'ExamplePlugin2'

  --> plugins/ExamplePlugin2/plugin.yml:4

  error[E1002]: incompatible plugin API version

   |
 4 | api: 0.8.0
   |      ^^^^^ expected API version 1.0.3
   |

  help: This plugin requires an older API version.
  help: Update the plugin to support API 1.0.3.
```

Rules:

- Locations (`-->`, snippet, carets) come only from REAL file scans
  (`DiagEngine::findKeySpan`); nothing is ever invented. When no location is
  known, the block renders without one.
- A failing plugin or secondary world never stops the boot; the remaining
  ones keep loading.
- An essential config file that exists but cannot be read is FATAL
  (E0001), and so are a failed network bind (E3001) and a failed default
  world (E2004): the diagnostic prints, then `Startup aborted.` and the
  process exits non-zero so supervisors (systemd, Docker, panels) see it.
- Client-side crashes render as runtime diagnostics (E5002, no snippet) and
  keep the full wire-tail report under `crashdumps/`.

## Diagnostic codes

| Code | Category | Description |
| ------ | ---------------- | --------------------------------------------- |
| E0001 | Configuration | configuration file cannot be read |
| E0002 | Configuration | configuration file is corrupt |
| E0003 | Configuration | configuration value is invalid |
| E0004 | Configuration | required configuration key is missing |
| E1001 | Plugins | plugin.yml missing or unreadable |
| E1002 | Plugins | incompatible plugin API version |
| E1003 | Plugins | invalid plugin manifest field |
| E1004 | Plugins | plugin entry script missing |
| E1005 | Plugins | plugin script failed to parse (Lua syntax) |
| E1006 | Plugins | plugin failed during load/enable |
| E1007 | Plugins | duplicate plugin identity |
| E1008 | Plugins | native plugin library failed to load |
| E2001 | Worlds | invalid world name |
| E2002 | Worlds | unknown world generator |
| E2003 | Worlds | world data missing or unreadable |
| E2004 | Worlds | world failed to load |
| E3001 | Network | network interface failed to bind |
| E3002 | Network | address or port already in use |
| E3003 | Network | invalid network address configuration |
| E4001 | Blocks and Items | block id registration collision |
| E4002 | Blocks and Items | invalid item definition |
| E5001 | Internal | unhandled internal exception |
| E5002 | Internal | client session lost unexpectedly (client crash) |
| E5003 | Internal | internal memory guard tripped |

## Configuration

`cursestone.yml`:

```yaml
settings:
  debug-mode: false      # true = reveal DEBUG lines (packet traces, [NET]...)
  console-color: auto    # auto | always | never
```

`console-color`:

- `auto` (default): colors when stdout is a real terminal.
- `always`: force ANSI even when piped (some Android host apps need this).
- `never`: plain text everywhere.

## Performance notes

- Logging is asynchronous: producers only stamp and copy into a ring buffer;
  a worker does all I/O. Every drain cycle ends in one write per target and
  an immediate flush — console commands echo in well under a tick (measured
  ~46 ms end-to-end for `help`, which is the tick cadence of the command
  executor itself; the old batching window added up to 500 ms on top).
- Log line timestamps are taken when the line is PRODUCED, not when the
  worker formats it, so a burst shows real times.
- The ring drops entries only when it is full (2048 entries of 2 KB) and
  prints `[LOG DROPPED xN]` once when that happens.
