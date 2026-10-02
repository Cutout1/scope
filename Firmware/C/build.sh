#!/usr/bin/env bash
# Build the firmware. Output: ./scopocket.uf2
# Uses ./toolchain (a folder, or a symlink to an existing toolchain);
# downloads one with scripts/setup_toolchain.sh if it's missing.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
TC="$ROOT/toolchain"
[ -d "$TC/pico-sdk" ] || "$ROOT/scripts/setup_toolchain.sh"

# The USB link needs the SDK's TinyUSB submodule.
if [ ! -f "$TC/pico-sdk/lib/tinyusb/src/tusb.h" ]; then
  git -C "$TC/pico-sdk" submodule update --init --depth 1 lib/tinyusb
fi

export PATH="$TC/arm-gnu-toolchain/bin:$TC/cmake/bin:$PATH"
export PICO_SDK_PATH="$TC/pico-sdk"
export PICO_TOOLCHAIN_PATH="$TC/arm-gnu-toolchain"

cmake -S "$ROOT" -B "$ROOT/build" -G "Unix Makefiles" \
      -DCMAKE_BUILD_TYPE=Release \
      -Dpicotool_DIR="$TC/picotool-install/picotool"
cmake --build "$ROOT/build" -j"$(nproc)"
cp "$ROOT/build/scopocket.uf2" "$ROOT/scopocket.uf2"
echo "Built $ROOT/scopocket.uf2"
