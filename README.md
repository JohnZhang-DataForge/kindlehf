# Kindle NES (native KindleHF)

Native C NES emulator frontend for jailbroken Kindle devices running firmware 5.16.3 or newer. First target: Kindle Basic 10th Generation (2019), firmware 5.16.8.

- NES core: pinned xboot/libxnes (CPU, PPU, cartridge/mapper emulation)
- Display: FBInk raw framebuffer output
- Input: Linux evdev touchscreen
- UI: native framebuffer; no browser, HTML, JavaScript, KUAL, or KPM launcher
- Audio: disabled
- E-ink: 7 emulated frames per display update; monochrome Bayer conversion and A2 game updates. Not 60 Hz display output.

## Repository layout

- `src/main.c`: native frontend
- `build.sh`: reproducible KindleHF build
- `.github/workflows/kindlehf.yml`: public GitHub-hosted runner build and packaging
- `tests/`: generated mapper-0 test ROM and host core/controller tests
- `documents/NES Emulator.sh`: SH Integration scriptlet

## Supported cartridge mappers

The pinned libxnes revision includes mapper implementations for 0, 1, 2, 3, 4, 7, 15, 66, 79, 87, 113, 140, 177, 225, and 241. Not all games or cartridge variants are guaranteed.

## Build

The public GitHub Actions workflow downloads the pinned KOReader KindleHF toolchain, verifies its SHA-256, checks out pinned FBInk and libxnes commits, compiles the ARM executable, verifies ELF/ABI attributes, runs host-side ROM/core/controller tests, and uploads the installation ZIP and source archive.

For a local Linux build: `./build.sh`.

Toolchain: `arm-kindlehf-linux-gnueabihf`, KOReader koxtoolchain 2026.08. KindleHF is the target documented for Kindle firmware >= 5.16.3. Do not substitute `kindlepw2`.

## Current limitations

- No audio output.
- Save states and battery-backed SRAM persistence are not yet wired into the frontend.
- Touchscreen discovery and coordinate orientation use FBInk device quirks and generic evdev ABS/BTN_TOUCH events. Physical Kindle testing is still required to confirm the exact event node and touch calibration.
- E-ink refreshes are throttled; emulation cadence and display refresh cadence are separate.
- A successful cross-build does not prove the app runs correctly on a physical Kindle.

See `README-INSTALL.txt` in the installation ZIP for installation and recovery steps.
