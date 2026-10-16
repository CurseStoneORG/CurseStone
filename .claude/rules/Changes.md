# Maintaining `changes.md`

`changes.md` at the repository root records actual modifications from development sessions. It is not the Git history.

1. Record what really changed, concisely. Not intentions, not the conversation, not the process.
2. Use only relevant categories: Added, Feat, Fixed, Changed, Improved, Tests, TODO, Pending, Removed, Compatibility.
3. Keep verified results separate from unverified work. Write `Build: Passed` or `Tests: Passed` only when the command ran and succeeded; otherwise write what was not run and why.
4. List every `//TODO: Put what's missing here` you added under TODO, and unfinished work under Pending. Never mark pending work as done.
5. Preserve existing history; add a new dated section at the top for each meaningful task.
6. Update it when a meaningful task completes, as part of the final review.
