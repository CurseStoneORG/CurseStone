# Mandatory code hygiene rules

These are enforceable. They complement `docs/STYLE_COMMENTS.md`; where both speak, follow the stricter one.

## Rule A — No diagnostic comments
Never add debugging notes, investigation logs, test results, "fixed X" narration or conversational text as source comments. Do not narrate obvious code.

Forbidden: `// Fixed the bug after investigating the inventory system.` / `// Claude tested this and it works.` / `// Changed because the previous version failed.`

Explanations of completed work go in `changes.md` or the commit description. State only the *current* invariant or constraint when a comment is warranted.

Existing diagnostic-style comments (for example "ROOT CAUSE FIX" blocks in `src/CurseStone.cpp` and `src/Server.h`) are legacy. Do not mass-edit them. Rewrite one only when your task changes that code, converting it to a statement of the current constraint and keeping the technical reason.

## Rule B — English-only new comments
All new line, block, Doxygen and TODO comments are in English (American English per `docs/STYLE_COMMENTS.md`). Never add Spanish. Do not translate existing Spanish comments unless the task requires it; `docs/STYLE_COMMENTS.md` also forbids automated or AI translation of existing comments, so leave them for human review.

## Rule C — Short comments
Ordinary comments fit on one line when practical. No paragraphs, no implementation history, no decorative separators. Doxygen may use several concise lines for parameters, returns, ownership/threading and contracts.

## Rule D — Doxygen
Document every new **public and protected** class, struct, enum, function, macro and non-obvious data member (repo convention; stricter than "public only"). Use `@brief`, `@param`, `@return`, `@note`, `@warning` only when applicable; document ownership, lifetime and threading when relevant. Single-line summaries may use `///`; multi-line uses `/** ... */` (both occur in the tree). Never describe behavior the code does not implement. Private details need docs only when non-obvious. Trivial overrides use `@copydoc`.

New files keep the project header: the license banner, exactly one `// SPDX-License-Identifier: LGPL-3.0-or-later`, then a one- or two-sentence summary. Copy the banner from a neighboring file; do not alter its text.

## Rule E — Incomplete implementations
If functionality genuinely cannot be completed in the task, place this exact marker at the unfinished spot:

`//TODO: Put what's missing here`

and, when the missing work is known, extend it: `//TODO: Put what's missing here - persist hunger state across reconnects.` Do not use it on complete code or as a substitute for work that can be finished now. Record every TODO in `changes.md`. Never report an incomplete system as complete.

Note: the tree already holds about 50 `// TODO:` comments in another form, and `docs/STYLE_COMMENTS.md` recommends Doxygen `@todo`. Leave existing ones alone. For new gaps use the marker above and do not also add `@todo` for the same gap. This conflict is listed in `.claude/README.md`.
