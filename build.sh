#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "$0")" && pwd)"
cd "$ROOT"

TOOLCHAIN_TAG="2026.08"
CROSS="arm-kindlehf-linux-gnueabihf"
TOOLCHAIN_SHA256="8cc7dfbd71abd78f9e947d6b2e20670288a4402edc7b07176bca791f7eaf87d0"
LIBXNES_COMMIT="45cab8022732542d0a8c6eddd4fb26c393744afe"
FBINK_COMMIT="bab62e96716ef978dab52048eb4d1c79a8eecfa7"
KINDLE_SDK_COMMIT="b4a6c99d718a7cf74935f36105c62491b4336a61"
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

# Build against the GTK2/GLib libraries shipped in Kindle firmware. The SDK
# extracts the target libraries and headers into the KOX sysroot; no GTK
# libraries are bundled into the installation package.
SDK_DIR="$ROOT/build/kindle-sdk"
SYSROOT="$PREFIX/$CROSS/sysroot"
if [[ ! -d "$SDK_DIR/.git" ]]; then
    git clone --recurse-submodules https://github.com/KindleModding/kindle-sdk "$SDK_DIR"
fi
git -C "$SDK_DIR" fetch --depth 1 origin "$KINDLE_SDK_COMMIT"
git -C "$SDK_DIR" checkout --detach "$KINDLE_SDK_COMMIT"
git -C "$SDK_DIR" submodule update --init --recursive

if [[ ! -f "$SYSROOT/usr/lib/pkgconfig/gtk+-2.0.pc" ]]; then
    # One KindleHF firmware image is sufficient to populate the GTK2 ABI
    # sysroot; use the Scribe 5.16.3 image (same KindleHF ABI generation).
    sed -i 's@Setup_SDK "arm-kindlehf-linux-gnueabihf" "kindlehf" "https://s3.amazonaws.com/firmwaredownloads/update_kindle_all_new_paperwhite_v2_5.16.3.bin" "https://s3.amazonaws.com/firmwaredownloads/update_kindle_scribe_5.16.3.bin"@Setup_SDK "arm-kindlehf-linux-gnueabihf" "kindlehf" "https://s3.amazonaws.com/firmwaredownloads/update_kindle_scribe_5.16.3.bin"@' "$SDK_DIR/gen-sdk.sh"
    (cd "$SDK_DIR" && ./gen-sdk.sh kindlehf "$PREFIX")
fi

test -f "$SYSROOT/usr/lib/pkgconfig/gtk+-2.0.pc"
export PKG_CONFIG_SYSROOT_DIR="$SYSROOT"
export PKG_CONFIG_LIBDIR="$SYSROOT/usr/lib/pkgconfig:$SYSROOT/usr/share/pkgconfig"
GTK_CFLAGS="$(pkg-config --cflags gtk+-2.0)"
GTK_LIBS="$(pkg-config --libs gtk+-2.0)"
for gtk_lib in libgtk-x11-2.0.so.0 libgdk-x11-2.0.so.0 libglib-2.0.so.0 libgobject-2.0.so.0; do
    if ! find "$SYSROOT/lib" "$SYSROOT/usr/lib" -name "$gtk_lib" -print -quit 2>/dev/null | grep -q .; then
        echo "Missing Kindle firmware library in SDK: $gtk_lib" >&2
        exit 1
    fi
done

if [[ ! -d vendor/libxnes/src ]]; then
    git clone https://github.com/xboot/libxnes vendor/libxnes
fi
if [[ -d vendor/libxnes/.git ]]; then
    git -C vendor/libxnes fetch --depth 1 origin "$LIBXNES_COMMIT"
    git -C vendor/libxnes checkout --detach "$LIBXNES_COMMIT"
elif ! grep -qx "libxnes=$LIBXNES_COMMIT" vendor/UPSTREAM_VERSIONS.txt 2>/dev/null; then
    echo "Bundled libxnes source is not pinned to $LIBXNES_COMMIT" >&2
    exit 1
fi

if [[ ! -f vendor/FBInk/fbink.c ]]; then
    git clone --recurse-submodules https://github.com/NiLuJe/FBInk vendor/FBInk
fi
if [[ -d vendor/FBInk/.git ]]; then
    git -C vendor/FBInk fetch --depth 1 origin "$FBINK_COMMIT"
    git -C vendor/FBInk checkout --detach "$FBINK_COMMIT"
    git -C vendor/FBInk submodule update --init --recursive
elif ! grep -qx "FBInk=$FBINK_COMMIT" vendor/UPSTREAM_VERSIONS.txt 2>/dev/null; then
    echo "Bundled FBInk source is not pinned to $FBINK_COMMIT" >&2
    exit 1
fi

make -C vendor/FBInk cleanstaticlib || true
make -C vendor/FBInk staticlib KINDLE=1 MINIMAL=1 CROSS_TC="$CROSS"

mapfile -t XNES_SOURCES < <(find vendor/libxnes/src -type f -name '*.c' -print | sort)
test "${#XNES_SOURCES[@]}" -gt 0

"$CC" -std=gnu11 -O2 -DNDEBUG -DPLATFORM_KINDLE \
    -march=armv7-a -mtune=cortex-a7 -mfpu=neon -mfloat-abi=hard -mthumb \
    -fomit-frame-pointer -fno-strict-aliasing -ffunction-sections -fdata-sections \
    -Wall -Wextra \
    -Ivendor/FBInk -Ivendor/libxnes/src -Isrc $GTK_CFLAGS \
    src/main.c src/file_picker.c src/rom_compat.c "${XNES_SOURCES[@]}" vendor/FBInk/Release/libfbink.a \
    -Wl,--gc-sections -Wl,--no-undefined -Wl,-rpath-link,"$SYSROOT/usr/lib" \
    $GTK_LIBS -lm -lrt -lpthread -ldl \
    -o dist/nes

"$STRIP" --strip-unneeded dist/nes
file dist/nes
"$READELF" -h dist/nes | tee dist/elf-header.txt
"$READELF" -A dist/nes | tee dist/elf-attributes.txt
"$READELF" -l dist/nes | tee dist/elf-program-headers.txt
"$READELF" -d dist/nes | tee dist/elf-dynamic.txt || true

grep -q 'Machine:.*ARM' dist/elf-header.txt
grep -Eq 'Version5 EABI|EABI5' dist/elf-header.txt
grep -q 'Tag_CPU_arch: v7' dist/elf-attributes.txt
grep -q 'Tag_Advanced_SIMD_arch: NEONv1' dist/elf-attributes.txt
grep -q 'Tag_ABI_VFP_args: VFP registers' dist/elf-attributes.txt
grep -q 'INTERP' dist/elf-program-headers.txt
grep -q 'Requesting program interpreter: /lib/ld-linux-armhf.so.3' dist/elf-program-headers.txt
grep -Fq 'Shared library: [libgtk-x11-2.0.so.0]' dist/elf-dynamic.txt
grep -Fq 'Shared library: [libgdk-x11-2.0.so.0]' dist/elf-dynamic.txt

# Every direct ELF dependency must exist in the firmware-derived Kindle sysroot.
while read -r needed; do
    [[ -z "$needed" ]] && continue
    if ! find "$SYSROOT/lib" "$SYSROOT/usr/lib" -name "$needed" -print -quit 2>/dev/null | grep -q .; then
        echo "Unresolved Kindle runtime dependency: $needed" >&2
        exit 1
    fi
done < <(awk -F'[][]' '/Shared library:/ {print $2}' dist/elf-dynamic.txt)

sha256sum dist/nes | tee dist/nes.sha256
echo "KindleHF executable build and ELF ABI checks passed."
