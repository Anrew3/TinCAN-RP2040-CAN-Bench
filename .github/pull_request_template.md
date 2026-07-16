<!--
Thanks for contributing to TinCAN! Fill in the sections that apply and delete
the rest. For a vehicle template, the "New/updated vehicle template" section is
the important one. See CONTRIBUTING.md for the full flow.
-->

## What does this PR do?

<!-- One or two sentences. -->

## Type of change

- [ ] New community vehicle template (`templates/community/<id>.json`)
- [ ] New/updated official template (`templates/<id>.json` + firmware defaults)
- [ ] Firmware change (`firmware/can_sim/`)
- [ ] Web controller / flasher change (`firmware/Controller/`)
- [ ] Docs
- [ ] Other

## New/updated vehicle template

<!-- Delete this section if the PR isn't a template. -->

- **Vehicle:** <!-- make / model / years / platform -->
- **Template id:** <!-- must match the filename and the id inside the JSON -->
- **Tier:** Community / Official
- [ ] Added the file under `templates/` or `templates/community/`
- [ ] Listed the `id` in `templates/index.json` (correct tier)
- [ ] `python scripts/validate_templates.py` passes locally (or CI is green)

## Testing

<!-- What hardware/modules did you test against? SYNC 3/4, IPC, bench only,
     simulator only, etc. What worked, what's untested. -->

## Notes for reviewers

<!-- Anything else worth calling out. -->
