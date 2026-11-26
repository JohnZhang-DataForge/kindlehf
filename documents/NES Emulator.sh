#!/bin/sh
# Name: NES Emulator
# Author: Kindle NES Project
# DontUseFBInk
# Kindle SH Integration scriptlet. Place this file in /mnt/us/documents/.
# Native KindleHF executable; no KUAL, KPM, browser, HTML, or JavaScript.

APPDIR="/mnt/us/nes"
BIN="$APPDIR/bin/nes"
BROWSE_ROOT="/mnt/us"
TMPDIR="${TMPDIR:-/var/tmp}"
TMPBIN="$TMPDIR/kindle-nes-$$"
LOG="$APPDIR/nes.log"

mkdir -p "$APPDIR/saves" "$APPDIR/bin"

if [ ! -f "$BIN" ]; then
    echo "NES Emulator: executable not found."
    echo "Expected: $BIN"
    exit 1
fi

if [ ! -x "$TMPDIR" ]; then
    TMPDIR="/tmp"
    TMPBIN="$TMPDIR/kindle-nes-$$"
fi

cleanup() {
    rm -f "$TMPBIN"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

if ! cp "$BIN" "$TMPBIN" || ! chmod 700 "$TMPBIN"; then
    echo "NES Emulator: cannot stage executable in $TMPDIR."
    exit 1
fi

cd "$APPDIR" || exit 1
"$TMPBIN" "$BROWSE_ROOT" >>"$LOG" 2>&1
RC=$?
exit "$RC"
