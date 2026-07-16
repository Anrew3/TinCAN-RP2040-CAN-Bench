# Contributing to TinCAN

Thanks for helping grow the TinCAN CAN-bus simulator! The most common
contribution is a **vehicle template** — a JSON file that teaches the
controller how to talk to a particular car's modules. This guide covers that
flow, plus firmware/UI changes.

---

## Contributing a vehicle template

Templates come in two tiers:

| Tier | Lives in | Baked into firmware? | Reviewed |
|------|----------|----------------------|----------|
| **Official** | `templates/<id>.json` | Yes (compiled defaults) | Curated + tested by maintainers |
| **Community** | `templates/community/<id>.json` | No — JSON only | Schema-checked in CI, lightly reviewed |

Most contributions start as **community** templates. They load in the web
controller and push to a device with **Save to Device**, without anyone having
to rebuild and re-flash the firmware. A community template that proves reliable
can later be promoted to official.

### 1. Build it in the controller

Open the [TinCAN Controller](https://anrew3.github.io/TinCAN-CANBUS-Simulator/firmware/Controller/can-controller.html),
click **Templates** to open the editor, and either start from an existing
template or from a blank one. Set the CAN IDs, gauges, blinkers, signals, and
background/boot messages for your vehicle. Test against real hardware if you
can.

### 2. Export

Click **Export** in the editor. The browser downloads `<id>.json`, where `<id>`
is your template's `id` field (lowercase, digits, and underscores only — e.g.
`focus_mk3`, `explorer_u625`).

### 3. Drop it in the repo

- Community template → `templates/community/<id>.json`
- Official template (maintainers / promotion) → `templates/<id>.json`

The **filename must match the `id`** inside the file.

### 4. List it in the manifest

Add your `id` to the matching array in
[`templates/index.json`](templates/index.json):

```json
{
  "official": ["mustang_s550", "f150_13gen"],
  "community": ["focus_mk3"]
}
```

The controller reads this manifest to know which templates to load, so a
template that isn't listed here won't appear.

### 5. Open a pull request

Push a branch and open a PR. The **Validate Templates** GitHub Action runs
[`scripts/validate_templates.py`](scripts/validate_templates.py), which:

- checks every manifest id resolves to a file in the right folder,
- checks each file's `id` matches its filename and tier,
- validates each template against [`templates/schema.json`](templates/schema.json),
- flags any template file that isn't referenced by the manifest.

Fix anything it reports, then a maintainer reviews and merges. Once merged, your
template shows up in the controller's vehicle list with a **Community** badge.

### Validate locally (optional)

```bash
pip install jsonschema
python scripts/validate_templates.py
```

The schema itself ([`templates/schema.json`](templates/schema.json)) documents
every field. `templates/_blank.json` is a minimal starting point, and the two
official templates are worked examples.

---

## Firmware and controller changes

The firmware lives in `firmware/can_sim/` (Arduino sketch for the Adafruit
Feather RP2040 CAN). The web controller/flasher is the single-file
`firmware/Controller/can-controller.html`. Build and flashing notes are in
[`firmware/can_sim/README.md`](firmware/can_sim/README.md).

Note that **official** templates exist in two synchronized copies: the JSON in
`templates/` and the compiled C++ defaults in
`firmware/can_sim/DefaultTemplates.h`. If you change an official template, keep
both in sync. Community templates have no firmware copy — they are JSON only.

Please keep PRs focused, describe what hardware you tested against, and match
the surrounding code style.
