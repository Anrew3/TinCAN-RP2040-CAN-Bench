# Community Templates

Contributor-submitted vehicle templates. Unlike the **official** templates
(`templates/*.json`, curated and compiled into the firmware defaults), these
are **JSON-only** — the web controller loads them and you push one to a device
with **Save to Device** when you select it. They are **not** baked into the
firmware and are **not** individually verified by the maintainers.

## Add one

1. Build it in the web controller's Template Editor, then **Export** (downloads `<id>.json`).
2. Put the file here as `templates/community/<id>.json` (the filename must match the template's `id`).
3. Add `"<id>"` to the `community` array in [`templates/index.json`](../index.json).
4. Open a pull request. CI validates it against [`templates/schema.json`](../schema.json); a maintainer reviews and merges.

Once merged it appears in the controller's vehicle list with a **Community** badge.
See [CONTRIBUTING.md](../../CONTRIBUTING.md) for the full flow.
