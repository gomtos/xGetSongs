// Differential test: the same random cases go through the Kotlin originals (Oracle.java, on the jars of the sidecar image that
// `gradlew :server:sidecarImage` makes) and the TypeScript ports (src/logic); the answers must be identical. The Kotlin code is what
// the server runs, so this is what keeps the names of the preview and the checks of the screen the same as the server's.
//
//   node tools/differential/fuzz.mts [cases-per-kind] [seed]       (from the shell folder; XGS_SIDECAR_DIR names another image)
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { destinationFolder, folderName, format } from "../../src/logic/filename.ts";
import { classify } from "../../src/logic/input.ts";

const perKind = Number(process.argv[2] ?? 10000);
let seed = Number(process.argv[3] ?? 20261011) >>> 0;

function random(): number {
  // mulberry32
  seed = (seed + 0x6d2b79f5) >>> 0;
  let t = seed;
  t = Math.imul(t ^ (t >>> 15), t | 1);
  t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
}
const int = (low: number, high: number) => low + Math.floor(random() * (high - low + 1));
const pick = <T>(items: readonly T[]): T => items[int(0, items.length - 1)];
const chance = (probability: number) => random() < probability;

const ASCII = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
const PUNCT = " .-_()[]{}'&,;!@#$%^+=~`";
const FORBIDDEN = '\\/:*?"<>|';
const CONTROL = ["\u0000", "\u0001", "\t", "\n", "\r", "\u001b", "\u001c", "\u001f", "\u007f"];
const SPACES = ["\u00a0", "\u3000", "\u2003", "\u2028", "\ufeff", "\u200b", "\u180e", "\u0085", "\u2007", "\u202f"];
const KOREAN = "가나다라마바사아자차카타파하소연퇴사할게여기안";
const EMOJI = ["😀", "🎵", "🇰🇷", "👨‍👩‍👧", "𝒜"];
const LONE = ["\ud83d", "\ude00", "\udbff"];
const DEVICES = ["CON", "PRN", "AUX", "NUL", "COM1", "LPT9", "con", "Nul", "com0", "LPT10", "COM9", "lpt1"];

function token(): string {
  const roll = random();
  if (roll < 0.3) return ASCII[int(0, ASCII.length - 1)];
  if (roll < 0.42) return PUNCT[int(0, PUNCT.length - 1)];
  if (roll < 0.52) return FORBIDDEN[int(0, FORBIDDEN.length - 1)];
  if (roll < 0.58) return pick(CONTROL);
  if (roll < 0.66) return pick(SPACES);
  if (roll < 0.82) return KOREAN[int(0, KOREAN.length - 1)];
  if (roll < 0.9) return pick(EMOJI);
  if (roll < 0.93) return pick(LONE);
  return pick(DEVICES) + pick(["", ".", " .", ". ", ".x", "..", " "]);
}

function text(): string {
  const shape = random();
  if (shape < 0.08) return "";
  if (shape < 0.18) return pick(DEVICES) + pick(["", ".", ".x", " .txt", ". ", ".tar.gz"]);
  if (shape < 0.3) {
    // long runs that sit near the truncation limits (80 for an artist or a folder, 180 for a whole name)
    const unit = pick(["a", "가", "😀", "A.", "🎵", "ab"]);
    return unit.repeat(int(60, 220)) + (chance(0.5) ? token() : "");
  }
  const length = shape < 0.7 ? int(1, 12) : int(12, 120);
  let result = "";
  for (let index = 0; index < length; index += 1) result += token();
  return result;
}

const hex = (value: string | null): string => {
  if (value === null) return "-";
  if (value === "") return ".";
  let out = "";
  for (let index = 0; index < value.length; index += 1) out += value.charCodeAt(index).toString(16).padStart(4, "0");
  return out;
};

const nullable = (): string | null => (chance(0.15) ? null : text());

// ---- URL-like input for the classifier ----
const ID_CHARS = ASCII + "_-";
const idOf = (length: number) => Array.from({ length }, () => ID_CHARS[int(0, ID_CHARS.length - 1)]).join("");
const videoId = () => (chance(0.85) ? idOf(11) : idOf(int(0, 14)) + (chance(0.2) ? "é" : ""));
const playlistId = () => pick(["PL", "UU", "LL", "FL", "OL", "RD", "PL", "XX"]) + idOf(int(8, 36));

function classifierInput(): string {
  const shape = random();
  if (shape < 0.12) return pick(["", " ", "\t\n", "hello world", "PLshort", "PLabc; rm -rf /", "youtu.be", "a.b", "/", "."]);
  if (shape < 0.3) return (chance(0.5) ? playlistId() : videoId()) + (chance(0.1) ? " " : "");
  const scheme = pick(["https://", "http://", "", "HTTPS://", "ftp://", "//"]);
  const host = pick([
    "www.youtube.com", "youtube.com", "m.youtube.com", "music.youtube.com", "youtu.be", "WWW.YOUTUBE.COM", "www.youtube.com.evil.com",
    "evil.com", "youtube.com@evil.com", "www.youtube.com:443", "user@youtu.be", "youtube.co", "",
  ]);
  const path = pick(["/watch", "/playlist", `/shorts/${videoId()}`, `/embed/${videoId()}`, `/live/${videoId()}`, "/", "", `/${videoId()}`, "/watch/", "//watch", "/shorts"]);
  const params: string[] = [];
  if (chance(0.7)) params.push(`v=${videoId()}`);
  if (chance(0.6)) params.push(`list=${playlistId()}`);
  for (const extra of ["t=10s", "index=3", "si=abc", "&", "=", "v", "list=", "v=", "x=y=z"]) if (chance(0.15)) params.push(extra);
  for (let index = params.length - 1; index > 0; index -= 1) {
    const other = int(0, index);
    [params[index], params[other]] = [params[other], params[index]];
  }
  const query = params.length > 0 ? "?" + params.join("&") : "";
  const fragment = chance(0.1) ? "#frag" : "";
  const padding = chance(0.1) ? " " : "";
  return padding + scheme + host + path + query + fragment + padding;
}

// ---- the cases ----
interface Case {
  line: string;
  expected: string;
}
const cases: Case[] = [];

for (let index = 0; index < perKind; index += 1) {
  const rank = chance(0.03) ? pick([0, 1000, -5, 5000]) : int(1, 999);
  const includeRank = chance(0.5);
  const artist = text();
  const title = text();
  let expected: string;
  try {
    expected = "OK " + hex(format(rank, artist, title, includeRank));
  } catch {
    expected = "ERR";
  }
  cases.push({ line: `F\t${rank}\t${includeRank ? 1 : 0}\t${hex(artist)}\t${hex(title)}`, expected });
}
for (let index = 0; index < perKind; index += 1) {
  const title = nullable();
  cases.push({ line: `D\t${hex(title)}`, expected: "OK " + hex(folderName(title)) });
}
for (let index = 0; index < perKind; index += 1) {
  const isPlaylist = chance(0.5);
  const title = nullable();
  const typed = nullable();
  const folder = destinationFolder(isPlaylist, title, typed);
  cases.push({ line: `G\t${isPlaylist ? 1 : 0}\t${hex(title)}\t${hex(typed)}`, expected: folder === null ? "NULL" : "OK " + hex(folder) });
}
for (let index = 0; index < perKind; index += 1) {
  const raw = classifierInput();
  const result = classify(raw);
  let expected: string;
  if (!result.ok) expected = `REJECT ${result.reason}`;
  else if (result.input.kind === "video") expected = `V ${hex(result.input.id)}`;
  else expected = `P ${hex(result.input.id)} ${hex(result.input.alsoVideoId)}`;
  cases.push({ line: `C\t${hex(raw)}`, expected });
}

const image = process.env.XGS_SIDECAR_DIR ?? fileURLToPath(new URL("../../../server/build/sidecar-image", import.meta.url));
const run = spawnSync(`${image}/runtime/bin/java.exe`, ["-cp", `${image}/lib/*`, fileURLToPath(new URL("Oracle.java", import.meta.url))], {
  input: cases.map((entry) => entry.line).join("\n") + "\n",
  encoding: "ascii",
  maxBuffer: 1 << 28,
});
if (run.status !== 0) throw new Error(`the oracle failed: ${run.stderr}`);
const answers = run.stdout.split("\n");
const decode = (hexText: string) => {
  if (hexText === "-") return "(null)";
  if (hexText === ".") return "(empty)";
  let out = "";
  for (let index = 0; index < hexText.length; index += 4) out += String.fromCharCode(parseInt(hexText.slice(index, index + 4), 16));
  return JSON.stringify(out);
};

let mismatches = 0;
const kinds: Record<string, { cases: number; mismatches: number }> = {};
cases.forEach((entry, index) => {
  const kind = entry.line[0];
  kinds[kind] ??= { cases: 0, mismatches: 0 };
  kinds[kind].cases += 1;
  if (answers[index] !== entry.expected) {
    mismatches += 1;
    kinds[kind].mismatches += 1;
    if (mismatches <= 12) {
      console.log(`MISMATCH #${index} [${kind}] input: ${entry.line.split("\t").slice(1).map(decode).join(" , ")}`);
      console.log(`   kotlin: ${answers[index]?.startsWith("OK ") ? "OK " + decode(answers[index].slice(3)) : answers[index]}`);
      console.log(`   port:   ${entry.expected.startsWith("OK ") ? "OK " + decode(entry.expected.slice(3)) : entry.expected}`);
    }
  }
});
console.log(`${cases.length} cases (${perKind} per kind), ${mismatches} mismatches`, JSON.stringify(kinds));
if (mismatches > 0) process.exitCode = 1;
