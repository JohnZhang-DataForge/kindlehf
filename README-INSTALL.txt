KINDLE NES — INSTALLATION
=========================

VERSION 0.1.10 — WAIT FOR INITIAL E-INK REFRESH
--------------------------------------------------------
The in-game top bar provides BACK (return to the emulator window), MENU
(Resume / Back to Library / Quit Emulator), and PAUSE/RESUME. The main-window
Options information dialog is non-modal. The game viewport starts below the
toolbar so gameplay refreshes cannot overwrite the top-bar labels. D-pad
direction letters sit inside their respective arms, and SELECT/START are
larger, centered high-contrast buttons. The bitmap font bit order is corrected
so all overlay labels display upright. The initial game image and controls are
composed into one full-screen refresh. The app now waits for the Kindle EPDC
to complete each GC16 refresh before sending subsequent A2 partial game frames.
This prevents the first partial update from racing the initial controller
refresh, which previously made the controls appear only after tapping PAUSE.
Build: KindleHF ARM hard-float; initial GC16 completion is now explicitly
fenced before the first A2 gameplay update. See nes.log for the
"GC16 refresh completed before next update" diagnostic.

This build logs the selected touchscreen's name and capabilities, the first
64 raw evdev events, and the first 32 mapped touch reports to
/mnt/us/nes/nes.log. A touchscreen being discovered does not prove that it is
delivering events. If controls do not respond, tap the screen several times,
exit using the MENU if possible, and share the complete log for calibration.

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
5. Return to the Kindle library and tap "NES Emulator". The GTK2 main window appears; tap "Open Game" to choose a ROM.

EXPECTED DEVICE PATHS
---------------------
/mnt/us/documents/NES Emulator.sh
/mnt/us/nes/bin/nes
/mnt/us/nes/saves/

The launcher stages the executable under /var/tmp before running it because USB
storage may be mounted noexec. It defaults DISPLAY to :0 for the Kindle GTK2 window manager. Runtime diagnostics are appended to:
/mnt/us/nes/nes.log

CONTROLS
--------
- Main window: Open Game, Options, Fullscreen, and Quit.
- Open Game opens the Gambatte-K2-style picker: Cancel + current path at the top, scrollable file list, parent row (..), folders first, then .nes files.
- Drag the list to scroll; tap a folder to enter it; tap a .nes file to launch it.
- The picker starts at /mnt/us and remembers the last directory when returning from a game.
- Cancel returns to the main window.
- D-pad: lower-left; A/B: lower-right; SELECT/START: lower middle.
- BACK: upper-left, returns to the native GTK2 main window. Tap Open Game to choose another ROM.
- MENU: top center. Opens an in-game menu with Resume, Back to Library, and Quit Emulator.
- PAUSE/RESUME: upper-right.
- The main-window Options information dialog is non-modal.
- The current Kindle reports one multitouch slot, so simultaneous multi-finger button combinations may not be supported.

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

Touch controls are visible but do not respond:
  Inspect /mnt/us/nes/nes.log. The current diagnostic build records the selected
  event device name and the first raw input events, then logs mapped touch
  coordinates and NES button masks. If the log has no "NES: touch raw" lines
  after tapping the screen, the selected evdev node is not delivering events.
  If coordinates appear but are rotated or mirrored, the logged axis ranges and
  swap/mirror values identify the calibration problem. Send the complete log
  so the exact device mapping can be corrected.

Cannot leave a game:
  Tap BACK in the upper-left to return to the emulator window. Tap MENU in the
  top center, then choose BACK TO LIBRARY or QUIT EMULATOR. PAUSE/RESUME is in
  the upper-right; it does not exit the game.

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


If a selected ROM does not start, inspect `/mnt/us/nes/nes.log`. The log records the ROM path, size, iNES header, mapper, and exact validation/core initialization failure. The launcher returns to the ROM picker after an unsupported or invalid ROM instead of leaving a generic stock-icon dialog open.


NES 2.0 / CHR-RAM compatibility:
  Basic NES 2.0 ROMs with ordinary 16 KB PRG and 8 KB CHR bank counts are
  normalized for the bundled libxnes core. CHR-RAM-only cartridges receive
  writable compatibility CHR memory. Extended NES 2.0 sizes, extended mapper
  IDs, and submappers are not yet supported.
