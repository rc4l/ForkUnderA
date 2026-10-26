// [rc4l] A BEHAVIOR lump in the old ACS0 format, so a template map can run an OPEN script without an ACS compiler.
import { Buffer } from "node:buffer";

// [rc4l] The p-codes templates use, numbered as in p_acs.h; ACS0 writes every code and argument as a 32-bit word.
export const PCD = { TERMINATE: 1, PUSHNUMBER: 3, LSPEC1: 4, ADD: 14, EQ: 19, ASSIGNMAPVAR: 26, PUSHMAPVAR: 29, INCMAPVAR: 47, GOTO: 52,
  IFGOTO: 53, DROP: 54, DELAYDIRECT: 56, IFNOTGOTO: 79, BEGINPRINT: 85, ENDPRINT: 86, PRINTSTRING: 87, CONSOLECOMMAND: 134,
  MOREHUDMESSAGE: 159, OPTHUDMESSAGE: 160, ENDHUDMESSAGE: 161, GETSCREENWIDTH: 250, GETSCREENHEIGHT: 251, GETCVAR: 255,
  GETSECTORFLOORZ: 261, SETCAMERATOTEXTURE: 269, ENDLOG: 270, THINGCOUNTNAME: 288, PRINTNUMBER: 88, SETMUGSHOTSTATE: 341, CHECKPLAYERCAMERA: 344, CALLFUNC: 351, SETFONT: 165, CHANGESKY: 266,
  CHANGEFLOORDIRECT: 66, SETLINETEXTURE: 97, SETACTORPROPERTY: 245, THINGCOUNT: 59 };

// [rc4l] The ACS functions templates call through CALLFUNC, numbered as EACSFunctions in p_acs.cpp.
export const ACSF = { SetCVar: 53, LineAttack: 60, PlaySound: 61, CheckFlag: 75 };

// [rc4l] OPEN script 1 running `code` (words), with `strings` indexed from 0 as the script's string arguments; `{ open: false, number }` makes it a closed script that `puke number` runs.
export function acs0(code, strings = [], { open = true, number = 1 } = {}) {
  return acsScripts([{ code, open, number }], strings);
}

// [rc4l] Several scripts in one lump, each { code, open, number } as acs0 takes them, their code laid end to end and sharing the strings.
export function acsScripts(scripts, strings = []) {
  const code = scripts.flatMap((s) => s.code);
  const strBytes = strings.map((s) => Buffer.from(`${s}\0`, "latin1"));
  const strStart = 8 + 4 * code.length;
  const strLen = strBytes.reduce((n, b) => n + b.length, 0);
  const dirOffset = strStart + Math.ceil(strLen / 4) * 4;
  const out = Buffer.alloc(dirOffset + 4 + 12 * scripts.length + 4 + 4 * strings.length);
  out.write("ACS\0", 0, "latin1");
  out.writeInt32LE(dirOffset, 4);
  code.forEach((w, i) => out.writeInt32LE(w, 8 + 4 * i));
  const offsets = [];
  let at = strStart;
  for (const b of strBytes) { b.copy(out, at); offsets.push(at); at += b.length; }
  // [rc4l] The directory: each script, its type in the thousands (1001 is script 1 of type OPEN) and where its code starts, then the string table after it.
  out.writeInt32LE(scripts.length, dirOffset);
  let address = 8;
  scripts.forEach((s, i) => {
    const entry = dirOffset + 4 + 12 * i;
    out.writeInt32LE((s.open ?? true ? 1000 : 0) + (s.number ?? 1), entry);
    out.writeInt32LE(address, entry + 4);
    out.writeInt32LE(0, entry + 8);
    address += 4 * s.code.length;
  });
  const table = dirOffset + 4 + 12 * scripts.length;
  out.writeInt32LE(strings.length, table);
  offsets.forEach((o, i) => out.writeInt32LE(o, table + 4 + 4 * i));
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

// [rc4l] The words of `HudMessage(s:strings[i]; type, id, color, x, y, holdTime)`, the last three in map units as 16.16 fixed point, and with `fade` a fade-out of that many seconds.
export function hudMessage(i, { type = 0, id = 0, color = 0, x = 0.5, y = 0.5, hold = 1, fade = null } = {}) {
  const fixed = (v) => Math.round(v * 65536);
  return [PCD.BEGINPRINT, PCD.PUSHNUMBER, i, PCD.PRINTSTRING, PCD.MOREHUDMESSAGE,
    ...[type, id, color, fixed(x), fixed(y), fixed(hold)].flatMap((v) => [PCD.PUSHNUMBER, v]),
    ...(fade === null ? [] : [PCD.OPTHUDMESSAGE, PCD.PUSHNUMBER, fixed(fade)]), PCD.ENDHUDMESSAGE];
}

// [rc4l] The words of `ConsoleCommand(strings[i])`, which the engine pops as three words, the string first.
export function consoleCommand(i) {
  return [PCD.PUSHNUMBER, i, PCD.PUSHNUMBER, 0, PCD.PUSHNUMBER, 0, PCD.CONSOLECOMMAND];
}

// [rc4l] The words that jump to word `index` of the script, which ACS0 addresses by its byte offset in the lump.
export function gotoWord(index) {
  return [PCD.GOTO, 8 + 4 * index];
}

// [rc4l] The words of an ACS function called with these pushed arguments (string arguments as their string index), its result left on the stack.
export function callFunc(fn, args) {
  return [...args.flatMap((a) => [PCD.PUSHNUMBER, a]), PCD.CALLFUNC, args.length, fn];
}

// [Doruk-Aksoy] The words of `SetCVar(strings[i], GetCVar(strings[i]) + 1)`, its result dropped: a CVARINFO counter a scenario reads back.
export function incrementCVar(i) {
  return [PCD.PUSHNUMBER, i, PCD.PUSHNUMBER, i, PCD.GETCVAR, PCD.PUSHNUMBER, 1, PCD.ADD, PCD.CALLFUNC, 2, ACSF.SetCVar, PCD.DROP];
}

// [rc4l] A script built word by word with named jump targets, resolved to ACS0 byte offsets by words().
export class AcsScript {
  constructor() { this.parts = []; this.labels = new Map(); }
  // [rc4l] Words as they are, p-codes and their arguments.
  emit(...words) { this.parts.push(...words); return this; }
  // [rc4l] Names the next word as a jump target.
  label(name) { this.labels.set(name, this.parts.length); return this; }
  // [rc4l] A jump (GOTO, IFGOTO, IFNOTGOTO) to a named target, resolved once the script is done.
  jump(pcd, name) { this.parts.push(pcd, { label: name }); return this; }
  // [rc4l] The script's words, every jump target turned into its byte offset; a target never named throws.
  words() {
    return this.parts.map((w) => {
      if (typeof w === "number") return w;
      if (!this.labels.has(w.label)) throw new Error(`no label ${w.label}`);
      return 8 + 4 * this.labels.get(w.label);
    });
  }
}
