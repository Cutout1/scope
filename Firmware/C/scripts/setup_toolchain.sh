#!/usr/bin/env bash
# Downloads everything needed to build the firmware into ./toolchain
# (ARM GNU toolchain, CMake, pico-sdk, picotool). No system-wide changes.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TC="$ROOT/toolchain"
mkdir -p "$TC"
cd "$TC"

ARM_VER=14.2.rel1
CMAKE_VER=3.31.6
SDK_TAG=2.1.1
PICOTOOL_TAG=2.1.1

if [ ! -x "arm-gnu-toolchain/bin/arm-none-eabi-gcc" ]; then
  echo "== ARM GNU toolchain $ARM_VER"
  curl -L --fail -o arm.tar.xz \
    "https://developer.arm.com/-/media/Files/downloads/gnu/$ARM_VER/binrel/arm-gnu-toolchain-$ARM_VER-x86_64-arm-none-eabi.tar.xz"
  mkdir -p arm-gnu-toolchain
  tar -xf arm.tar.xz -C arm-gnu-toolchain --strip-components=1
  rm arm.tar.xz
fi

if [ ! -x "cmake/bin/cmake" ]; then
  echo "== CMake $CMAKE_VER"
  curl -L --fail -o cmake.tar.gz \
    "https://github.com/Kitware/CMake/releases/download/v$CMAKE_VER/cmake-$CMAKE_VER-linux-x86_64.tar.gz"
  mkdir -p cmake
  tar -xzf cmake.tar.gz -C cmake --strip-components=1
  rm cmake.tar.gz
fi

if [ ! -d "pico-sdk" ]; then
  echo "== pico-sdk $SDK_TAG"
  git clone --depth 1 --branch "$SDK_TAG" https://github.com/raspberrypi/pico-sdk.git
fi
# TinyUSB (USB link to the companion app)
if [ ! -f "pico-sdk/lib/tinyusb/src/tusb.h" ]; then
  echo "== TinyUSB"
  git -C pico-sdk submodule update --init --depth 1 lib/tinyusb
fi

if [ ! -x "picotool-install/picotool/picotool" ] && [ ! -x "picotool-install/bin/picotool" ]; then
  echo "== picotool $PICOTOOL_TAG"
  [ -d picotool ] || git clone --depth 1 --branch "$PICOTOOL_TAG" https://github.com/raspberrypi/picotool.git
  "$TC/cmake/bin/cmake" -S picotool -B picotool/build \
    -DPICO_SDK_PATH="$TC/pico-sdk" -DCMAKE_INSTALL_PREFIX="$TC/picotool-install" \
    -DPICOTOOL_FLAT_INSTALL=1 -DPICOTOOL_NO_LIBUSB=1
  "$TC/cmake/bin/cmake" --build picotool/build -j"$(nproc)"
  "$TC/cmake/bin/cmake" --install picotool/build
fi
echo "Toolchain ready in $TC"
