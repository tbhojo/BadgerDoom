// SPDX-License-Identifier: GPL-2.0-or-later
function makeInstallUf2(firmware, whx) {
  const blockSize = 512, pageSize = 256, headerSize = 32;
  const flashStart = 0x10000000, whxStart = 0x10040000, savesStart = 0x10200000;
  const armSecure = 0xe48bff59, familyFlag = 0x2000;
  const magic0 = 0x0a324655, magic1 = 0x9e5d5157, magicEnd = 0x0ab16f30;
  const game = new DataView(whx.buffer, whx.byteOffset, whx.byteLength);
  if (whx.length < 36 || whx.length > savesStart - whxStart ||
      game.getUint32(0) !== 0x49574858 || game.getUint32(12, true) !== whx.length) {
    throw new Error('The converter did not produce a WHX that fits the Badger game-data slot.');
  }

  // The first record is picotool's RP2350-E10 workaround, not a flash page.
  const fwPages = firmware.length / blockSize - 1;
  const invalidFirmware = () => new Error('The embedded Badger firmware is not a supported UF2 image.');
  if (!Number.isInteger(fwPages) || fwPages < 1 || fwPages * pageSize > whxStart - flashStart) {
    throw invalidFirmware();
  }
  const uf2 = new DataView(firmware.buffer, firmware.byteOffset, firmware.byteLength);
  const read = (base, offset) => uf2.getUint32(base + offset, true);
  const matchesPage = (base, flags, address, family, number, total) =>
    read(base, 0) === magic0 && read(base, 4) === magic1 && read(base, 508) === magicEnd &&
    read(base, 8) === flags && read(base, 12) === address && read(base, 16) === pageSize &&
    read(base, 20) === number && read(base, 24) === total && read(base, 28) === family;
  if (!matchesPage(0, 0xa000, 0x10ffff00, 0xe48bff57, 0, 2) || read(0, 288) !== 0x9957e304) {
    throw invalidFirmware();
  }

  // The ROM treats different page totals as different transfers. Keep every
  // flash page in one ARM-S transfer; preserve the ignored prefix unchanged.
  const total = fwPages + Math.ceil(whx.length / pageSize);
  const output = new Uint8Array((total + 1) * blockSize);
  const view = new DataView(output.buffer);
  output.set(firmware);
  let base = blockSize;
  for (let i = 0; i < fwPages; i++, base += blockSize) {
    if (!matchesPage(base, familyFlag, flashStart + i * pageSize, armSecure, i, fwPages)) {
      throw invalidFirmware();
    }
    view.setUint32(base + 24, total, true);
  }
  for (let offset = 0, i = fwPages; i < total; i++, offset += pageSize, base += blockSize) {
    view.setUint32(base, magic0, true);
    view.setUint32(base + 4, magic1, true);
    view.setUint32(base + 8, familyFlag, true);
    view.setUint32(base + 12, whxStart + offset, true);
    view.setUint32(base + 16, pageSize, true);
    view.setUint32(base + 20, i, true);
    view.setUint32(base + 24, total, true);
    view.setUint32(base + 28, armSecure, true);
    output.set(whx.subarray(offset, offset + pageSize), base + headerSize);
    view.setUint32(base + 508, magicEnd, true);
  }
  return output;
}
