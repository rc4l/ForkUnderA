// [rc4l] A BEHAVIOR lump in the old ACS0 format, so a template map can run an OPEN script without an ACS compiler.
import { Buffer } from "node:buffer";

// [rc4l] The p-codes templates use, numbered as in p_acs.h; ACS0 writes every code and argument as a 32-bit word.
export const PCD = { TERMINATE: 1, PUSHNUMBER: 3, LSPEC1: 4 };

// [rc4l] OPEN script 1 running `code` (words), with `strings` indexed from 0 as the script's string arguments.
export function acs0(code, strings = []) {
  const strBytes = strings.map((s) => Buffer.from(`${s}\0`, "latin1"));
  const strStart = 8 + 4 * code.length;
  const strLen = strBytes.reduce((n, b) => n + b.length, 0);
  const dirOffset = strStart + Math.ceil(strLen / 4) * 4;
  const out = Buffer.alloc(dirOffset + 4 + 12 + 4 + 4 * strings.length);
  out.write("ACS\0", 0, "latin1");
  out.writeInt32LE(dirOffset, 4);
  code.forEach((w, i) => out.writeInt32LE(w, 8 + 4 * i));
  const offsets = [];
  let at = strStart;
  for (const b of strBytes) { b.copy(out, at); offsets.push(at); at += b.length; }
  // [rc4l] The directory: one script numbered 1001, which ACS0 reads as script 1 of type OPEN, then the string table after it.
  out.writeInt32LE(1, dirOffset);
  out.writeInt32LE(1001, dirOffset + 4);
  out.writeInt32LE(8, dirOffset + 8);
  out.writeInt32LE(0, dirOffset + 12);
  out.writeInt32LE(strings.length, dirOffset + 16);
  offsets.forEach((o, i) => out.writeInt32LE(o, dirOffset + 20 + 4 * i));
  return out;
}

// [rc4l] The words that call action special `special` with up to five arguments, as `Special(a, b, c)` compiles.
export function callSpecial(special, args) {
  if (args.length < 1 || args.length > 5) throw new Error(`a special takes 1 to 5 arguments, not ${args.length}`);
  return [...args.flatMap((a) => [PCD.PUSHNUMBER, a]), PCD.LSPEC1 + args.length - 1, special];
}
