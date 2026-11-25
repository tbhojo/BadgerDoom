# Badger Doom

Play the original Doom shareware episode on the **GitHub Universe 2024 Badger**. It runs on the badge itself: five buttons, a 296×128 monochrome E-Ink display, and on-device saves. No host game process or keyboard required.

**Hardware:** This build targets the GitHub Universe badge with an **RP2350A**, **4 MiB flash**, and a **UC8151** panel. It is not a build for the Badger 2040, the retail Badger 2350, or a bare Raspberry Pi Pico. The Badger version is single-player and silent. Expect visible refresh lag and occasional flashing on E-Ink.

[First install](#first-install) · [Updates and backup](#updates-and-backup) · [Controls](#controls) · [Troubleshooting](#troubleshooting) · [Build from source](#build-from-source) · [Credits](#credits-and-license)

## First install

You need a USB **data** cable and a current desktop browser. No picotool or build tools are required.

**Back up first if you need to restore your badge's original firmware or files.** Flashing replaces the current firmware and can overwrite files from it. The BOOTSEL drive does not expose those files; see [full-flash backup](#full-flash-backup-and-restore) before you proceed.

1. Obtain the original [Doom v1.9 shareware distribution](https://www.doomworld.com/files/file/7043-doom-v19-shareware) and extract `DOOM1.WAD`. If your archive contains a DOS installer, you may need DOSBox to extract it. The supported WAD has SHA-256 `1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771`. Retail Doom, Ultimate Doom, and Doom II WADs are not supported.
2. Download `badger-doom-installer.html` from the [latest release](https://github.com/tbhojo/BadgerDoom/releases/latest). Open the downloaded file, select `DOOM1.WAD`, and click **Create installer**. Download the resulting `badger-doom-install.uf2`.
3. Unplug the badge. Hold **BOOTSEL** while reconnecting it by USB. Release the button when the `RP2350` drive appears. A drive named `RP2350` alone does not mean the board is the supported GitHub Universe badge.
4. Copy `badger-doom-install.uf2` onto the `RP2350` drive. The badge should reboot automatically after the copy. Wait for the panel to clear and show the Doom title.

The installer combines firmware and game data **locally in your browser**. Nothing is uploaded. The generated UF2 contains your game data; keep it private.

At the title, press and release **A+C**, then choose **New Game**. A controls card appears after difficulty selection; **B** starts and **Up+Down** takes you back.

## Updates and backup

**Already running Badger Doom?** Use the smaller `doom_tiny.uf2` from the [latest release](https://github.com/tbhojo/BadgerDoom/releases/latest) for firmware-only updates. Copy it onto `RP2350` in BOOTSEL mode. It does not replace the game data or saves.

The combined installer also leaves the Badger Doom save slots untouched. Game data begins at `0x10040000`; Badger Doom saves use flash offsets `0x200000`–`0x2fffff`.

### Full-flash backup and restore

Drag-and-drop flashing cannot read back flash. For a restorable copy of the whole badge, use [picotool 2.1.1](https://github.com/raspberrypi/picotool/tree/2.1.1) or newer. This is optional for installation but needed for the full backup procedure below.

Enter BOOTSEL, disconnect any other RP-series boards, and run `picotool info -d` to confirm the RP2350 reports **4096K** flash. Save the backup before your first install. Do not overwrite an existing original backup; it may contain your badge's files.

```sh
picotool save -a "$HOME/badger-backup.bin"
```

To restore that snapshot later, enter BOOTSEL again. This replaces the current firmware, game data, and Doom saves:

```sh
picotool load -v -t bin "$HOME/badger-backup.bin" -o 0x10000000
picotool reboot
```

## Build from source

### Firmware

Use Git, CMake, Ninja, a native C/C++ compiler, and the [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain) (`arm-none-eabi-gcc` and `arm-none-eabi-g++` on `PATH`). The tested firmware toolchain is CMake 3.26, Arm GNU 13.2.Rel1, and Pico SDK 2.1.1. No Pico Extras or SDL is needed. The SDK's TinyUSB submodule is required for USB diagnostics; the older TinyUSB submodule in this repository is not used.

```sh
git clone --branch 2.1.1 --recurse-submodules https://github.com/raspberrypi/pico-sdk.git "$HOME/pico-sdk"
cmake -S . -B build-badger -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel \
  -DPICO_BOARD=github_universe_badger -DPICO_SDK_PATH="$HOME/pico-sdk"
cmake --build build-badger --target doom_tiny
```

The result is `build-badger/src/doom_tiny.uf2`. If CMake cannot find a local picotool package it may download and build one; see [picotool's build instructions](https://github.com/raspberrypi/picotool#building) if it needs host dependencies.

### Offline installer and native converter

Activate [Emscripten SDK 4.0.12](https://github.com/emscripten-core/emsdk/tree/4.0.12) and pass the path to a firmware-only `doom_tiny.uf2` built for the GitHub Universe Badger:

```sh
emcmake cmake -S . -B build-browser-converter -G Ninja -DBUILD_WHD_GEN_ONLY=ON \
  -DCMAKE_BUILD_TYPE=Release -DBADGER_FIRMWARE_UF2="$PWD/build-badger/src/doom_tiny.uf2"
cmake --build build-browser-converter --target whd_converter
```

The result is `build-browser-converter/src/whd_gen/badger-doom-installer.html`.

For a native command-line converter that outputs only `doom1.whx`, use CMake, Ninja, and a C/C++ compiler. No Pico SDK, Arm toolchain, or SDL is needed:

```sh
cmake -S . -B build-whd-gen -G Ninja -DBUILD_WHD_GEN_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-whd-gen --target whd_gen
./build-whd-gen/src/whd_gen/whd_gen doom1.wad doom1.whx
```

## Controls

| Where | Controls |
| --- | --- |
| Gameplay | **Up/Down** move forward/back. **A/C** turn left/right. **B** fires. **Up+Down** uses switches and opens doors. |
| Gameplay shortcuts | Hold **A+C**, then tap **B** to cycle owned weapons (including fists), **Up** for the automap, or **Down** to pause/resume. Release **A+C** before the next shortcut. |
| Menu | **A+C** opens or closes the menu; release the chord after pressing. **Up/Down** select, **A/C** adjust, **B** confirms, and **Up+Down** goes back. |
| Prompts and saves | **B** confirms; either chord cancels. New saves default to the current map name. |
| Automap | **B** zooms in; **Up+Down** zooms out. Hold **A+C**, then tap **B** to toggle follow mode. When follow is off, **Up/Down/A/C** pan. |

Use short taps for precise turns. The game and buttons keep running between panel updates, so a held turn can move farther than the current image suggests. On the HUD, `--` means the current weapon uses no ammo; `R`, `Y`, and `B` indicate held keys. On the automap, short crossbars mark discovered doors and dashed lines mark changes in floor height. Opening the map does not reveal undiscovered rooms.

## Troubleshooting

| Symptom | What to check |
| --- | --- |
| No `RP2350` drive | Unlock the computer and approve the USB accessory if prompted. Use a known data cable, connect directly, and hold BOOTSEL before plugging in. `picotool info -d` can find the bootloader even if the OS hasn't mounted the drive. |
| Blank panel on first boot | Give the panel time to clear. If it stays blank, re-enter BOOTSEL and copy the generated `badger-doom-install.uf2` again. The smaller `doom_tiny.uf2` is for firmware-only updates; it cannot install the game data. |
| Need boot diagnostics | Open the badge's USB serial port at `115200` to catch startup messages. On a successful boot it reports panel initialization and that the first game frame was sent. USB is not used for gameplay input. |
| Brief flashing or a faint cursor trail | Menu steps use differential updates and clean up after a short idle. Page changes and pause use a stronger refresh. Gameplay periodically refreshes to recover contrast; some ghosting is normal on this panel. |
| `arm-none-eabi-gcc` or TinyUSB not found | Check the toolchain is on `PATH`. In the SDK checkout run `git submodule update --init --recursive`. Use a fresh build directory when changing toolchains or targets. |

## Credits and license

- [id Software's Doom source release](https://github.com/id-Software/DOOM) is the original engine. Doom game data is separate from the engine's GPL release and remains copyrighted by its rights holders.
- [Chocolate Doom](https://www.chocolate-doom.org/) is the base source port. See the [original README](README-chocolate.md) and [AUTHORS](AUTHORS).
- [Graham Sanderson's RP2040 Doom](https://github.com/kilograham/rp2040-doom) provides the embedded renderer and WAD conversion tooling. This port is based on [upstream revision `29a453c`](https://github.com/kilograham/rp2040-doom/commit/29a453c9). For the original VGA build and other boards, use the upstream documentation.
- [Pimoroni](https://github.com/pimoroni/badger2040) supplied badge firmware and pin definitions; Pimoroni and [Salvatore Sanfilippo](https://github.com/antirez/uc8151_micropython) provided the UC8151 refresh routines used as references here.

The engine is distributed under the [GNU General Public License](COPYING.md); see individual source notices for version and component-specific terms. [NOTICE.md](NOTICE.md) covers the Badger changes and third-party notices. This is an independent port, not an official release from id Software, GitHub, Pimoroni, or Raspberry Pi.
