// [rc4l] What fua_seams prints: each seam entered (a table line with its entries) and the entries a thread its owner does not allow made, with the first such seam.
export function parseSeamReport(text) {
  const total = /^(\d+) violations(?:, the first in (\S+) on the (\w+) thread(?: in (\S+?)[;\s])?)?/m.exec(text);
  const lines = String(text).split(/\r?\n/);
  const entered = lines.filter((l) => /^\s*\S+\/\S+\s/.test(l)).length;
  // [rc4l] fua_seams' own rows of where disallowed threads entered, the most entered first.
  const sites = lines.filter((l) => /^\s+violated /.test(l)).map((l) => l.trim().replace(/^violated /, ""));
  return { walked: entered, differed: total ? Number(total[1]) : 0, first: total && total[2] ? `${total[2]} (${total[3]}${total[4] ? `, ${total[4]}` : ""})` : "", sites, found: !!total, text };
}
