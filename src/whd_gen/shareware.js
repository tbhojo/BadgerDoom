// SPDX-License-Identifier: GPL-2.0-or-later
const sharewareWadSize = 4196020;

function wadCrc32(bytes) {
  const table = new Uint32Array(256);
  for (let i = 0; i < table.length; i++) {
    let crc = i;
    for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
    table[i] = crc;
  }
  let crc = 0xffffffff;
  for (const byte of bytes) crc = (crc >>> 8) ^ table[(crc ^ byte) & 0xff];
  return (~crc) >>> 0;
}

async function inflateSharewareWad(compressed) {
  let inflater;
  try {
    inflater = new DecompressionStream('deflate-raw');
  } catch {
    throw new Error('This browser cannot open the ZIP. Use a current browser or select the extracted DOOM1.WAD.');
  }
  // Feed bounded chunks and cap the output independently of the ZIP metadata.
  let offset = 0;
  const input = new ReadableStream({
    pull(controller) {
      if (offset === compressed.length) {
        controller.close();
      } else {
        const next = Math.min(offset + 16384, compressed.length);
        controller.enqueue(compressed.subarray(offset, next));
        offset = next;
      }
    },
  });
  const reader = input.pipeThrough(inflater).getReader();
  const wad = new Uint8Array(sharewareWadSize);
  let written = 0;
  try {
    for (;;) {
      const { value, done } = await reader.read();
      if (done) break;
      if (value.length > wad.length - written) {
        await reader.cancel();
        throw new Error('WAD entry exceeds its expected size');
      }
      wad.set(value, written);
      written += value.length;
    }
  } catch {
    throw new Error('The ZIP has an invalid DOOM1.WAD entry. Try downloading Doom95 again.');
  }
  return wad.subarray(0, written);
}

async function extractSharewareWad(zip) {
  const view = new DataView(zip.buffer, zip.byteOffset, zip.byteLength);
  const u16 = offset => view.getUint16(offset, true);
  const u32 = offset => view.getUint32(offset, true);
  const malformed = () => new Error('Could not read this ZIP. Try downloading the Doom95 shareware ZIP again.');
  const decoder = new TextDecoder();
  const rootName = (offset, size) => size === 9 &&
    decoder.decode(zip.subarray(offset, offset + size)).toLowerCase() === 'doom1.wad';

  // Use the single-disk ZIP32 central directory, not local sizes that may be
  // absent when the archive uses data descriptors.
  const earliestEnd = Math.max(0, zip.length - 22 - 65535);
  let end = zip.length - 22;
  while (end >= earliestEnd && !(u32(end) === 0x06054b50 && end + 22 + u16(end + 20) === zip.length)) end--;
  if (end < earliestEnd || u16(end + 4) || u16(end + 6) || u16(end + 8) !== u16(end + 10)) throw malformed();
  const directory = u32(end + 16);
  if (directory + u32(end + 12) !== end) throw malformed();
  let pos = directory;
  let entry = -1;
  const entries = u16(end + 10);
  for (let i = 0; i < entries; i++) {
    if (pos + 46 > end || u32(pos) !== 0x02014b50) throw malformed();
    const nameSize = u16(pos + 28);
    const next = pos + 46 + nameSize + u16(pos + 30) + u16(pos + 32);
    if (next > end) throw malformed();
    if (rootName(pos + 46, nameSize)) {
      if (entry !== -1) throw new Error('This ZIP has more than one top-level DOOM1.WAD. Select the intended WAD directly.');
      entry = pos;
    }
    pos = next;
  }
  if (pos !== end) throw malformed();
  if (entry < 0) throw new Error('No top-level DOOM1.WAD found. Choose doom95.zip, not the DOS installer archive.');
  const flags = u16(entry + 8), method = u16(entry + 10);
  if ((flags & 0x41) || (method !== 0 && method !== 8)) {
    throw new Error('This ZIP uses encryption or unsupported compression. Choose doom95.zip or the WAD directly.');
  }
  if (u32(entry + 24) !== sharewareWadSize) throw new Error('The ZIP does not contain the Doom v1.9 shareware WAD.');
  const local = u32(entry + 42);
  if (u16(entry + 34) || local + 30 > directory || u32(local) !== 0x04034b50 ||
      u16(local + 6) !== flags || u16(local + 8) !== method) throw malformed();
  const start = local + 30 + u16(local + 26) + u16(local + 28);
  const stop = start + u32(entry + 20);
  if (stop > directory || !rootName(local + 30, u16(local + 26))) throw malformed();
  const packed = zip.subarray(start, stop);
  const wad = method === 0 ? packed : await inflateSharewareWad(packed);
  if (wad.length !== sharewareWadSize || wadCrc32(wad) !== u32(entry + 16)) throw malformed();
  return wad;
}

async function readSharewareWad(input) {
  const header = new DataView(input);
  const magic = input.byteLength >= 4 ? header.getUint32(0) : 0;
  const bytes = new Uint8Array(input);
  const wad = magic === 0x504b0304 || magic === 0x504b0506 ? await extractSharewareWad(bytes) : bytes;
  if (wad.length !== sharewareWadSize || new DataView(wad.buffer, wad.byteOffset).getUint32(0) !== 0x49574144) {
    throw new Error('Choose doom95.zip or DOOM1.WAD from the Doom v1.9 shareware release.');
  }
  return wad;
}
