# Notices

## Engine and port

Badger Doom is a modified build of [Graham Sanderson's RP2040 Doom](https://github.com/kilograham/rp2040-doom/tree/29a453c9), itself based on [Chocolate Doom](https://github.com/chocolate-doom/chocolate-doom) and [id Software's Doom source release](https://github.com/id-Software/DOOM). See [AUTHORS](AUTHORS), [README-chocolate.md](README-chocolate.md), the source headers, and the Git history for original contributors and copyright notices. The program is distributed under the [GNU General Public License](COPYING.md); separately licensed components retain their own notices.

The Badger Doom contributors modified the port in November 2026 to add the GitHub Universe RP2350 board, E-Ink display and controls, monochrome rendering and HUD, board-local saves, and a native converter build. The Badger renderer reads the original graphics from the user's local game data; the shareware archive, WAD, and converted WHX are not part of the public source distribution and are not covered by the engine's GPL.

When distributing a prebuilt firmware UF2, make corresponding source for that exact build available alongside it, with the build instructions and dependency revisions. Distribute the firmware separately from game data and never include a full-flash device backup.

The offline browser installer compiles the same `whd_gen` source with [Emscripten 4.0.12](https://github.com/emscripten-core/emscripten/tree/4.0.12) and embeds the firmware-only UF2. Its release notes identify the exact firmware build. Runtime and component licenses are embedded in the HTML. Use the tagged repository and [source-build instructions](README.md#offline-installer-and-native-converter) to rebuild that download. A UF2 generated from a user's WAD contains game data and is not part of the release.

## UC8151 driver and badge pinout

- Panel setup and FAST/TURBO tables: Pimoroni Ltd, [pimoroni-pico UC8151 driver](https://github.com/pimoroni/pimoroni-pico/blob/74cf9b7cacc050f2e6c62e5cd57d9067460c658b/drivers/uc8151/uc8151.cpp), [upstream license](https://github.com/pimoroni/pimoroni-pico/blob/74cf9b7cacc050f2e6c62e5cd57d9067460c658b/LICENSE).
- GPIO pin assignments: Pimoroni Ltd, [Badge firmware pin definitions](https://github.com/pimoroni/badger2040/blob/6ea88e080bc48350eb48c6608f67c18497709a31/firmware/PIMORONI_BADGER2350/pimoroni_badger2350.h), [upstream license](https://github.com/pimoroni/badger2040/blob/6ea88e080bc48350eb48c6608f67c18497709a31/LICENSE).
- Differential and maintenance waveforms: Salvatore Sanfilippo, [uc8151_micropython](https://github.com/antirez/uc8151_micropython), [upstream license](https://github.com/antirez/uc8151_micropython/blob/main/LICENSE).

### MIT license for the above material

```text
Copyright (c) 2021 Pimoroni Ltd
Copyright (c) 2023 Pimoroni Ltd
Copyright (c) 2024 Salvatore Sanfilippo.

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```
