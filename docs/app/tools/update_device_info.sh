#!/usr/bin/env bash
# Rebuilds js/device_info.js (the simulator's copy of the firmware's INFO
# JSON) from the C firmware sources in ../../Firmware/C.
set -euo pipefail
APP="$(cd "$(dirname "$0")/.." && pwd)"
FW="${FW:-$APP/../../Firmware/C}"
SDK="${PICO_SDK_PATH:-$FW/toolchain/pico-sdk}"
TMP="$(mktemp -d)"
gcc -O1 -I"$FW/src" -I"$SDK/src/common/pico_base_headers/include" \
    -o "$TMP/info_json" "$FW/tests/info_json.c" "$FW/src/settings.c" -lm
"$TMP/info_json" > "$APP/tests/device_info.json"
python3 - "$APP" <<'PY'
import json, sys
app = sys.argv[1]
info = json.load(open(f"{app}/tests/device_info.json"))
open(f"{app}/js/device_info.js", "w").write(
    "// Settings description exactly as the firmware sends it (INFO packet),\n"
    "// used by the simulator. Regenerate with tools/update_device_info.sh after\n"
    "// changing the firmware's settings table.\n"
    "(function (root) {\n  const info = " + json.dumps(info, separators=(",", ":")) + ";\n"
    "  if (typeof module === 'object' && module.exports) module.exports = info;\n"
    "  else root.SP_DEVICE_INFO = info;\n})(typeof self !== 'undefined' ? self : this);\n")
PY
rm -rf "$TMP"
echo "Updated js/device_info.js"
