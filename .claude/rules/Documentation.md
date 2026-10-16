# Documentation policy

- New source comments and Doxygen are in English (American English). Rules A–E in `Rules.md` and `docs/STYLE_COMMENTS.md` apply.
- Be concise and technically accurate; document actual behavior, ownership, threading and protocol constraints, and nothing the code does not do.
- Use the project's Doxygen conventions (`@brief`, `@param`, `@return`, `@note`, `@warning`). `Doxyfile` uses `JAVADOC_AUTOBRIEF`, extracts only non-private documented entities, and excludes `third_party`.
- No debugging transcripts in source. Do not claim functionality is complete without verification.
- When public behavior, the Lua plugin API, config keys, protocol handling or architecture changes, update the relevant docs in the same task: `README.md`, `CONTRIBUTING.md`, `BUILDING.md`, `.claude/rules/Architecture.md`, and note it in `changes.md`. Plugin API docs live in the separate repo `CurseStoneTeam/cursestone-docs`; flag needed updates there instead of guessing.
- Do not translate existing documentation or edit unrelated files. `BUILDING.md` is currently Spanish; leave its language unless asked.
- API docs: `doxygen Doxyfile` writes to `build/docs/html/`; missing-doc warnings are currently non-fatal. The `Docs` workflow runs on branch `stable`.
- Known doc inconsistencies (report, do not silently fix): `BUILDING.md` refers to `build.yml` but the file is `Build.yml`; `README.md` lists Linux and Android prebuilt artifacts although CI also builds Windows; `Doxyfile` excludes `src/legacy/*`, which does not exist.

## Where each kind of change is documented
| Change | Update |
|---|---|
| Config key or default | `utils/DefaultConfig.cpp`, `README.md` "Configuration", `changes.md` |
| Lua API / events exposed to plugins | Doxygen on the binding, external `cursestone-docs` repo (flag it), `changes.md` |
| New platform step or build flag | `BUILDING.md`, `Compatibility.md`, workflow files only if the task is about CI |
| New subsystem or registration point | `.claude/rules/Architecture.md` and `Extension-points.md` |
| Comment/Doxygen conventions | `docs/STYLE_COMMENTS.md` only when the maintainers ask for a convention change |

Additional discrepancies found in the audit (report, do not silently fix): `CMakeLists.txt` `VERSION 1.0.0` vs `CURSESTONE_API "1.0.1"`; `CurseStone.h` says "Inspired by PocketMine-MP 4.0.0" while the other docs say 0.15.10; `Build.yml` triggers on `main/master/dev` while `docs.yml` triggers on `stable`; README Doxygen link (`CurseStoneTeam.github.com`) vs the docs repo link used elsewhere.
