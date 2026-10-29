# CurseStone — Audit Test Results

Companion to [`memory-audit.md`](memory-audit.md) and [`ownership-map.md`](ownership-map.md).
Every result below was produced on `stable` @ `1c2e94d` + the audit working tree. Nothing here is
inferred: each section states what ran, where the evidence is, and how to reproduce it.

**Status legend:** PASS (verified green) / FAIL (verified red) / N/E (not executable here, reason
given). Counts are never extrapolated.

---

## 1. Environment

| Item | Value |
|---|---|
| OS / compiler | Linux sandbox, gcc 14.2.0 (C: `-std=c2x`, C++: `-std=c++20`, `-pthread`, links `-lz -ldl -lm`) |
| Build system | `CMakeLists.txt` + `build_local.sh` exist; **cmake is not installed** in the sandbox, so builds used `scripts/cs_build.sh` / `scripts/cs_build_san.sh` which replicate `build_local.sh` flags exactly (verified against the script) |
| Not available | cmake, valgrind, cppcheck, clang/clang-tidy (no sudo to install) — recorded, worked around |
| Production build config | **untouched**; sanitizer and warning builds use separate output trees |
| Test clients | project's own Python 0.15.10 protocol tooling in `tools/` (no official client available in sandbox) |

## 2. Builds

| Build | Command | Result |
|---|---|---|
| Baseline (`-O2`) | `bash scripts/cs_build.sh` | **PASS** — `built /tmp/cs_build/CurseStone (4048192 bytes)`; only diagnostic: Lua's `tmpnam` link warning (third-party, pre-existing) |
| Sanitizer (`-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all`) | `bash scripts/cs_build_san.sh` | **PASS** — `built /tmp/cs_build_san/CurseStone (107134800 bytes)`, 0 build errors |
| Warnings (`-Wall -Wextra`, compile-only) | `bash scripts/warn_build.sh` | **PASS** (compiles clean) — 677 diagnostics, bucketed in §3 |

The audit diff (29 files, +315/−68) compiles and links under both configurations. Baseline size
moved 4,034,168 → 4,048,192 bytes (+14 KB of hardening).

## 3. Static analysis

`-Wall -Wextra` full pass (production uses `-w`, so these were previously invisible):

| Diagnostic | Count | Verdict |
|---|---|---|
| `missing-field-initializers` — `CraftingManager.cpp` partial `ItemStack{a,b}` init | 672 | benign pattern (value-init completes the rest); documented as style pass, not fixed |
| `unused variable 'col'` — `PerlinNoise.cpp` | 1 | dead code, documented, not fixed (out of audit's minimal-fix scope) |
| `unused parameter` ×3 — `Normal.cpp`, `PlayerInventory.cpp` | 3 | dead code, documented, not fixed |
| `comparison will always evaluate as true` (`-Waddress`) — `AnvilChunkCodec.c` | 1 | **real finding**: dead null-check on an array member's address; code after it is correct → documented as suspected/cosmetic in memory-audit.md §11 |

Tools not executable here: cppcheck, clang-tidy, clang static analyzer, valgrind (not installed,
no sudo). The static-analysis leg is therefore compiler diagnostics + manual review; that
limitation is restated in memory-audit.md §12.

## 4. Sanitizer runs (ASan + UBSan + LSan)

### 4.1 Discovery run (first pass, same audit)
- UBSan **flagged live** `signed integer overflow: 128 * 73856093` at `Normal.cpp:408` during
  normal world generation (random-seed spawn). → became fix **U1** (same family fixed in 3 more
  files by inspection). After the fix: **0 `runtime error` lines in the entire sanitizer log**.
- One operational note: under ASan, world generation is slow enough that a 30 s SIGTERM grace
  window once expired before the shutdown finished — the run budget was raised; not a server
  defect (the server was *still saving*, which is correct behavior).

### 4.2 Final-state leak verification (fresh re-run on the final tree)
Script: `scripts/leak_run.sh` (starts the sanitizer build, waits for readiness, drives churn,
sends SIGINT, extracts sanitizer reports). Workload:
- `python3 tools/memory_audit_checks.py --host 127.0.0.1 --port 19132 --name LeakAud --cycles 20`
  → 20 × (connect → invalid-name login → **server kick** → session teardown) — PASS (`CHURN_EXIT=0`)
- `--only split_mismatch` → split-reassembly attack flood + forced close — PASS (`SPLIT_EXIT=0`)
- clean SIGINT stop → `SERVER_EXIT=0`, log ends `Shutdown complete.` (`/tmp/cs_leak_run/san_out.log`)

| Check | Result |
|---|---|
| UBSan `runtime error` lines in full server log | **0** |
| LeakSanitizer report blocks in full server log | **0 leaks** |
| ASan error reports (use-after-free, overflow, etc.) | **0** |

### 4.3 Red/green experiment proving the harness detects the leak (fix C1)
Performed during the audit's first verification pass, one-line revert of
`mcp_rak_session_destroy()` on the kick path, identical workload:
- **Red run:** LeakSanitizer reported **1490 bytes leaked in 21 allocations — exactly one
  allocation per kicked session** (20 churn cycles + 1 forced close). This is the per-session
  leak signature predicted by code reading; the harness reproduces it deterministically.
- **Green run (fix restored, rebuilt): 0 leaks.** Fix then kept; final re-verification in §4.2.

This satisfies the "the test must be able to fail" requirement: the leak tool demonstrably
discriminates the defect.

## 5. Malformed-input / lifecycle stress suite

Tool: `tools/memory_audit_checks.py` (new in this audit, 284 lines; reuses the project's Python
protocol client). **7 scenarios, 9 assertions.** Run against the **fixed baseline build**:

Script: `scripts/suite_run.sh` → `python3 tools/memory_audit_checks.py --host 127.0.0.1 --port 19132 --name SuiteAud`

| # | Scenario | What it does | Assertion(s) | Result |
|---|---|---|---|---|
| 1 | `playerlist_bomb` | `PlayerListPacket` with hostile entry count (previously reached `reserve()`) | server still answers pings | **PASS** |
| 2 | `zlib_bomb` | Batch(0x06) whose zlib stream inflates to 12 MiB | server still answers pings **+ fresh client can log in after** | **PASS** (2) |
| 3 | `nbt_depth_bomb` | deeply nested NBT payload (previously unbounded recursion) | server still answers pings | **PASS** |
| 4 | `far_remove_block` | `RemoveBlock` flood for far-from-spawn coordinates (previously forced chunk generation) | server still answers pings | **PASS** |
| 5 | `split_mismatch` | split fragments that change `split_count` mid-message + forced close (previously wrote segments past the slot bound) | server still answers pings | **PASS** |
| 6 | `invalid_name_churn` | 12 × connect → invalid-name login → server kick (teardown path churn) | valid login afterwards **+ server answers pings** | **PASS** (2) |
| 7 | `ocr_flood` | 150 unauthenticated OCR1 datagrams in a burst (session-creation pressure) | server still answers pings | **PASS** |

**Result: ALL SCENARIOS PASSED**, then SIGINT → `SERVER_EXIT=0`, `Shutdown complete.`
Full log: `/tmp/cs_suite_run/base_out.log`.

### Test-development failure recorded honestly
During suite development (first pass), `zlib_bomb`'s "fresh-client login after bomb" assertion
**failed**: analysis showed the server had *intentionally* closed the abusive session ("bad
batch" response — correct behavior), and the test had tried to reuse the same client. The test
was corrected to expect a fresh client login; the server behavior was verified correct and was
**not** changed. No failing test was deleted or ignored.

## 6. Gameplay regression tests (existing e2e tools)

All six existing suites, run against the fixed baseline (tools spawn their own server):

| Suite | Result |
|---|---|
| `metadata_spawn_check.py` | **ALL CHECKS PASSED** |
| `combat_kb_hurt_check.py` | **ALL PASSED** |
| `multiplayer_bow_check.py` | **ALL PASSED** (invoked with `--server /tmp/cs_build/CurseStone`; the tool's hardcoded default path is stale) |
| `arrow_creative_flash_check.py` | **ALL CHECKS PASSED** |
| `mp_bow_death_arrow_check.py` | **ALL CHECKS PASSED** (`--server` as above) |
| `bow_stack_check.py` | **ALL CHECKS PASSED** (`--server` as above) |

No gameplay regressions from the 30 audit fixes, including the login-path (`kick`) and
block-break (`getChunkIfLoaded`) changes.

## 7. Repeated-lifecycle stress evidence (summary)

- **Player connect/disconnect ×20** under LeakSanitizer → 0 leaks (§4.2), red/green-proven (§4.3).
- **Invalid-name churn ×12** in the suite → subsequent valid login OK (§5 #6).
- **World load → full play area → SIGINT → save → clean shutdown**: every suite and leak run ends
  in a graceful shutdown with `EXIT 0` and no sanitizer findings (world save lines present in logs).
- **Chunk load/unload**: exercised implicitly by every login (spawn chunks) and by the GC sweep
  in the forced final GC; no sanitizer findings.
- Not exercised (and why): world unload/reload via `/unload`-style commands with in-flight
  generation (the drain-guard path) — no second world was available in the sandbox config used by
  the harness; plugin load/unload cycles — no third-party plugin binary was available; both are
  recorded as N/E rather than claimed.

## 8. Not executed — with reasons

| Item | Status | Reason |
|---|---|---|
| Valgrind Memcheck | N/E | not installed, no sudo; ASan/LSan cover the same classes |
| cppcheck / clang-tidy / clang SCA | N/E | not installed, no sudo |
| ThreadSanitizer | N/E (this pass) | cannot combine with ASan in one binary; separate toolchain unavailable. Concurrency fixes were code-verified (A6, L1, L3) — residual risk accepted and documented |
| Official Minecraft PE 0.15.10 client | N/E | no GUI/client in sandbox; all dynamic tests are wire-level via the project's Python protocol client |
| Performance benchmarks | not claimed | no pre-audit baseline was measured; per audit rules, no performance improvement is claimed without measurement |
| Plugin load/unload stress, second-world unload with in-flight generation | N/E | no suitable plugin/world fixture in harness; unit covered by code review only |

## 9. Reproduction commands

```bash
# builds (config-identical to build_local.sh, isolated output trees)
bash scripts/cs_build.sh          # baseline   -> /tmp/cs_build/CurseStone
bash scripts/cs_build_san.sh      # ASan+UBSan -> /tmp/cs_build_san/CurseStone
bash scripts/warn_build.sh        # -Wall -Wextra diagnostics

# malformed-input / lifecycle suite vs the fixed baseline (spawns its own server)
bash scripts/suite_run.sh
#   = starts /tmp/cs_build/CurseStone, runs:
python3 tools/memory_audit_checks.py --host 127.0.0.1 --port 19132 --name SuiteAud

# LeakSanitizer provocation + report extraction (sanitizer build)
bash scripts/leak_run.sh
#   = starts /tmp/cs_build_san/CurseStone, runs:
python3 tools/memory_audit_checks.py --host 127.0.0.1 --port 19132 --name LeakAud --cycles 20
python3 tools/memory_audit_checks.py --host 127.0.0.1 --port 19132 --name LeakAud --only split_mismatch

# single scenario
python3 tools/memory_audit_checks.py --host 127.0.0.1 --port 19132 --name X --only zlib_bomb

# gameplay regressions (each spawns its own server)
python3 tools/metadata_spawn_check.py
python3 tools/combat_kb_hurt_check.py
python3 tools/multiplayer_bow_check.py --server /tmp/cs_build/CurseStone
python3 tools/arrow_creative_flash_check.py
python3 tools/mp_bow_death_arrow_check.py --server /tmp/cs_build/CurseStone
python3 tools/bow_stack_check.py --server /tmp/cs_build/CurseStone
```

(`scripts/` lives next to the repo checkout, outside the repo tree; the repo itself is unmodified
in its build configuration.)
