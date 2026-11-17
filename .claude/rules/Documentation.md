# Documentation policy

- New source comments and Doxygen are in English (American English). Rules A–E in `Rules.md` and `docs/STYLE_COMMENTS.md` apply.
- Be concise and technically accurate; document actual behavior, ownership, threading and protocol constraints, and nothing the code does not do.
- Use the project's Doxygen conventions (`@brief`, `@param`, `@return`, `@note`, `@warning`). `Doxyfile` uses `JAVADOC_AUTOBRIEF`, extracts only non-private documented entities, and excludes `third_party`.
- No debugging transcripts in source. Do not claim functionality is complete without verification.
- When public behavior, the plugin API, config keys, protocol handling or architecture changes, update the relevant docs in the same task: `README.md`, `CONTRIBUTING.md`, `docs/BUILDING.md`, `docs/PLUGINS.md` (plugin API), `.claude/rules/Architecture.md`.
- Do not translate existing documentation or edit unrelated files. `docs/BUILDING.md` is currently Spanish; leave its language unless asked.
- API docs: `doxygen Doxyfile` writes to `build/docs/html/`; missing-doc warnings are currently non-fatal. The `Docs` workflow runs on branch `stable`.

## Where each kind of change is documented
| Change | Update |
|---|---|
| Config key or default | `utils/DefaultConfig.cpp`, docs if user-visible |
| Plugin API (Lua or native) | Doxygen on the binding, `docs/PLUGINS.md`, `Extension-points.md` |
| New platform step or build flag | `docs/BUILDING.md`, `Compatibility.md`, workflow files only if the task is about CI |
| New subsystem or registration point | `.claude/rules/Architecture.md` and `Extension-points.md` |
| Comment/Doxygen conventions | `docs/STYLE_COMMENTS.md` only when the maintainers ask for a convention change |

If a doc contradicts the code, report the discrepancy; fix the doc only when the task covers it.
