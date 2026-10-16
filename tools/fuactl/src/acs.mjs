// [rc4l] A BEHAVIOR lump in the old ACS0 format, so a template map can run an OPEN script without an ACS compiler.
import { Buffer } from "node:buffer";

// [rc4l] The p-codes templates use, numbered as in p_acs.h; ACS0 writes every code and argument as a 32-bit word.
export const PCD = { TERMINATE: 1, PUSHNUMBER: 3, LSPEC1: 4, GOTO: 52, DELAYDIRECT: 56, BEGINPRINT: 85, ENDPRINT: 86, PRINTSTRING: 87,
  CONSOLECOMMAND: 134, MOREHUDMESSAGE: 159, ENDHUDMESSAGE: 161, SETCAMERATOTEXTURE: 269 };

// [rc4l] OPEN script 1 running `code` (words), with `strings` indexed from 0 as the script's string arguments; `{ open: false, number }` makes it a closed script that `puke number` runs.
export function acs0(code, strings = [], { open = true, number = 1 } = {}) {
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
  // [rc4l] The directory: one script, its type in the thousands (1001 is script 1 of type OPEN), then the string table after it.
  out.writeInt32LE(1, dirOffset);
  out.writeInt32LE((open ? 1000 : 0) + number, dirOffset + 4);
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

// [rc4l] The words of `Print(s:strings[i])`.
export function printString(i) {
  return [PCD.BEGINPRINT, PCD.PUSHNUMBER, i, PCD.PRINTSTRING, PCD.ENDPRINT];
}

// [rc4l] The words of `HudMessage(s:strings[i]; type, id, color, x, y, holdTime)`, the last three in map units as 16.16 fixed point.
export function hudMessage(i, { type = 0, id = 0, color = 0, x = 0.5, y = 0.5, hold = 1 } = {}) {
  const fixed = (v) => Math.round(v * 65536);
  return [PCD.BEGINPRINT, PCD.PUSHNUMBER, i, PCD.PRINTSTRING, PCD.MOREHUDMESSAGE,
    ...[type, id, color, fixed(x), fixed(y), fixed(hold)].flatMap((v) => [PCD.PUSHNUMBER, v]), PCD.ENDHUDMESSAGE];
}

// [rc4l] The words of `ConsoleCommand(strings[i])`, which the engine pops as three words, the string first.
export function consoleCommand(i) {
  return [PCD.PUSHNUMBER, i, PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 0, PCD.CONSOLECOMMAND];
}

// [rc4l] The words that jump to word `index` of the script, which ACS0 addresses by its byte offset in the lump.
export function gotoWord(index) {
  return [PCD.GOTO, 8 + 4 * index];
}
