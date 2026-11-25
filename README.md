# Badger Doom

Play the original Doom shareware episode on the **GitHub Universe 2024 Badger**. It runs on the badge itself: five buttons, a 296×128 monochrome E-Ink display, and on-device saves. No host game process or keyboard required.

**Hardware:** This build targets the GitHub Universe badge with an **RP2350A**, **4 MiB flash**, and a **UC8151** panel. It is not a build for the Badger 2040, the retail Badger 2350, or a bare Raspberry Pi Pico. The Badger version is single-player and silent. Expect visible refresh lag and occasional flashing on E-Ink.

[Firmware](#get-the-firmware) · [Game data](#prepare-the-game-data) · [First install](#first-install) · [Controls](#controls) · [Troubleshooting](#troubleshooting) · [Credits](#credits-and-license)

## Get the firmware

Download `doom_tiny.uf2` from the [latest GitHub release](https://github.com/tbhojo/BadgerDoom/releases/latest). You do not need to build the firmware yourself. The UF2 does not contain Doom's game data; a first install also needs the [separate game data](#prepare-the-game-data).

### Build from source (optional)

Use Git, CMake, Ninja, a native C/C++ compiler, and the [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain) (`arm-none-eabi-gcc` and `arm-none-eabi-g++` on `PATH`). This build was tested with CMake 3.26, Arm GNU 13.2.Rel1, and Pico SDK 2.1.1. No Pico Extras or SDL is needed.

From the repository root, install the [Pico SDK](https://github.com/raspberrypi/pico-sdk) if you do not already have it. The SDK's TinyUSB submodule is required for USB diagnostics; the older TinyUSB submodule in this repository is not needed for the Badger.

```sh
git clone --branch 2.1.1 --recurse-submodules https://github.com/raspberrypi/pico-sdk.git "$HOME/pico-sdk"

cmake -S . -B build-badger -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel \
  -DPICO_BOARD=github_universe_badger -DPICO_SDK_PATH="$HOME/pico-sdk"
cmake --build build-badger --target doom_tiny
```

The firmware is `build-badger/src/doom_tiny.uf2`. If CMake cannot find a local picotool package it may download and build one; see [picotool's build instructions](https://github.com/raspberrypi/picotool#building) if it needs host dependencies or a local installation.

## Prepare the game data

Obtain the original [Doom v1.9 shareware distribution](https://www.doomworld.com/files/file/7043-doom-v19-shareware) separately and extract `DOOM1.WAD` (you may need to run the DOS installer in DOSBox). Rename it to `doom1.wad` in your working directory. The tested file has SHA-256 `1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771`. This build is for the shareware episode, not the retail Ultimate Doom or Doom II WADs.

From a clone of this repository, use CMake, Ninja, and a native C/C++ compiler to build the converter. You don't need the Pico SDK, Arm toolchain, or SDL for this step.

```sh
cmake -S . -B build-whd-gen -G Ninja -DBUILD_WHD_GEN_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-whd-gen --target whd_gen
./build-whd-gen/src/whd_gen/whd_gen doom1.wad build-whd-gen/doom1.whx
```

Keep your WAD and the converted WHX local. The source release does not distribute either file.

## First install

Use a USB **data** cable and [picotool 2.1.1](https://github.com/raspberrypi/picotool/tree/2.1.1) or newer.

1. Unplug the badge. Hold **BOOTSEL** while reconnecting it by USB. Release the button when the `RP2350` drive appears. Disconnect other RP-series boards before flashing.
2. Run `picotool info -d`. Confirm it reports an **RP2350** with **4096K** of flash; the chip and flash size alone do not distinguish badge models.
3. Save the existing firmware and data before writing anything. **Skip the backup command if this file already contains your original backup.** Keep that snapshot private; it can contain your badge's files.

   ```sh
   picotool save -a "$HOME/badger-backup.bin"
   ```

4. Write and verify the data first, then the firmware. In the second command, substitute the path to your downloaded UF2 or use `build-badger/src/doom_tiny.uf2` if you built it. The badge reboots after the firmware is written.

   ```sh
   picotool load -u -v -t bin build-whd-gen/doom1.whx -o 0x10040000
   picotool load -u -v -x doom_tiny.uf2
   ```

Wait for the initial panel clear and the Doom title. It stays on the title until you open the menu. Press and release **A+C**, then choose **New Game**. A controls card appears after difficulty selection; **B** starts and **Up+Down** takes you back.

### Update or restore

For a firmware-only update, enter BOOTSEL and run only the second `load` command above (or copy the new `doom_tiny.uf2` onto `RP2350`). This does not replace the game data or saves. The game data starts at `0x10040000`; Badger Doom saves use flash offsets `0x200000`–`0x2fffff`.

To **restore the original badge**, enter BOOTSEL and restore your own full-flash backup. This overwrites the current firmware, game data, and Doom saves:

```sh
picotool load -v -t bin "$HOME/badger-backup.bin" -o 0x10000000
picotool reboot
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
| Blank panel on first boot | Give the panel time to clear. If it stays blank, return to BOOTSEL and verify the data with `picotool verify -t bin build-whd-gen/doom1.whx -o 0x10040000`. Copying only the firmware does not install the game. |
| Need boot diagnostics | Open the badge's USB serial port at `115200` to catch startup messages. On a successful boot it reports panel initialization and that the first game frame was sent. USB is not used for gameplay input. |
| Brief flashing or a faint cursor trail | Menu steps use differential updates and clean up after a short idle. Page changes and pause use a stronger refresh. Gameplay periodically refreshes to recover contrast; some ghosting is normal on this panel. |
| `arm-none-eabi-gcc` or TinyUSB not found | Check the toolchain is on `PATH`. In the SDK checkout run `git submodule update --init --recursive`. Use a fresh build directory when changing toolchains or targets. |

## Credits and license

- [id Software's Doom source release](https://github.com/id-Software/DOOM) is the original engine. Doom game data is separate from the engine's GPL release and remains copyrighted by its rights holders.
- [Chocolate Doom](https://www.chocolate-doom.org/) is the base source port. See the [original README](README-chocolate.md) and [AUTHORS](AUTHORS).
- [Graham Sanderson's RP2040 Doom](https://github.com/kilograham/rp2040-doom) provides the embedded renderer and WAD conversion tooling. This port is based on [upstream revision `29a453c`](https://github.com/kilograham/rp2040-doom/commit/29a453c9). For the original VGA build and other boards, use the upstream documentation.
- [Pimoroni](https://github.com/pimoroni/badger2040) supplied badge firmware and pin definitions; Pimoroni and [Salvatore Sanfilippo](https://github.com/antirez/uc8151_micropython) provided the UC8151 refresh routines used as references here.

The engine is distributed under the [GNU General Public License](COPYING.md); see individual source notices for version and component-specific terms. [NOTICE.md](NOTICE.md) covers the Badger changes and third-party notices. This is an independent port, not an official release from id Software, GitHub, Pimoroni, or Raspberry Pi.
