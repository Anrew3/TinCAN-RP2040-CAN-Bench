#!/usr/bin/env python3
"""Validate TinCAN vehicle templates against the schema and the catalog manifest.

Runs in CI on every PR that touches templates/. Checks, in order:

  1. templates/index.json parses and only references ids that exist on disk.
  2. Every official id resolves to templates/<id>.json; every community id to
     templates/community/<id>.json.
  3. Each template file's "id" field matches its filename and its manifest tier.
  4. Every template validates against templates/schema.json (draft-07).
  5. No stray *.json template sits unreferenced by the manifest.

Exit code is non-zero if any check fails, with a human-readable report.

Dependencies: jsonschema (pip install jsonschema). Standard library otherwise.
"""

import json
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
TEMPLATES_DIR = REPO_ROOT / "templates"
COMMUNITY_DIR = TEMPLATES_DIR / "community"
SCHEMA_PATH = TEMPLATES_DIR / "schema.json"
MANIFEST_PATH = TEMPLATES_DIR / "index.json"

# Template files that are not vehicle templates and should be skipped.
IGNORED_FILES = {"schema.json", "index.json", "_blank.json"}


def load_json(path: Path):
    with path.open("r", encoding="utf-8") as fh:
        return json.load(fh)


def main() -> int:
    errors: list[str] = []
    warnings: list[str] = []

    # --- Load schema + validator -------------------------------------------
    try:
        from jsonschema import Draft7Validator
    except ImportError:
        print("ERROR: jsonschema is not installed (pip install jsonschema)", file=sys.stderr)
        return 2

    try:
        schema = load_json(SCHEMA_PATH)
    except (OSError, json.JSONDecodeError) as e:
        print(f"ERROR: cannot read schema {SCHEMA_PATH}: {e}", file=sys.stderr)
        return 2
    validator = Draft7Validator(schema)

    # --- Load manifest ------------------------------------------------------
    try:
        manifest = load_json(MANIFEST_PATH)
    except (OSError, json.JSONDecodeError) as e:
        print(f"ERROR: cannot read manifest {MANIFEST_PATH}: {e}", file=sys.stderr)
        return 2

    official_ids = manifest.get("official", [])
    community_ids = manifest.get("community", [])

    if not isinstance(official_ids, list) or not isinstance(community_ids, list):
        print("ERROR: index.json 'official'/'community' must be arrays", file=sys.stderr)
        return 2

    dup = set(official_ids) & set(community_ids)
    if dup:
        errors.append(f"ids listed in both official and community: {sorted(dup)}")

    # --- Map each manifest id to its expected file --------------------------
    expected = {}  # id -> (path, tier)
    for tid in official_ids:
        expected[tid] = (TEMPLATES_DIR / f"{tid}.json", "official")
    for tid in community_ids:
        expected[tid] = (COMMUNITY_DIR / f"{tid}.json", "community")

    # --- Validate each referenced template ----------------------------------
    for tid, (path, tier) in expected.items():
        if not path.exists():
            errors.append(f"[{tier}] manifest id '{tid}' has no file at {path.relative_to(REPO_ROOT)}")
            continue
        try:
            data = load_json(path)
        except json.JSONDecodeError as e:
            errors.append(f"[{tier}] {path.relative_to(REPO_ROOT)} is not valid JSON: {e}")
            continue

        if data.get("id") != tid:
            errors.append(
                f"[{tier}] {path.relative_to(REPO_ROOT)}: 'id' is "
                f"'{data.get('id')}' but manifest/filename says '{tid}'"
            )

        schema_errors = sorted(validator.iter_errors(data), key=lambda e: e.path)
        for se in schema_errors:
            loc = "/".join(str(p) for p in se.path) or "(root)"
            errors.append(f"[{tier}] {path.relative_to(REPO_ROOT)} @ {loc}: {se.message}")

    # --- Catch stray unreferenced template files ----------------------------
    for path in sorted(TEMPLATES_DIR.glob("*.json")):
        if path.name in IGNORED_FILES:
            continue
        tid = path.stem
        if tid not in official_ids:
            warnings.append(
                f"templates/{path.name} exists but is not listed in index.json 'official'"
            )
    for path in sorted(COMMUNITY_DIR.glob("*.json")):
        if path.name in IGNORED_FILES:
            continue
        tid = path.stem
        if tid not in community_ids:
            warnings.append(
                f"templates/community/{path.name} exists but is not listed in index.json 'community'"
            )

    # --- Report -------------------------------------------------------------
    checked = len(expected)
    if warnings:
        print("Warnings:")
        for w in warnings:
            print(f"  - {w}")
        print()

    if errors:
        print("Template validation FAILED:")
        for e in errors:
            print(f"  ✗ {e}")
        print(f"\n{len(errors)} error(s) across {checked} template(s).")
        return 1

    print(f"✓ All {checked} template(s) valid and consistent with index.json.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
