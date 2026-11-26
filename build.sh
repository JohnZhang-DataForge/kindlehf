#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "$0")" && pwd)"
cd "$ROOT"

TOOLCHAIN_TAG="2026.08"
CROSS="arm-kindlehf-linux-gnueabihf"
TOOLCHAIN_SHA256="8cc7dfbd71abd78f9e947d6b2e20670288a4402edc7b07176bca791f7eaf87d0"
LIBXNES_COMMIT="45cab8022732542d0a8c6eddd4fb26c393744afe"
FBINK_COMMIT="bab62e96716ef978dab52048eb4d1c79a8eecfa7"
PREFIX="$HOME/x-tools/$CROSS"
ARCHIVE="$ROOT/build/kindlehf.tar.zst"

mkdir -p "$ROOT/build" "$ROOT/dist" "$ROOT/vendor" "$HOME/x-tools"
if [[ ! -x "$PREFIX/bin/$CROSS-gcc" ]]; then
    curl --fail --location --retry 3 \
      "https://github.com/koreader/koxtoolchain/releases/download/$TOOLCHAIN_TAG/kindlehf.tar.zst" \
      -o "$ARCHIVE"
    echo "$TOOLCHAIN_SHA256  $ARCHIVE" | sha256sum --check -
    tar --zstd -xf "$ARCHIVE" -C "$HOME"
fi

export PATH="$PREFIX/bin:$PATH"
CC="$PREFIX/bin/$CROSS-gcc"
STRIP="$PREFIX/bin/$CROSS-strip"
READELF="$PREFIX/bin/$CROSS-readelf"
test -x "$CC"
"$CC" --version | head -n 1
"$CC" -dumpmachine

if [[ ! -d vendor/libxnes/.git ]]; then
    git clone https://github.com/xboot/libxnes vendor/libxnes
fi
git -C vendor/libxnes fetch --depth 1 origin "$LIBXNES_COMMIT"
git -C vendor/libxnes checkout --detach "$LIBXNES_COMMIT"

if [[ ! -d vendor/FBInk/.git ]]; then
    git clone --recurse-submodules https://github.com/NiLuJe/FBInk vendor/FBInk
fi
git -C vendor/FBInk fetch --depth 1 origin "$FBINK_COMMIT"
git -C vendor/FBInk checkout --detach "$FBINK_COMMIT"
git -C vendor/FBInk submodule update --init --recursive

make -C vendor/FBInk cleanstaticlib || true
make -C vendor/FBInk staticlib KINDLE=1 MINIMAL=1 CROSS_TC="$CROSS"

mapfile -t XNES_SOURCES < <(find vendor/libxnes/src -type f -name '*.c' -print | sort)
test "${#XNES_SOURCES[@]}" -gt 0

"$CC" -std=gnu11 -O2 -DNDEBUG -DPLATFORM_KINDLE \
    -march=armv7-a -mtune=cortex-a8 -mfpu=neon -mfloat-abi=hard -mthumb \
    -fomit-frame-pointer -fno-strict-aliasing -ffunction-sections -fdata-sections \
    -Wall -Wextra \
    -Ivendor/FBInk -Ivendor/libxnes/src \
    src/main.c "${XNES_SOURCES[@]}" vendor/FBInk/Release/libfbink.a \
    -static -Wl,--gc-sections -Wl,--no-undefined \
    -lm -lrt -lpthread -ldl \
    -o dist/nes

"$STRIP" --strip-unneeded dist/nes
file dist/nes
"$READELF" -h dist/nes | tee dist/elf-header.txt
"$READELF" -A dist/nes | tee dist/elf-attributes.txt
"$READELF" -l dist/nes | tee dist/elf-program-headers.txt
"$READELF" -d dist/nes | tee dist/elf-dynamic.txt || true

grep -q 'Machine:.*ARM' dist/elf-header.txt
grep -q 'EABI5' dist/elf-header.txt
grep -q 'Tag_ABI_VFP_args: VFP registers' dist/elf-attributes.txt
if grep -q 'INTERP' dist/elf-program-headers.txt; then
    echo "Unexpected dynamic interpreter in supposedly static executable" >&2
    exit 1
fi
if grep -q '(NEEDED)' dist/elf-dynamic.txt; then
    echo "Unexpected dynamic shared-library dependency" >&2
    exit 1
fi

sha256sum dist/nes | tee dist/nes.sha256
echo "KindleHF executable build and ELF ABI checks passed."
