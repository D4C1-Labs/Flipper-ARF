#!/usr/bin/env bash
# Assemble recommended SD layout for CVMR-Lab (paths + placeholders).
# Does not download proprietary/community signal dumps automatically.
set -euo pipefail
ROOT="${1:-$PWD/build-sd}"
mkdir -p "$ROOT/ext/subghz" "$ROOT/ext/subghz/playlist" "$ROOT/ext/apps/Sub-GHz" \
  "$ROOT/ext/subghz/Vehicles" "$ROOT/ext/apps_data/kat"
cat >"$ROOT/ext/CVMR-README.txt" <<'EOF'
CVMR-Lab SD bundle layout
- /ext/apps/Sub-GHz/*.fap  — install ProtoPirate, Sibyl, RollLab, car_breaker_19 manually from upstream releases
- /ext/subghz/Vehicles/    — sync kakuzu-f0/Automotive-Sub-Ghz-Collection (decode regression; TX policy per lab SOP)
- /ext/subghz/playlist/    — optional kakuzu Automotive-Sub-Ghz-Playlists (authorized chamber only)
- /ext/apps_data/kat/      — KAT USB tx.sub staging when using Flipper as KAT radio
EOF
echo "Wrote $ROOT/ext/CVMR-README.txt"
echo "Copy this tree to Flipper SD ext/ after flashing CVMR-Lab firmware."
