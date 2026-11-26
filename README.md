# Kindle NES (native KindleHF)

Native C NES emulator frontend for jailbroken Kindle devices running firmware 5.16.3 or newer. First target: Kindle Basic 10th Generation (2019), firmware 5.16.8.

The GTK2 shell and ROM picker follow the Gambatte-K2 interaction model; only the frontend/UI is reused as a design reference. NES CPU, PPU, cartridge and mapper emulation are provided by the independent libxnes core.

- NES core: pinned xboot/libxnes (CPU, PPU, cartridge/mapper emulation)
- Display: FBInk raw framebuffer output for gameplay; Kindle GTK2/X11 window for the ROM picker
- Input: Linux evdev touchscreen
- UI: GTK2 frontend with Open Game / Options / Fullscreen / Quit toolbar, plus Gambatte-K2-style `SimpleFilePicker` (Cancel + path, scrollable list, `..`, folders first, touch-drag scrolling); lists `.nes` files only
- Audio: disabled
- E-ink: 7 emulated frames per display update; monochrome Bayer conversion and A2 game updates. Not 60 Hz display output.

## Repository layout

- `src/main.c`: libxnes/FBInk NES gameplay frontend
- `src/file_picker.c`: GTK2 ROM picker ported to C without the Gambatte Game Boy core or libgee
- `build.sh`: reproducible KindleHF build
- `.github/workflows/kindlehf.yml`: public GitHub-hosted runner build and packaging
- `tests/`: generated mapper-0 test ROM and host core/controller tests
- `documents/NES Emulator.sh`: SH Integration scriptlet; opens the GTK2 main window

## Supported cartridge mappers

The pinned libxnes revision includes mapper implementations for 0, 1, 2, 3, 4, 7, 15, 66, 79, 87, 113, 140, 177, 225, and 241. Not all games or cartridge variants are guaranteed.

## Build

The public GitHub Actions workflow downloads the pinned KOReader KindleHF toolchain, verifies its SHA-256, checks out pinned FBInk and libxnes commits, compiles the ARM executable, verifies ELF/ABI attributes, runs host-side ROM/core/controller tests, and uploads the installation ZIP and a source archive containing the pinned upstream sources and license texts.

For a local Linux build: `bash ./build.sh`.

Toolchain: `arm-kindlehf-linux-gnueabihf`, KOReader koxtoolchain 2026.08, plus KindleModding Kindle SDK (pinned commit) to link against firmware GTK2/GLib libraries. The compiler flags follow KOX's KHF target: `-march=armv7-a -mtune=cortex-a7 -mfpu=neon -mfloat-abi=hard -mthumb`. KindleHF is the target documented for Kindle firmware >= 5.16.3. Do not substitute `kindlepw2`.

## ROM file picker

The GTK2 main window presents Open Game, Options, Fullscreen, and Quit. Open Game opens a Gambatte-K2-style dialog: Cancel button and current path at the top, a scrollable single-column list, a `..` parent row, directories first, then `.nes` files, and touch-drag scrolling that does not activate the row. Cancel returns to the main window; exiting a game also returns there. The picker starts at `/mnt/us`. ROMs can remain in any folder under `/mnt/us`; no `nes/roms/` staging folder is required. The GTK2 runtime is supplied by Kindle firmware and is dynamically linked, not bundled.

## Gameplay loop\n\nThe emulator advances the NES core in bounded GLib timer callbacks while keeping the GTK2 main loop active. This avoids starving X11/GTK event dispatch after a ROM is selected and keeps the UI lifecycle responsive. Runtime diagnostics are written to `/mnt/us/nes/nes.log`.\n\n## Current limitations

- No audio output.
- Save states and battery-backed SRAM persistence are not yet wired into the frontend.
- Touchscreen discovery and coordinate orientation use FBInk device quirks and generic evdev ABS/BTN_TOUCH events. Physical Kindle testing is still required to confirm the exact event node and touch calibration.
- E-ink refreshes are throttled; emulation cadence and display refresh cadence are separate.
- A successful cross-build does not prove the app runs correctly on a physical Kindle.

See `README-INSTALL.txt` in the installation ZIP for installation and recovery steps.


## ROM loading troubleshooting

The emulator writes the selected ROM path, file size, iNES header, mapper, validation result, and cartridge-core initialization result to `/mnt/us/nes/nes.log`. Unsupported NES 2.0 headers, CHR-RAM-only images, truncated files, and unsupported mappers are reported explicitly. Error dialogs use plain text rather than stock theme icons because Kindle GTK themes may not provide those icons.
