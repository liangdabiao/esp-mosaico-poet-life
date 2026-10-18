// Extract the game data constants from the vanilla-JS single-file game into
// JSON files for the ESP-Mosaico port. Usage: node extract_data.js <src.html> <out_dir>
const fs = require("fs");

const [src, outDir] = process.argv.slice(2);
const html = fs.readFileSync(src, "utf8");

// Locate the <script> body that holds the data constants.
const scriptStart = html.indexOf("const POETS");
if (scriptStart < 0) throw new Error("POETS not found");

// Brace/bracket-matched scanner: returns the text of the initializer starting
// at `start` (which must point at '{' or '['), stopping at the matching close.
function scanValue(start) {
  let depth = 0, inStr = null, esc = false;
  for (let i = start; i < html.length; i++) {
    const c = html[i];
    if (inStr) {
      if (esc) esc = false;
      else if (c === "\\") esc = true;
      else if (c === inStr) inStr = null;
      continue;
    }
    if (c === "'" || c === '"' || c === "`") { inStr = c; continue; }
    if (c === "{" || c === "[") depth++;
    else if (c === "}" || c === "]") {
      depth--;
      if (depth === 0) return html.slice(start, i + 1);
    }
  }
  throw new Error("unbalanced initializer at " + start);
}

function extractConst(name) {
  const marker = "const " + name + " =";
  const at = html.indexOf(marker);
  if (at < 0) throw new Error(name + " not found");
  let i = at + marker.length;
  while (html[i] === " ") i++;
  const literal = scanValue(i);
  // JS object literal (unquoted keys, occasional callbacks): eval it as data.
  return new Function("return (" + literal + ")")();
}

const data = {
  poets: extractConst("POETS"),
  eras: extractConst("ERAS"),
  events: extractConst("EVENTS"),
  specialEvents: extractConst("SPECIAL_EVENTS"),
  questionBank: extractConst("QUESTION_BANK"),
};

// 原作的 李白·千金散尽 把 result 写进了 effect 里（effect:{fame:12,poems:1,
// result:'店主…'}），浏览器上这条选项没有结算文案。移植时把它抬回选项层。
function hoistMisplacedResult(node) {
  if (Array.isArray(node)) return node.forEach(hoistMisplacedResult);
  if (!node || typeof node !== "object") return node;
  if (node.effect && typeof node.effect === "object"
      && typeof node.effect.result === "string") {
    if (!node.result) node.result = node.effect.result;
    delete node.effect.result;
  }
  Object.values(node).forEach(hoistMisplacedResult);
  return node;
}
hoistMisplacedResult(data.events);
hoistMisplacedResult(data.specialEvents);

for (const [name, value] of Object.entries(data)) {
  const file = outDir + "/" + name + ".json";
  fs.writeFileSync(file, JSON.stringify(value, null, 1), "utf8");
  console.log(file, Array.isArray(value) ? value.length + " items" : typeof value);
}
