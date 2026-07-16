#!/usr/bin/env bash
#
# publish-firmware.sh
# --------------------
# Copies your freshly-built can_sim UF2 into the spot the web flasher serves
# as "Official Firmware" (firmware/can_sim.uf2), so you never have to drag it
# by hand. The GitHub-hosted flasher can only fetch committed files, so the
# UF2 has to live at a tracked path -- this script puts it there for you.
#
# Usage:
#   1. In the Arduino IDE: Sketch -> Export Compiled Binary
#      (writes the UF2 under firmware/can_sim/build/<board>/can_sim.ino.uf2)
#   2. Run this script:   ./firmware/publish-firmware.sh
#   3. Commit + push when it prints the git commands.
#
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
build_dir="$here/can_sim/build"
dest="$here/can_sim.uf2"

if [ ! -d "$build_dir" ]; then
    echo "No build folder found at:"
    echo "  $build_dir"
    echo
    echo "Build it first: Arduino IDE -> Sketch -> Export Compiled Binary,"
    echo "then re-run this script."
    exit 1
fi

# Newest can_sim*.uf2 anywhere under the build tree (handles the board-named
# subfolder and the can_sim.ino.uf2 naming without you typing the path).
built="$(find "$build_dir" -name '*.uf2' -type f -print0 2>/dev/null \
         | xargs -0 ls -t 2>/dev/null | head -n 1 || true)"

if [ -z "${built:-}" ]; then
    echo "No .uf2 found under $build_dir."
    echo "Run Sketch -> Export Compiled Binary in the Arduino IDE first."
    exit 1
fi

cp "$built" "$dest"

echo "Published official firmware:"
echo "  from: ${built#"$here"/}"
echo "  to:   ${dest#"$here"/}"
echo
echo "Next, commit and push so the hosted flasher serves it:"
echo "  git add firmware/can_sim.uf2"
echo "  git commit -m 'Update official firmware'"
echo "  git push"
