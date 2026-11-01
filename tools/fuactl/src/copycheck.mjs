// [rc4l] The numbers fua_copy_prescan_report gives: the fills held to the whole-map scan, how many took the tic's end actor list too, what the tic's end scan or the change logs missed, and the first misses it described.
export function parsePrescanReport(text) {
  const m = /prescan: (\d+) fills checked, (\d+) with the actor list, (\d+) missed/.exec(text);
  const first = String(text ?? "").split(/\r?\n/).find((l) => /^\s+\S/.test(l));
  return { walked: m ? Number(m[1]) : 0, listed: m ? Number(m[2]) : 0, differed: m ? Number(m[3]) : 0, first: first ? first.trim() : "", found: !!m, text };
}
