KINDLE NES — INSTALLATION
=========================

TARGET
------
Kindle Basic 10th Generation (2019), jailbroken, firmware 5.16.8.
Requires SH Integration (scriptlets). This is NOT a KUAL/KPM package.

INSTALL
-------
1. Connect the Kindle by USB and open its USB storage.
2. Copy the "documents" and "nes" folders from this ZIP to the Kindle storage root.
   Merge with existing folders if prompted; do not delete existing files.
3. Keep legally obtained, unmodified NES ROM files anywhere on Kindle USB storage (/mnt/us), including nested folders. You do NOT need to copy them into the emulator folder.
4. Safely eject the Kindle.
5. Return to the Kindle library and tap "NES Emulator".

EXPECTED DEVICE PATHS
---------------------
/mnt/us/documents/NES Emulator.sh
/mnt/us/nes/bin/nes
/mnt/us/nes/saves/

The launcher stages the executable under /var/tmp before running it because USB
storage may be mounted noexec. Runtime diagnostics are appended to:
/mnt/us/nes/nes.log

CONTROLS
--------
- Browse folders in the native file picker; tap any .nes file to start.
- ROOT returns to the Kindle USB storage root; UP goes to the parent folder.
- D-pad: lower-left.
- A/B: lower-right.
- SELECT/START: lower middle.
- PAUSE/RESUME: upper-right.
- EXIT: upper-left, returns to the file picker in the current folder.

NOTES
-----
- Audio is disabled.
- Save states and battery-backed saves are not implemented in this initial build.
- The e-ink display is updated in batches and does not run at 60 display FPS.
- Cross-compilation and host tests do not replace a real-device test. If the
  touchscreen cannot be discovered or the screen is incorrectly oriented, this
  build must be treated as not yet validated on that Kindle.

TROUBLESHOOTING
---------------
Launcher is visible but reports a missing binary:
  Check that nes/bin/nes exists and is executable after copying.

Tapping the launcher returns immediately:
  Inspect /mnt/us/nes/nes.log. Confirm SH Integration is installed and its
  Gandalf/su privilege helper is functional. SH Integration normally launches
  scriptlets through its setuid helper; do not manually chmod or replace that
  system helper. The emulator needs framebuffer and evdev access.

"TOUCH INPUT NOT FOUND":
  The frontend could not find a compatible evdev touchscreen. Device-specific
  event-node support or permissions may be required; this is not fixed by
  changing ROMs.

ROM does not appear:
  Confirm the file ends in .nes and is somewhere under /mnt/us. Browse into its
  containing folder. Hidden files and symbolic links are intentionally omitted.

A ROM appears but fails to start:
  Try the generated mapper-0 test ROM from the repository test suite. Not all
  mappers and cartridge formats are supported.

UNINSTALL
---------
Delete /mnt/us/documents/NES Emulator.sh and /mnt/us/nes/. This does not modify
Kindle system partitions or jailbreak files.
