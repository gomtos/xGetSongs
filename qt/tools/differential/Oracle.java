import com.xgetsongs.engine.lyrics.LyricsCandidate;
import com.xgetsongs.engine.lyrics.LyricsMatcher;
import com.xgetsongs.engine.lyrics.LyricsQuery;
import com.xgetsongs.engine.tags.Ffmetadata;
import com.xgetsongs.engine.tags.TrackTags;
import com.xgetsongs.engine.tools.ToolPaths;
import com.xgetsongs.engine.ytdlp.ErrorClassifier;
import com.xgetsongs.engine.ytdlp.FfmpegCommands;
import com.xgetsongs.engine.ytdlp.YtDlpCommands;
import com.xgetsongs.engine.ytdlp.Failure;
import com.xgetsongs.engine.ytdlp.ProgressParser;
import com.xgetsongs.engine.ytdlp.ProgressUpdate;
import com.xgetsongs.engine.ytdlp.VideoInfo;
import com.xgetsongs.engine.ytdlp.VideoInfoFile;
import com.xgetsongs.shared.filename.FilenameFormatter;
import com.xgetsongs.shared.input.ClassifyResult;
import com.xgetsongs.shared.input.InputClassifier;
import com.xgetsongs.shared.input.ParsedInput;
import com.xgetsongs.shared.input.RejectReason;
import com.xgetsongs.shared.log.LogRedaction;
import com.xgetsongs.shared.lyrics.LyricsExtractor;
import com.xgetsongs.shared.title.ParsedTrack;
import com.xgetsongs.shared.title.TitleParser;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;
import java.text.Normalizer;
import java.util.Locale;
import java.util.TreeMap;
import kotlin.text.CharsKt;
import kotlin.text.StringsKt;
import kotlinx.serialization.json.Json;
import kotlinx.serialization.json.JsonArray;
import kotlinx.serialization.json.JsonElement;
import kotlinx.serialization.json.JsonNull;
import kotlinx.serialization.json.JsonObject;
import kotlinx.serialization.json.JsonPrimitive;

/**
 * The Kotlin side of the Qt port's differential test (qt/tools/differential/run.ps1): the answers of the Kotlin originals
 * (shared-jvm.jar and the Kotlin standard library of the sidecar image) on the JVM. The C++ side is xgs-diff.exe.
 *
 * A string is the hex of its UTF-16 units (4 digits each), "." for the empty string and "-" for null.
 *
 *   java -cp "server/build/sidecar-image/lib/*" qt/tools/differential/Oracle.java chartable
 *       one line per UTF-16 unit 0000..FFFF: "w l d u c" = Char.isWhitespace(), isLetter(), isLetterOrDigit() as 0/1,
 *       uppercaseChar() and lowercaseChar() as 4 hex digits
 *   ... Oracle.java constants
 *       one line per user-facing constant, sorted by name: "name hex"
 *   ... Oracle.java unicodecase
 *       one line per code point 0..10FFFF except the surrogates D800..DFFF: "cp lower upper" = the code point in hex, and
 *       String.lowercase() and String.uppercase() of the one-code-point string as hex units
 *   ... Oracle.java lettertable
 *       one line per code point 0..10FFFF except the surrogates D800..DFFF: "cp d" = the code point in hex and
 *       Character.isLetterOrDigit(int) as 0/1 (kind B)
 *   ... Oracle.java nfkctable
 *       one line per code point 0..10FFFF except the surrogates: "cp hex" = the code point in hex and
 *       java.text.Normalizer.normalize(s, Normalizer.Form.NFKC) of the one-code-point string (kind Z)
 *   ... Oracle.java                (no argument)
 *       one answer per case line on stdin (tab-separated, the kind letter first):
 *         S op text          op = lower | upper | trim | trimStart | trimEnd   -> OK hex
 *                            op = blank                                    -> 0 | 1
 *         S ci-starts|ci-ends|ci-contains text other          -> 0 | 1   (ignoreCase = true)
 *         S ci-starts-from text prefix from                   -> 0 | 1   (startsWith(prefix, from, ignoreCase = true))
 *         S ci-index|index text needle from                   -> decimal index, -1 when absent
 *         S ci-replace|replace text old new                   -> OK hex
 *         S eqic a b                                          -> 0 | 1   (two single-unit strings, Char.equals(ignoreCase))
 *         C raw              InputClassifier.classify(raw)  -> P id alsoVideoId | V id | REJECT NAME
 *         F rank includeRank artist title    FilenameFormatter.format  -> OK hex | ERR
 *         D title                            FilenameFormatter.folderName  -> OK hex
 *         G isPlaylist playlistTitle typed   FilenameFormatter.destinationFolder  -> OK hex | NULL
 *         T rawTitle channel metaArtist metaTrack   TitleParser.parse  -> OK artist title HIGH | MEDIUM | LOW
 *         L description                      LyricsExtractor.extract  -> OK hex | NULL
 *         Y text                             LyricsExtractor.tidy  -> OK hex | NULL
 *         R text                             LogRedaction.redactPaths  -> OK hex
 *         N text name1 [name2 ...]           LogRedaction.redact(text, names)  -> OK hex
 *         H text                             Normalizer.normalize(text, Normalizer.Form.NFKC)  -> OK hex
 *         O text                             String.toDoubleOrNull()  -> NULL | the 16 hex digits of
 *                                            Double.doubleToRawLongBits
 *         J text                             Json.Default.parseToJsonElement(text)  -> ERR (anything thrown) | the
 *                                            canonical dump (dumpJson)
 *         E line1 [line2 ...]                ErrorClassifier.classify(lines)  -> FATAL | UNAVAILABLE | TRANSIENT | OTHER, a
 *                                            space and the message as hex (no lines: a case with no fields)
 *         Q line                             ProgressParser.parse(line)  -> NULL | P - (percent null) | P and the 16 hex
 *                                            digits of Double.doubleToRawLongBits(percent)
 *         V bytes                            VideoInfoFile.read(file) of a temporary file holding the bytes (2 hex digits
 *                                            each, "." for none)  -> album description duration (the strings as hex, "-"
 *                                            for null; the duration in decimal or "-"), or THROW and the class of what
 *                                            read threw
 *         K rp|rv ytDlp ffmpeg js url        YtDlpCommands.resolvePlaylist | resolveVideo(ToolPaths(Path.of(..)), url)
 *         K dl ytDlp ffmpeg js url dir id    YtDlpCommands.download
 *         K tag ffmpeg input cover meta out  FfmpegCommands.tag; the answer is OK and every argument as hex, ERR when it
 *                                            throws IllegalArgumentException (a path is hex, "-" for null; BADPATH when
 *                                            Path.of(text).toString() differs from text)
 *         A title artist album albumArtist track comment lyrics     Ffmetadata.render(TrackTags(...))  -> OK hex
 *         I tv title | I av artist           LyricsMatcher.titleVariants | artistVariants  -> OK and every variant as hex
 *         I norm text                        LyricsMatcher.normalize  -> OK hex
 *         I match query candidate            LyricsMatcher.isMatch  -> 0 | 1
 *         I pick query n candidate...        LyricsMatcher.pick  -> the index of the candidate picked (by identity), -1 for
 *                                            null. A query is 4 fields (artist title album durationSeconds: hex, album "-"
 *                                            for null, the duration in decimal or "-"); a candidate is 6 fields (trackName
 *                                            artistName albumName duration instrumental plainLyrics: hex or "-", the duration
 *                                            as the 16 hex digits of Double.doubleToRawLongBits or "-", instrumental 0 | 1 | -)
 *         W op fields...                     paths and files, one op per case (answerW): pn text, pc dir name, pf path,
 *                                            px path, ln bytes chunkSize... (the lines of a child's output, answerLines)
 *
 * An observation, not a gate (Q2b Task 2 Step 4):
 *   xgs-diff arglists 2000 20261118 | ... Oracle.java argv <path of xgs-test-child.exe>
 *       starts the helper with each argument list through ProcessBuilder and counts the lists its C runtime received
 *       differently (argv below)
 *
 * Only for qt/tools/differential/gen-tables.ps1:
 *   ... Oracle.java casetable      Unicode case data as the JVM's public API reports it: String.uppercase()/lowercase() of
 *                                  each single unit where it is not uppercaseChar()/lowercaseChar() (special casing), and
 *                                  the case mappings of the code points above U+FFFF
 *   ... Oracle.java lettertable    (also kind B) the code points above U+FFFF whose answer differs from Qt's are tabled
 */
public class Oracle {
    static String dec(String hex) {
        if (hex.equals("-")) return null;
        if (hex.equals(".")) return "";
        StringBuilder out = new StringBuilder();
        for (int i = 0; i < hex.length(); i += 4) out.append((char) Integer.parseInt(hex.substring(i, i + 4), 16));
        return out.toString();
    }

    // A path field of a kind K case: Path.of(text), "-" for null. The generator only writes paths that Path gives back unchanged;
    // one that comes back different would hide a generator problem behind a matching answer, so the case answers BADPATH.
    static final class BadPath extends RuntimeException {}

    static java.nio.file.Path path(String hex) {
        if (hex.equals("-")) return null;
        String text = dec(hex);
        java.nio.file.Path path = java.nio.file.Path.of(text);
        if (!path.toString().equals(text)) throw new BadPath();
        return path;
    }

    // the bytes of a kind V case: 2 hex digits each, "." for none
    static byte[] decBytes(String hex) {
        if (hex.equals(".")) return new byte[0];
        byte[] out = new byte[hex.length() / 2];
        for (int i = 0; i < out.length; i++) out[i] = (byte) Integer.parseInt(hex.substring(2 * i, 2 * i + 2), 16);
        return out;
    }

    // a query of kind I: artist title album durationSeconds, from part[at]
    static LyricsQuery lyricsQuery(String[] part, int at) {
        Integer duration = part[at + 3].equals("-") ? null : Integer.valueOf(Integer.parseInt(part[at + 3]));
        return new LyricsQuery(dec(part[at]), dec(part[at + 1]), dec(part[at + 2]), duration);
    }

    // a candidate of kind I: trackName artistName albumName duration instrumental plainLyrics, from part[at]
    static LyricsCandidate lyricsCandidate(String[] part, int at) {
        Double duration = part[at + 3].equals("-") ? null : Double.valueOf(Double.longBitsToDouble(Long.parseUnsignedLong(part[at + 3], 16)));
        Boolean instrumental = part[at + 4].equals("-") ? null : Boolean.valueOf(part[at + 4].equals("1"));
        return new LyricsCandidate(dec(part[at]), dec(part[at + 1]), dec(part[at + 2]), duration, instrumental, dec(part[at + 5]));
    }

    // kind V's temporary file (created at the first V case, deleted after the last)
    static java.nio.file.Path videoInfoFile;

    static String enc(String text) {
        if (text == null) return "-";
        if (text.isEmpty()) return ".";
        StringBuilder out = new StringBuilder();
        for (int i = 0; i < text.length(); i++) out.append(hex4(text.charAt(i)));
        return out.toString();
    }

    static final char[] DIGITS = "0123456789abcdef".toCharArray();

    static String hex4(int unit) {
        return new String(new char[] {DIGITS[(unit >> 12) & 15], DIGITS[(unit >> 8) & 15], DIGITS[(unit >> 4) & 15], DIGITS[unit & 15]});
    }

    static char bit(boolean value) {
        return value ? '1' : '0';
    }

    // Kotlin's Char.isWhitespace() is an ordinary function: the static method of the kotlin.text.CharsKt facade is called.
    // isLetter(), isLetterOrDigit(), uppercaseChar() and lowercaseChar() are inline-only (not callable from Java); on the JVM
    // they compile to exactly Character.isLetter(char), isLetterOrDigit(char), toUpperCase(char) and toLowerCase(char).
    static void chartable(StringBuilder out) {
        for (int unit = 0; unit <= 0xFFFF; unit++) {
            char c = (char) unit;
            out.append(bit(CharsKt.isWhitespace(c))).append(' ')
                .append(bit(Character.isLetter(c))).append(' ')
                .append(bit(Character.isLetterOrDigit(c))).append(' ')
                .append(hex4(Character.toUpperCase(c))).append(' ')
                .append(hex4(Character.toLowerCase(c))).append('\n');
        }
    }

    static void constants(StringBuilder out) {
        TreeMap<String, String> constants = new TreeMap<>();
        for (RejectReason reason : RejectReason.values()) constants.put("RejectReason." + reason.name(), reason.getMessage());
        constants.put("FilenameFormatter.UNTITLED_PLAYLIST", FilenameFormatter.UNTITLED_PLAYLIST);
        constants.put("TitleParser.UNKNOWN_ARTIST", TitleParser.UNKNOWN_ARTIST);
        constants.put("LogRedaction.PATH_PLACEHOLDER", LogRedaction.PATH_PLACEHOLDER);
        constants.put("LogRedaction.FILE_NAME_PLACEHOLDER", LogRedaction.FILE_NAME_PLACEHOLDER);
        // includeRank defaults to true in Kotlin; Java has to pass it.
        constants.put("FilenameFormatter.format(1,a,)", FilenameFormatter.INSTANCE.format(1, "a", "", true));
        constants.put("FilenameFormatter.folderName(null)", FilenameFormatter.INSTANCE.folderName(null));
        constants.put("FilenameFormatter.format(1,a,b)", FilenameFormatter.INSTANCE.format(1, "a", "b", true));
        for (var entry : constants.entrySet()) out.append(entry.getKey()).append(' ').append(enc(entry.getValue())).append('\n');
    }

    // String.lowercase()/uppercase() are inline-only in Kotlin and compile to exactly these JDK calls.
    static String kotlinLowercase(String s) {
        return s.toLowerCase(Locale.ROOT);
    }

    static String kotlinUppercase(String s) {
        return s.toUpperCase(Locale.ROOT);
    }

    static void unicodecase(StringBuilder out) {
        for (int cp = 0; cp <= 0x10FFFF; cp++) {
            if (cp >= 0xD800 && cp <= 0xDFFF) continue;
            String one = new String(Character.toChars(cp));
            out.append(Integer.toHexString(cp)).append(' ').append(enc(kotlinLowercase(one))).append(' ')
                .append(enc(kotlinUppercase(one))).append('\n');
        }
    }

    static void casetable(StringBuilder out) {
        for (int unit = 0; unit <= 0xFFFF; unit++) {
            String one = String.valueOf((char) unit);
            String upper = kotlinUppercase(one);
            String lower = kotlinLowercase(one);
            if (!upper.equals(String.valueOf(Character.toUpperCase((char) unit)))) out.append("U ").append(hex4(unit)).append(' ').append(enc(upper)).append('\n');
            if (!lower.equals(String.valueOf(Character.toLowerCase((char) unit)))) out.append("L ").append(hex4(unit)).append(' ').append(enc(lower)).append('\n');
        }
        for (int cp = 0x10000; cp <= 0x10FFFF; cp++) {
            int upper = Character.toUpperCase(cp);
            int lower = Character.toLowerCase(cp);
            if (upper == cp && lower == cp) continue;
            String one = new String(Character.toChars(cp));
            out.append("P ").append(Integer.toHexString(cp)).append(' ').append(Integer.toHexString(upper)).append(' ')
                .append(Integer.toHexString(lower)).append(' ').append(enc(kotlinUppercase(one))).append(' ')
                .append(enc(kotlinLowercase(one))).append('\n');
        }
    }

    static void lettertable(StringBuilder out) {
        for (int cp = 0; cp <= 0x10FFFF; cp++) {
            if (cp >= 0xD800 && cp <= 0xDFFF) continue;
            out.append(Integer.toHexString(cp)).append(' ').append(bit(Character.isLetterOrDigit(cp))).append('\n');
        }
    }

    static String nfkc(String s) {
        return Normalizer.normalize(s, Normalizer.Form.NFKC);
    }

    static void nfkctable(StringBuilder out) {
        for (int cp = 0; cp <= 0x10FFFF; cp++) {
            if (cp >= 0xD800 && cp <= 0xDFFF) continue;
            out.append(Integer.toHexString(cp)).append(' ').append(enc(nfkc(new String(Character.toChars(cp))))).append('\n');
        }
    }

    // The canonical dump of kind J (the same on both sides): N (JsonNull), S<hex> (a string primitive), L<hex> (the content
    // of any other primitive), O{<key hex>:<value>,...} in the order of entries, A[<value>,...]. Iterative, so that the
    // dump of a deep document cannot overflow the stack.
    static String dumpJson(JsonElement root) {
        StringBuilder out = new StringBuilder();
        java.util.ArrayDeque<Object> work = new java.util.ArrayDeque<>();
        work.push(root);
        while (!work.isEmpty()) {
            Object item = work.pop();
            if (item instanceof String text) {
                out.append(text);
            } else if (item instanceof JsonNull) {
                out.append('N');
            } else if (item instanceof JsonPrimitive primitive) {
                out.append(primitive.isString() ? 'S' : 'L').append(enc(primitive.getContent()));
            } else if (item instanceof JsonObject object) {
                java.util.List<Object> parts = new java.util.ArrayList<>();
                parts.add("O{");
                boolean first = true;
                for (var entry : object.entrySet()) {
                    parts.add((first ? "" : ",") + enc(entry.getKey()) + ":");
                    parts.add(entry.getValue());
                    first = false;
                }
                parts.add("}");
                for (int k = parts.size() - 1; k >= 0; k--) work.push(parts.get(k));
            } else if (item instanceof JsonArray array) {
                java.util.List<Object> parts = new java.util.ArrayList<>();
                parts.add("A[");
                for (int k = 0; k < array.size(); k++) {
                    if (k > 0) parts.add(",");
                    parts.add(array.get(k));
                }
                parts.add("]");
                for (int k = parts.size() - 1; k >= 0; k--) work.push(parts.get(k));
            } else {
                out.append('?');
            }
        }
        return out.toString();
    }

    public static void main(String[] args) throws Exception {
        StringBuilder out = new StringBuilder();
        if (args.length > 0) {
            switch (args[0]) {
                case "chartable" -> chartable(out);
                case "constants" -> constants(out);
                case "unicodecase" -> unicodecase(out);
                case "casetable" -> casetable(out);
                case "lettertable" -> lettertable(out);
                case "nfkctable" -> nfkctable(out);
                case "argv" -> argv(args[1], out);
                default -> throw new IllegalArgumentException("unknown mode " + args[0]);
            }
            print(out);
            return;
        }
        BufferedReader in = new BufferedReader(new InputStreamReader(System.in, StandardCharsets.US_ASCII));
        for (String line = in.readLine(); line != null; line = in.readLine()) {
            if (line.isEmpty()) continue;
            String[] part = line.split("\t", -1);
            switch (part[0]) {
                case "S": {
                    String text = dec(part[2]);
                    // trim()/trimStart()/trimEnd() on a String are inline-only; they call these CharSequence functions of the
                    // kotlin.text.StringsKt facade and convert the result with toString(). isBlank() is an ordinary function.
                    switch (part[1]) {
                        case "lower" -> out.append("OK ").append(enc(kotlinLowercase(text)));
                        case "upper" -> out.append("OK ").append(enc(kotlinUppercase(text)));
                        case "trim" -> out.append("OK ").append(enc(StringsKt.trim((CharSequence) text).toString()));
                        case "trimStart" -> out.append("OK ").append(enc(StringsKt.trimStart((CharSequence) text).toString()));
                        case "trimEnd" -> out.append("OK ").append(enc(StringsKt.trimEnd((CharSequence) text).toString()));
                        case "blank" -> out.append(bit(StringsKt.isBlank(text)));
                        // The ignoreCase family: the real kotlin.text functions (the ordinary, non-inline ones).
                        case "ci-starts" -> out.append(bit(StringsKt.startsWith(text, dec(part[3]), true)));
                        case "ci-starts-from" -> out.append(bit(StringsKt.startsWith(text, dec(part[3]), Integer.parseInt(part[4]), true)));
                        case "ci-ends" -> out.append(bit(StringsKt.endsWith(text, dec(part[3]), true)));
                        case "ci-contains" -> out.append(bit(StringsKt.contains(text, dec(part[3]), true)));
                        case "ci-index" -> out.append(StringsKt.indexOf(text, dec(part[3]), Integer.parseInt(part[4]), true));
                        case "index" -> out.append(StringsKt.indexOf(text, dec(part[3]), Integer.parseInt(part[4]), false));
                        case "ci-replace" -> out.append("OK ").append(enc(StringsKt.replace(text, dec(part[3]), dec(part[4]), true)));
                        case "replace" -> out.append("OK ").append(enc(StringsKt.replace(text, dec(part[3]), dec(part[4]), false)));
                        case "eqic" -> out.append(bit(CharsKt.equals(text.charAt(0), dec(part[3]).charAt(0), true)));
                        default -> out.append("?");
                    }
                    break;
                }
                case "F":
                    try {
                        String name = FilenameFormatter.INSTANCE.format(Integer.parseInt(part[1]), dec(part[3]), dec(part[4]), part[2].equals("1"));
                        out.append("OK ").append(enc(name));
                    } catch (IllegalArgumentException e) {
                        out.append("ERR");
                    }
                    break;
                case "D":
                    out.append("OK ").append(enc(FilenameFormatter.INSTANCE.folderName(dec(part[1]))));
                    break;
                case "G": {
                    String folder = FilenameFormatter.INSTANCE.destinationFolder(part[1].equals("1"), dec(part[2]), dec(part[3]));
                    out.append(folder == null ? "NULL" : "OK " + enc(folder));
                    break;
                }
                case "T": {
                    ParsedTrack parsed = TitleParser.INSTANCE.parse(dec(part[1]), dec(part[2]), dec(part[3]), dec(part[4]));
                    out.append("OK ").append(enc(parsed.getArtist())).append(' ').append(enc(parsed.getTitle())).append(' ')
                        .append(parsed.getConfidence().name());
                    break;
                }
                case "L": {
                    String lyrics = LyricsExtractor.INSTANCE.extract(dec(part[1]));
                    out.append(lyrics == null ? "NULL" : "OK " + enc(lyrics));
                    break;
                }
                case "Y": {
                    String lyrics = LyricsExtractor.INSTANCE.tidy(dec(part[1]));
                    out.append(lyrics == null ? "NULL" : "OK " + enc(lyrics));
                    break;
                }
                case "R":
                    out.append("OK ").append(enc(LogRedaction.INSTANCE.redactPaths(dec(part[1]))));
                    break;
                case "N": {
                    java.util.List<String> names = new java.util.ArrayList<>();
                    for (int k = 2; k < part.length; k++) names.add(dec(part[k]));
                    out.append("OK ").append(enc(LogRedaction.INSTANCE.redact(dec(part[1]), names)));
                    break;
                }
                case "H":
                    out.append("OK ").append(enc(nfkc(dec(part[1]))));
                    break;
                case "O": {
                    // String.toDoubleOrNull() is an ordinary function of the kotlin.text.StringsKt facade.
                    Double value = StringsKt.toDoubleOrNull(dec(part[1]));
                    out.append(value == null ? "NULL" : String.format(Locale.ROOT, "%016x", Double.doubleToRawLongBits(value)));
                    break;
                }
                case "J": {
                    String dumped;
                    try {
                        dumped = dumpJson(Json.Default.parseToJsonElement(dec(part[1])));
                    } catch (Throwable e) {
                        // any exception (and an error such as a stack overflow, should a document be deep enough)
                        dumped = "ERR";
                    }
                    out.append(dumped);
                    break;
                }
                case "E": {
                    // the stderr lines (a field is one line; the empty field of a case without lines is skipped)
                    java.util.List<String> lines = new java.util.ArrayList<>();
                    for (int k = 1; k < part.length; k++) if (!part[k].isEmpty()) lines.add(dec(part[k]));
                    Failure failure = ErrorClassifier.INSTANCE.classify(lines);
                    out.append(failure.getKind().name()).append(' ').append(enc(failure.getMessage()));
                    break;
                }
                case "Q": {
                    ProgressUpdate.Downloading update = ProgressParser.INSTANCE.parse(dec(part[1]));
                    if (update == null) out.append("NULL");
                    else if (update.getPercent() == null) out.append("P -");
                    else out.append("P ").append(String.format(Locale.ROOT, "%016x", Double.doubleToRawLongBits(update.getPercent())));
                    break;
                }
                case "V": {
                    // one temporary file under the JVM's temp directory, rewritten for each case and deleted at the end
                    if (videoInfoFile == null) videoInfoFile = java.nio.file.Files.createTempFile("xgs-diff-", ".info.json");
                    java.nio.file.Files.write(videoInfoFile, decBytes(part[1]));
                    try {
                        VideoInfo info = VideoInfoFile.INSTANCE.read(videoInfoFile);
                        out.append(enc(info.getAlbum())).append(' ').append(enc(info.getDescription())).append(' ')
                            .append(info.getDuration() == null ? "-" : info.getDuration().toString());
                    } catch (Throwable e) {
                        // read promises never to throw; should it (an Error such as a stack overflow), say what it threw
                        out.append("THROW ").append(e.getClass().getName());
                    }
                    break;
                }
                case "K": {
                    try {
                        java.util.List<String> command;
                        if (part[1].equals("tag")) {
                            command = FfmpegCommands.INSTANCE.tag(path(part[2]), path(part[3]), path(part[4]), path(part[5]), path(part[6]));
                        } else {
                            ToolPaths tools = new ToolPaths(path(part[2]), path(part[3]), path(part[4]));
                            String url = dec(part[5]);
                            command = switch (part[1]) {
                                case "rp" -> YtDlpCommands.INSTANCE.resolvePlaylist(tools, url);
                                case "rv" -> YtDlpCommands.INSTANCE.resolveVideo(tools, url);
                                default -> YtDlpCommands.INSTANCE.download(tools, url, path(part[6]), dec(part[7]));
                            };
                        }
                        out.append("OK");
                        for (String argument : command) out.append(' ').append(enc(argument));
                    } catch (BadPath e) {
                        out.append("BADPATH");
                    } catch (IllegalArgumentException e) {
                        out.append("ERR");
                    }
                    break;
                }
                case "A": {
                    TrackTags tags = new TrackTags(dec(part[1]), dec(part[2]), dec(part[3]), dec(part[4]), Integer.parseInt(part[5]),
                        dec(part[6]), dec(part[7]));
                    out.append("OK ").append(enc(Ffmetadata.INSTANCE.render(tags)));
                    break;
                }
                case "I": {
                    switch (part[1]) {
                        case "tv", "av" -> {
                            java.util.List<String> variants = part[1].equals("tv") ? LyricsMatcher.INSTANCE.titleVariants(dec(part[2]))
                                : LyricsMatcher.INSTANCE.artistVariants(dec(part[2]));
                            out.append("OK");
                            for (String variant : variants) out.append(' ').append(enc(variant));
                        }
                        case "norm" -> out.append("OK ").append(enc(LyricsMatcher.INSTANCE.normalize(dec(part[2]))));
                        case "match" -> out.append(bit(LyricsMatcher.INSTANCE.isMatch(lyricsQuery(part, 2), lyricsCandidate(part, 6))));
                        case "pick" -> {
                            LyricsQuery query = lyricsQuery(part, 2);
                            int count = Integer.parseInt(part[6]);
                            java.util.List<LyricsCandidate> candidates = new java.util.ArrayList<>();
                            for (int k = 0; k < count; k++) candidates.add(lyricsCandidate(part, 7 + 6 * k));
                            LyricsCandidate picked = LyricsMatcher.INSTANCE.pick(query, candidates);
                            // indexOfFirst { it === picked }: by identity (a data class's equals would find an equal earlier one)
                            int index = -1;
                            for (int k = 0; k < count; k++) if (candidates.get(k) == picked) { index = k; break; }
                            out.append(index);
                        }
                        default -> out.append("?");
                    }
                    break;
                }
                case "W": {
                    out.append(answerW(part));
                    break;
                }
                case "C": {
                    ClassifyResult result = InputClassifier.INSTANCE.classify(dec(part[1]));
                    if (result instanceof ClassifyResult.Ok ok) {
                        ParsedInput input = ok.getInput();
                        if (input instanceof ParsedInput.Playlist p) out.append("P ").append(enc(p.getId())).append(' ').append(enc(p.getAlsoVideoId()));
                        else out.append("V ").append(enc(((ParsedInput.Video) input).getId()));
                    } else {
                        out.append("REJECT ").append(((ClassifyResult.Rejected) result).getReason().name());
                    }
                    break;
                }
                default:
                    out.append("?");
            }
            out.append('\n');
        }
        if (videoInfoFile != null) java.nio.file.Files.deleteIfExists(videoInfoFile);
        print(out);
    }

    // Kind W, one op per case. A path is Path.of(text) on this JVM (Windows); InvalidPathException (what Path.of documents it
    // throws for a string that cannot be a path) answers ERR.
    //   pn text       Path.of(text).toString()                        -> hex | ERR
    //   pc dir name   Path.of(dir).resolve(name).toString()           -> hex | ERR
    //   pf path       Path.of(path).getFileName(), "-" for null       -> hex | - | ERR
    //   px path       kotlin.io.path nameWithoutExtension of Path.of(path)   -> hex | ERR
    static String answerW(String[] part) {
        try {
            switch (part[1]) {
                case "pn":
                    return enc(java.nio.file.Path.of(dec(part[2])).toString());
                case "pc":
                    return enc(java.nio.file.Path.of(dec(part[2])).resolve(dec(part[3])).toString());
                case "pf": {
                    java.nio.file.Path name = java.nio.file.Path.of(dec(part[2])).getFileName();
                    return name == null ? "-" : enc(name.toString());
                }
                case "px":
                    return enc(kotlin.io.path.PathsKt.getNameWithoutExtension(java.nio.file.Path.of(dec(part[2]))));
                case "ln":
                    return answerLines(decBytes(part[2]));
                default:
                    return "?";
            }
        } catch (java.nio.file.InvalidPathException e) {
            return "ERR";
        }
    }

    // W ln: the lines SystemProcessRunner hands to its handlers for these bytes of a child's output:
    // `inputStream.bufferedReader(Charsets.UTF_8).forEachLine(...)`. bufferedReader(charset) is inline-only in Kotlin; it is
    // an InputStreamReader of the charset in a BufferedReader of Kotlin's DEFAULT_BUFFER_SIZE (8 * 1024). forEachLine is the
    // ordinary function of the kotlin.io.TextStreamsKt facade. The chunk sizes of the case are the C++ side's only.
    static String answerLines(byte[] bytes) {
        java.io.BufferedReader reader = new java.io.BufferedReader(
            new InputStreamReader(new java.io.ByteArrayInputStream(bytes), StandardCharsets.UTF_8), 8 * 1024);
        StringBuilder out = new StringBuilder("OK");
        kotlin.io.TextStreamsKt.forEachLine(reader, line -> {
            out.append(' ').append(enc(line));
            return kotlin.Unit.INSTANCE;
        });
        return out.toString();
    }

    // argv <helper>: an observation, not a gate (Q2b Task 2 Step 4). For each argument list on stdin (one per line, every
    // argument as hex, tab-separated: `xgs-diff arglists`), ProcessBuilder starts `<helper> argv <arguments...>`
    // (xgs-test-child, which writes back each argument its C runtime received, as hex). Prints the number of lists that
    // arrived different and the first ten of them (sent, received).
    static void argv(String helper, StringBuilder out) throws Exception {
        BufferedReader in = new BufferedReader(new InputStreamReader(System.in, StandardCharsets.US_ASCII));
        int lists = 0, differ = 0, failed = 0;
        StringBuilder examples = new StringBuilder();
        for (String line = in.readLine(); line != null; line = in.readLine()) {
            if (line.isEmpty()) continue;
            lists++;
            String[] fields = line.split("\t", -1);
            java.util.List<String> command = new java.util.ArrayList<>();
            command.add(helper);
            command.add("argv");
            for (String field : fields) command.add(dec(field));
            String received;
            try {
                Process process = new ProcessBuilder(command).redirectError(ProcessBuilder.Redirect.DISCARD).start();
                process.getOutputStream().close();
                byte[] bytes = process.getInputStream().readAllBytes();
                process.waitFor();
                received = new String(bytes, StandardCharsets.US_ASCII).replace("\r", "").replace("\n", "\t");
                if (received.endsWith("\t")) received = received.substring(0, received.length() - 1);
            } catch (java.io.IOException e) {
                failed++;
                received = "START FAILED: " + e.getMessage();
            }
            if (!received.equals(String.join("\t", fields))) {
                differ++;
                if (differ <= 10) examples.append("  sent:     ").append(line.replace('\t', ' ')).append('\n')
                    .append("  received: ").append(received.replace('\t', ' ')).append('\n');
            }
        }
        out.append(lists).append(" lists, ").append(differ).append(" arrived different (").append(failed).append(" did not start)\n");
        out.append(examples);
    }

    static void print(StringBuilder out) {
        PrintStream stdout = new PrintStream(System.out, false, StandardCharsets.US_ASCII);
        stdout.print(out);
        stdout.flush();
    }
}
