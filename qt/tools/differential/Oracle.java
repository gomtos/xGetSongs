import com.xgetsongs.engine.lyrics.LyricsCandidate;
import com.xgetsongs.engine.lyrics.LyricsMatcher;
import com.xgetsongs.engine.lyrics.LyricsQuery;
import com.xgetsongs.engine.output.LocalFolderSink;
import com.xgetsongs.engine.tags.Ffmetadata;
import com.xgetsongs.engine.tags.TrackTags;
import com.xgetsongs.engine.ResolveException;
import com.xgetsongs.engine.ToolException;
import com.xgetsongs.engine.tools.DefaultToolManager;
import com.xgetsongs.engine.tools.ToolPaths;
import com.xgetsongs.engine.ytdlp.ErrorClassifier;
import com.xgetsongs.engine.ytdlp.FfmpegCommands;
import com.xgetsongs.engine.ytdlp.YtDlpCommands;
import com.xgetsongs.engine.ytdlp.Failure;
import com.xgetsongs.engine.ytdlp.ProgressParser;
import com.xgetsongs.engine.ytdlp.ProgressUpdate;
import com.xgetsongs.engine.ytdlp.VideoInfo;
import com.xgetsongs.engine.ytdlp.VideoInfoFile;
import com.xgetsongs.engine.ytdlp.VideoMeta;
import com.xgetsongs.engine.ytdlp.YtDlpResolver;
import com.xgetsongs.shared.api.ApiJson;
import com.xgetsongs.shared.api.ResolveResponse;
import com.xgetsongs.shared.api.ResolvedItem;
import com.xgetsongs.shared.api.ActionResult;
import com.xgetsongs.shared.api.ToolInfo;
import com.xgetsongs.shared.api.ToolsStatus;
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
 *                                            px path, ln bytes chunkSize... (the lines of a child's output, answerLines),
 *                                            st and up (DefaultToolManager.status and updateYtDlp on a runner that plays
 *                                            the case: answerTools), fn name (does LocalFolderSink accept the file
 *                                            name: 0 | 1, answerFileName), lf query maxRequests timeout step...
 *                                            (LrclibLyricsProvider on an HttpClient that answers from the case and sends
 *                                            nothing: answerLookUp), jb request items scripts... (the whole
 *                                            download job on scripted collaborators: answerJob)
 *         P op fields...                     YtDlpResolver on a runner that plays the case, one op per case (answerP): rs input
 *                                            run stdout stderr..., fx videoId run stdout stderr...
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
                case "P": {
                    out.append(answerP(part));
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
    //   fn name       LocalFolderSink(a JVM temp folder).exists(name) throws: 0, else 1 (answerFileName)
    //   lf query maxRequests timeout step...   LrclibLyricsProvider on a scripted HttpClient (answerLookUp)
    static String answerW(String[] part) throws Exception {
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
                case "st":
                case "up":
                    return answerTools(part);
                case "fn":
                    return answerFileName(dec(part[2]));
                case "lf":
                    return answerLookUp(part);
                case "jb":
                    return answerJob(part);
                default:
                    return "?";
            }
        } catch (java.nio.file.InvalidPathException e) {
            return "ERR";
        }
    }

    // W fn: does LocalFolderSink accept this file name? Its private resolve() is reached through exists(), which calls it
    // before any file access (`Files.exists(resolve(fileName))`); resolve's require throws IllegalArgumentException (or
    // InvalidPathException, a subclass, from Path.of) for a rejected name, and Path.of(name).fileName.toString() throws
    // NullPointerException for a name that is a root only (`C:`, `\`; observed). Either answers 0. Files.exists only reads
    // attributes. A name is one path part at most, but Windows resolves some accepted names (`...`, `.. `) to the folder or
    // to its parent (observed: exists is true), so the sink folder is two levels inside the temp folder: nothing outside
    // that folder is ever looked at. The folder is created at the first fn case and removed when the JVM exits.
    static LocalFolderSink fileNameSink;

    static String answerFileName(String name) throws Exception {
        if (fileNameSink == null) {
            java.nio.file.Path root = java.nio.file.Files.createTempDirectory("xgs-diff-sink-");
            Runtime.getRuntime().addShutdownHook(new Thread(() -> deleteTree(root)));
            fileNameSink = new LocalFolderSink(root.resolve("x").resolve("sink"));
        }
        try {
            kotlinx.coroutines.BuildersKt.<Boolean>runBlocking(kotlin.coroutines.EmptyCoroutineContext.INSTANCE,
                (scope, continuation) -> fileNameSink.exists(name, continuation));
            return "1";
        } catch (IllegalArgumentException | NullPointerException e) {
            return "0";
        }
    }

    static void deleteTree(java.nio.file.Path root) {
        try (java.util.stream.Stream<java.nio.file.Path> walk = java.nio.file.Files.walk(root)) {
            for (java.nio.file.Path p : walk.sorted(java.util.Comparator.reverseOrder()).toList()) java.nio.file.Files.deleteIfExists(p);
        } catch (java.io.IOException e) {
            System.err.println("could not remove " + root + ": " + e);
        }
    }

    // W ln: the lines SystemProcessRunner hands to its handlers for these bytes of a child's output (readLines, which kind P's
    // runner uses too):
    // `inputStream.bufferedReader(Charsets.UTF_8).forEachLine(...)`. bufferedReader(charset) is inline-only in Kotlin; it is
    // an InputStreamReader of the charset in a BufferedReader of Kotlin's DEFAULT_BUFFER_SIZE (8 * 1024). forEachLine is the
    // ordinary function of the kotlin.io.TextStreamsKt facade. The chunk sizes of the case are the C++ side's only.
    static String answerLines(byte[] bytes) {
        StringBuilder out = new StringBuilder("OK");
        for (String line : readLines(bytes)) out.append(' ').append(enc(line));
        return out.toString();
    }

    static java.util.List<String> readLines(byte[] bytes) {
        java.io.BufferedReader reader = new java.io.BufferedReader(
            new InputStreamReader(new java.io.ByteArrayInputStream(bytes), StandardCharsets.UTF_8), 8 * 1024);
        java.util.List<String> lines = new java.util.ArrayList<>();
        kotlin.io.TextStreamsKt.forEachLine(reader, line -> {
            lines.add(line);
            return kotlin.Unit.INSTANCE;
        });
        return lines;
    }

    // W st / W up: DefaultToolManager on a ProcessRunner that plays the case. A tool is three fields: its path (hex, "-" when
    // missing), its run (the exit code in decimal, or S: the runner throws IOException(START_FAILURE) as ProcessBuilder.start
    // does for a program that cannot be started) and its output lines ("~" for none, else comma-separated items: o or e for
    // stdout or stderr, then the line as hex). The output goes to the handlers in that order on the caller's thread.
    //   st yt-dlp ffmpeg js (3 fields each)   status()  -> found version path for each tool (found 0 | 1, the strings as hex)
    //   up yt-dlp (3 fields)                  updateYtDlp()  -> OK hex | ERR hex (the ToolException's message)
    // Both answers end in " R" and every command the runner was given (its arguments as hex, comma-separated).
    static final String START_FAILURE = "Cannot run program (start failure of the differential test)";

    // What the runner does for one program: its run (the exit code in decimal, or S) and its output lines in order (each a
    // stdout flag and the line).
    static final class Play {
        final String run;
        final java.util.List<Object[]> lines = new java.util.ArrayList<>();

        Play(String run) {
            this.run = run;
        }

        // kind W's notation of the lines: "~" for none, else comma-separated o<hex> / e<hex>
        static Play of(String run, String items) {
            Play play = new Play(run);
            if (!items.equals("~")) {
                for (String item : items.split(",", -1)) play.lines.add(new Object[] {item.charAt(0) == 'o', dec(item.substring(1))});
            }
            return play;
        }
    }

    // A ProcessRunner that plays the case (kinds W and P): the program at command[0] runs as its Play says (a program without
    // one exits with 1), its lines going to the handlers in order on the caller's thread. Records every command.
    static final class CaseRunner implements com.xgetsongs.engine.process.ProcessRunner {
        final java.util.Map<String, Play> runs = new java.util.HashMap<>();   // path -> what it does
        final StringBuilder commands = new StringBuilder(" R");

        @Override
        public Object run(java.util.List<String> command, kotlin.jvm.functions.Function1<? super String, kotlin.Unit> onStdout,
                          kotlin.jvm.functions.Function1<? super String, kotlin.Unit> onStderr,
                          kotlin.coroutines.Continuation<? super Integer> continuation) {
            commands.append(' ');
            for (int k = 0; k < command.size(); k++) commands.append(k > 0 ? "," : "").append(enc(command.get(k)));
            Play play = runs.get(command.get(0));
            if (play == null) return Integer.valueOf(1);
            if (play.run.equals("S")) Oracle.<RuntimeException>sneakyThrow(new java.io.IOException(START_FAILURE));
            for (Object[] line : play.lines) {
                if ((Boolean) line[0]) onStdout.invoke((String) line[1]);
                else onStderr.invoke((String) line[1]);
            }
            return Integer.valueOf(Integer.parseInt(play.run));
        }
    }

    @SuppressWarnings("unchecked")
    static <E extends Throwable> void sneakyThrow(Throwable e) throws E {
        throw (E) e;
    }

    static String toolInfo(ToolInfo info) {
        return bit(info.getFound()) + " " + enc(info.getVersion()) + " " + enc(info.getPath());
    }

    static String answerTools(String[] part) throws Exception {
        CaseRunner runner = new CaseRunner();
        int tools = part[1].equals("st") ? 3 : 1;
        java.nio.file.Path[] paths = new java.nio.file.Path[3];
        for (int t = 0; t < tools; t++) {
            try {
                paths[t] = path(part[2 + 3 * t]);
            } catch (BadPath e) {
                return "BADPATH";
            }
            if (paths[t] != null) runner.runs.put(paths[t].toString(), Play.of(part[3 + 3 * t], part[4 + 3 * t]));
        }
        ToolPaths toolPaths = new ToolPaths(paths[0], paths[1], paths[2]);
        DefaultToolManager manager = new DefaultToolManager(() -> toolPaths, runner, java.nio.file.Path.of("C:\\xgs-diff-unused"),
            (url, continuation) -> { throw new IllegalStateException("fetch is not used"); });
        String answer;
        if (part[1].equals("st")) {
            ToolsStatus status = kotlinx.coroutines.BuildersKt.<ToolsStatus>runBlocking(kotlin.coroutines.EmptyCoroutineContext.INSTANCE,
                (scope, continuation) -> manager.status(continuation));
            answer = toolInfo(status.getYtDlp()) + " " + toolInfo(status.getFfmpeg()) + " " + toolInfo(status.getJsRuntime());
        } else {
            try {
                ActionResult result = kotlinx.coroutines.BuildersKt.<ActionResult>runBlocking(kotlin.coroutines.EmptyCoroutineContext.INSTANCE,
                    (scope, continuation) -> manager.updateYtDlp(continuation));
                answer = "OK " + enc(result.getMessage());
            } catch (Exception e) {
                if (!(e instanceof ToolException)) throw e;
                answer = "ERR " + enc(e.getMessage());
            }
        }
        return answer + runner.commands;
    }

    // Kind P: YtDlpResolver(runner, tools, ApiJson.instance) on a CaseRunner. The fields after the op: the input (rs) or the
    // video id (fx) as hex; the run: the exit code in decimal, S (the runner throws IOException(START_FAILURE)) or - (no yt-dlp:
    // ToolPaths.ytDlp is null); the stdout bytes (2 hex digits each, "." for none), split into lines by readLines as
    // SystemProcessRunner does; the stderr lines, one per field (an empty field is no line). Stdout goes to the handler before
    // stderr. The tools are C:\tools\yt-dlp.exe, C:\tools\ffmpeg.exe and C:\tools\node.exe.
    //   rs  resolve(input)  -> OK resolveId kind playlistTitle truncated alsoVideoId count, then per item: rank videoId title
    //                          channel artist track lowConfidence available unavailableReason expectedFileName | ERR hex (the
    //                          ResolveException's message)
    //   fx  fetch(videoId)  -> M title channel artist track | NULL
    // Anything else thrown answers THROW and its class. Every answer ends in " R" and every command the runner was given.
    static final String P_YT_DLP = "C:\\tools\\yt-dlp.exe";

    static String answerP(String[] part) throws Exception {
        CaseRunner runner = new CaseRunner();
        String run = part[3];
        java.nio.file.Path ytDlp = run.equals("-") ? null : java.nio.file.Path.of(P_YT_DLP);
        ToolPaths toolPaths = new ToolPaths(ytDlp, java.nio.file.Path.of("C:\\tools\\ffmpeg.exe"), java.nio.file.Path.of("C:\\tools\\node.exe"));
        if (ytDlp != null) {
            Play play = new Play(run);
            for (String line : readLines(decBytes(part[4]))) play.lines.add(new Object[] {true, line});
            for (int k = 5; k < part.length; k++) if (!part[k].isEmpty()) play.lines.add(new Object[] {false, dec(part[k])});
            runner.runs.put(P_YT_DLP, play);
        }
        YtDlpResolver resolver = new YtDlpResolver(runner, () -> toolPaths, ApiJson.INSTANCE.getInstance());
        String argument = dec(part[2]);
        String answer;
        try {
            if (part[1].equals("rs")) {
                ResolveResponse response = kotlinx.coroutines.BuildersKt.<ResolveResponse>runBlocking(kotlin.coroutines.EmptyCoroutineContext.INSTANCE,
                    (scope, continuation) -> resolver.resolve(argument, continuation));
                StringBuilder out = new StringBuilder("OK ");
                out.append(enc(response.getResolveId())).append(' ').append(response.getKind().name()).append(' ')
                    .append(enc(response.getPlaylistTitle())).append(' ').append(bit(response.getTruncated())).append(' ')
                    .append(enc(response.getAlsoVideoId())).append(' ').append(response.getItems().size());
                for (ResolvedItem item : response.getItems()) {
                    out.append(' ').append(item.getRank()).append(' ').append(enc(item.getVideoId())).append(' ').append(enc(item.getTitle()))
                        .append(' ').append(enc(item.getChannel())).append(' ').append(enc(item.getArtist())).append(' ')
                        .append(enc(item.getTrack())).append(' ').append(bit(item.getLowConfidence())).append(' ')
                        .append(bit(item.getAvailable())).append(' ').append(enc(item.getUnavailableReason())).append(' ')
                        .append(enc(item.getExpectedFileName()));
                }
                answer = out.toString();
            } else if (part[1].equals("fx")) {
                VideoMeta meta = kotlinx.coroutines.BuildersKt.<VideoMeta>runBlocking(kotlin.coroutines.EmptyCoroutineContext.INSTANCE,
                    (scope, continuation) -> resolver.fetch(argument, continuation));
                answer = meta == null ? "NULL" : "M " + enc(meta.getTitle()) + " " + enc(meta.getChannel()) + " " + enc(meta.getArtist())
                    + " " + enc(meta.getTrack());
            } else {
                return "?";
            }
        } catch (Throwable e) {
            if (e instanceof ResolveException) answer = "ERR " + enc(e.getMessage());
            else answer = "THROW " + e.getClass().getName();
        }
        return answer + runner.commands;
    }

    // W lf artist title album durationSeconds maxRequests timeout step...: LrclibLyricsProvider(client = a ScriptClient,
    // baseUrl = "http://h/api", requestTimeout, maxRequests, timeSource = a TestTimeSource that is never moved) and find(query)
    // twice. The query is 4 fields as kind I's; maxRequests in decimal; timeout D (15 s), 0 (zero) or N (minus one second).
    // A step is what the client does for one request, in order across both calls (once the steps run out, 404 with no body):
    //   <code>:<bytes>   an answer with that status and body (2 hex digits a byte, "." for none)
    //   <code>:L         an answer with that status whose body is 2 MiB + 1 bytes (a stream that makes them as it is read)
    //   C | T | U | I    send throws ConnectException | HttpTimeoutException | UnknownHostException | IOException
    // -> OK <lyrics hex> | NULL (the first call), then N and the number of requests the second call made, then C and the number
    // of requests one call makes on a fresh client and provider with maxRequests + 1 (the same steps from the start), then U
    // and the URL of every request of the first call as hex (the URL the provider handed to the client: request.uri().toString()).
    // Nothing is sent anywhere: the client answers from the case.
    static final String LF_BASE_URL = "http://h/api";

    static final class ScriptClient extends java.net.http.HttpClient {
        final String[] steps;
        int next;
        final java.util.List<String> urls = java.util.Collections.synchronizedList(new java.util.ArrayList<>());

        ScriptClient(String[] steps) {
            this.steps = steps;
        }

        @Override
        public java.util.Optional<java.net.CookieHandler> cookieHandler() { return java.util.Optional.empty(); }
        @Override
        public java.util.Optional<java.time.Duration> connectTimeout() { return java.util.Optional.empty(); }
        @Override
        public Redirect followRedirects() { return Redirect.NEVER; }
        @Override
        public java.util.Optional<java.net.ProxySelector> proxy() { return java.util.Optional.empty(); }
        @Override
        public javax.net.ssl.SSLContext sslContext() { throw new UnsupportedOperationException(); }
        @Override
        public javax.net.ssl.SSLParameters sslParameters() { throw new UnsupportedOperationException(); }
        @Override
        public java.util.Optional<java.net.Authenticator> authenticator() { return java.util.Optional.empty(); }
        @Override
        public Version version() { return Version.HTTP_1_1; }
        @Override
        public java.util.Optional<java.util.concurrent.Executor> executor() { return java.util.Optional.empty(); }
        @Override
        public <T> java.util.concurrent.CompletableFuture<java.net.http.HttpResponse<T>> sendAsync(java.net.http.HttpRequest request,
                java.net.http.HttpResponse.BodyHandler<T> handler) { throw new UnsupportedOperationException(); }
        @Override
        public <T> java.util.concurrent.CompletableFuture<java.net.http.HttpResponse<T>> sendAsync(java.net.http.HttpRequest request,
                java.net.http.HttpResponse.BodyHandler<T> handler, java.net.http.HttpResponse.PushPromiseHandler<T> push) {
            throw new UnsupportedOperationException();
        }

        // The provider asks for BodyHandlers.ofInputStream(); the answer's body is an InputStream whatever the handler.
        @Override
        @SuppressWarnings("unchecked")
        public <T> java.net.http.HttpResponse<T> send(java.net.http.HttpRequest request, java.net.http.HttpResponse.BodyHandler<T> handler)
                throws java.io.IOException {
            urls.add(request.uri().toString());
            String step;
            synchronized (this) {
                step = next < steps.length ? steps[next++] : "404:.";
            }
            switch (step) {
                case "C" -> throw new java.net.ConnectException("connection refused (differential test)");
                case "T" -> throw new java.net.http.HttpTimeoutException("request timed out (differential test)");
                case "U" -> throw new java.net.UnknownHostException("h");
                case "I" -> throw new java.io.IOException("connection reset (differential test)");
                default -> { }
            }
            int colon = step.indexOf(':');
            int code = Integer.parseInt(step.substring(0, colon));
            String body = step.substring(colon + 1);
            java.io.InputStream stream = body.equals("L") ? new LongBody() : new java.io.ByteArrayInputStream(decBytes(body));
            return (java.net.http.HttpResponse<T>) new ScriptResponse(request, code, stream);
        }
    }

    // A body of 2 MiB + 1 bytes, made as it is read (never held in full by the stream).
    static final class LongBody extends java.io.InputStream {
        long left = 2L * 1024 * 1024 + 1;

        @Override
        public int read() {
            if (left == 0) return -1;
            left--;
            return 'x';
        }

        @Override
        public int read(byte[] buffer, int offset, int length) {
            if (length == 0) return 0;
            if (left == 0) return -1;
            int count = (int) Math.min(length, left);
            java.util.Arrays.fill(buffer, offset, offset + count, (byte) 'x');
            left -= count;
            return count;
        }
    }

    record ScriptResponse(java.net.http.HttpRequest request, int statusCode, java.io.InputStream body)
            implements java.net.http.HttpResponse<java.io.InputStream> {
        @Override
        public java.util.Optional<java.net.http.HttpResponse<java.io.InputStream>> previousResponse() { return java.util.Optional.empty(); }
        @Override
        public java.net.http.HttpHeaders headers() { return java.net.http.HttpHeaders.of(java.util.Map.of(), (name, value) -> true); }
        @Override
        public java.util.Optional<javax.net.ssl.SSLSession> sslSession() { return java.util.Optional.empty(); }
        @Override
        public java.net.URI uri() { return request.uri(); }
        @Override
        public java.net.http.HttpClient.Version version() { return java.net.http.HttpClient.Version.HTTP_1_1; }
    }

    // LrclibLyricsProvider's constructor takes kotlin.time.Duration values, an inline class: its JVM constructor is the
    // synthetic one with a DefaultConstructorMarker, and a Duration is passed as the long that Duration.parse returns (the
    // public JVM form of both). The one with the default mask leaves userAgent, connectTimeout, maxConcurrent and coolDown
    // (5 minutes) at their defaults.
    static java.lang.reflect.Constructor<?> lrclibConstructor;
    static java.lang.reflect.Method durationParse;

    static long duration(String text) throws Exception {
        if (durationParse == null) durationParse = kotlin.time.Duration.Companion.class.getMethod("parse-UwyO8pc", String.class);
        return (Long) durationParse.invoke(kotlin.time.Duration.Companion, text);
    }

    static com.xgetsongs.engine.lyrics.LrclibLyricsProvider lrclibProvider(ScriptClient client, long requestTimeout, int maxRequests)
            throws Exception {
        if (lrclibConstructor == null) {
            lrclibConstructor = com.xgetsongs.engine.lyrics.LrclibLyricsProvider.class.getConstructor(java.net.http.HttpClient.class,
                String.class, String.class, long.class, long.class, int.class, int.class, long.class, kotlin.time.TimeSource.class,
                int.class, kotlin.jvm.internal.DefaultConstructorMarker.class);
        }
        // the mask: bit i stands for the i-th parameter taking its default (2 userAgent, 3 connectTimeout, 6 maxConcurrent, 7 coolDown)
        int mask = (1 << 2) | (1 << 3) | (1 << 6) | (1 << 7);
        return (com.xgetsongs.engine.lyrics.LrclibLyricsProvider) lrclibConstructor.newInstance(client, LF_BASE_URL, null, 0L,
            requestTimeout, maxRequests, 0, 0L, new kotlin.time.TestTimeSource(), mask, null);
    }

    static String answerLookUp(String[] part) throws Exception {
        LyricsQuery query = lyricsQuery(part, 2);
        int maxRequests = Integer.parseInt(part[6]);
        long requestTimeout = duration(switch (part[7]) {
            case "D" -> "15s";
            case "0" -> "0s";
            case "N" -> "-1s";
            default -> throw new IllegalArgumentException("timeout " + part[7]);
        });
        ScriptClient client = new ScriptClient(java.util.Arrays.copyOfRange(part, 8, part.length));
        com.xgetsongs.engine.lyrics.LrclibLyricsProvider provider = lrclibProvider(client, requestTimeout, maxRequests);
        String lyrics = kotlinx.coroutines.BuildersKt.<String>runBlocking(kotlin.coroutines.EmptyCoroutineContext.INSTANCE,
            (scope, continuation) -> provider.find(query, continuation));
        java.util.List<String> firstUrls;
        synchronized (client.urls) {
            firstUrls = new java.util.ArrayList<>(client.urls);
        }
        kotlinx.coroutines.BuildersKt.<String>runBlocking(kotlin.coroutines.EmptyCoroutineContext.INSTANCE,
            (scope, continuation) -> provider.find(query, continuation));
        int second = client.urls.size() - firstUrls.size();
        // the same lookup on a fresh client and provider whose cap is one higher: more requests than maxRequests means the cap
        // cut the first call short
        ScriptClient wider = new ScriptClient(java.util.Arrays.copyOfRange(part, 8, part.length));
        // (the generator never writes Integer.MAX_VALUE; clamped there, as main.cpp does, so a hand-written case cannot overflow)
        int widerMaxRequests = maxRequests == Integer.MAX_VALUE ? maxRequests : maxRequests + 1;
        com.xgetsongs.engine.lyrics.LrclibLyricsProvider widerProvider = lrclibProvider(wider, requestTimeout, widerMaxRequests);
        kotlinx.coroutines.BuildersKt.<String>runBlocking(kotlin.coroutines.EmptyCoroutineContext.INSTANCE,
            (scope, continuation) -> widerProvider.find(query, continuation));
        StringBuilder out = new StringBuilder(lyrics == null ? "NULL" : "OK " + enc(lyrics));
        out.append(" N ").append(second).append(" C ").append(wider.urls.size()).append(" U");
        for (String url : firstUrls) out.append(' ').append(enc(url));
        return out.toString();
    }

    // W jb overwrite includeRank album albumOverride searchLyrics existing n item... meta ytdlp ffmpeg lyrics put: the real
    // DefaultDownloadService(ItemDownloader(a JobRunner, tools C:\tools\yt-dlp.exe ffmpeg.exe node.exe, a scripted
    // VideoMetadataSource, a scripted LyricsProvider), a fresh temporary folder, the scope of runBlocking, retryDelays = two
    // zero Durations) runs one DownloadRequest (concurrency 1) on a scripted OutputSink, and the events are read to the end
    // (ChannelsKt.toList of the job's channel). The notation (gen.h has it too):
    //   overwrite includeRank searchLyrics 0 | 1; album albumOverride hex | . | -; existing: the names the sink already has,
    //   "~" or comma-separated hex; n, then per item: rank videoId title channel artist track lowConfidence.
    //   Five scripts, comma-separated entries, each consumed in call order (the last entry again once they run out):
    //   meta    per fetch: - (null) or title/channel/artist/track (each hex | . | -)
    //   ytdlp   per yt-dlp run: exit/m4a/cover/info/stdout/stderr: the exit code; m4a and cover 0 | 1 (the run writes
    //           <id>.m4a "m4a-data", <id>.jpg "jpg-data" into the folder of its -o template); info - (none written) | . | the
    //           bytes of <id>.info.json as hex; the stdout and stderr lines, "~" for none, else "+"-separated hex (. an empty
    //           line). The files are written first, then the stdout lines, then the stderr lines, on the caller's thread.
    //   ffmpeg  per tag run: exit/writes/stderr (writes 1: the output, the last argument, gets "tagged-m4a-data"); the text
    //           of the ffmetadata input is read before anything else
    //   lyrics  per find: - (null) | . | hex | X (find throws IllegalStateException)
    //   put     per put: S (stored: the name joins the sink's names) | A<hex or .> (FileAlreadyExistsException(fileName); the
    //           text is the C++ result's only) | a<hex or .> (the same after the name joins the sink's names: another
    //           program put it there first) | F<code>/<hex> (code 5: AccessDeniedException(fileName), else
    //           IOException(text); the generator writes the codes 112 and 39 only with a disk-full marker in the text)
    //   exists(name) is whether the sink's names hold it.
    // -> E <event>... T <videoId>... M <ffmetadata text hex>... Q <query>... Y <rank>... G <rank>... P <rank/fileName>... R <count>:
    //   every event in order: S,rank,videoId,fileName | P,rank,D|F,percent (- for null, N when not finite, else the 16 hex
    //   digits of doubleToRawLongBits) | D,rank,fileName,lyrics (the LyricsOutcome's name or -) | K,rank,reason |
    //   F,rank,message | J,status,succeeded,skipped,failed; the video ID of each metadata fetch (hex); the ffmetadata text
    //   of each tag run; each lyrics query
    //   (artist/title/album/durationSeconds, hex or "-", the duration in decimal); the rank folder of each yt-dlp run and of
    //   each tag run; the rank folder of each put's source and the file name it was given; the entries left in the
    //   temporary folder after the job. Anything the job throws out of runBlocking answers THROW <class> <message hex> for
    //   that case (a loud mismatch that names the case, instead of ending the oracle).
    static final String JOB_YT_DLP = "C:\\tools\\yt-dlp.exe";
    static final String JOB_FFMPEG = "C:\\tools\\ffmpeg.exe";
    static java.lang.reflect.Method durationBox;

    // a Duration of zero, boxed: the element type of DefaultDownloadService's List<Duration> (an inline class in a generic
    // position is passed boxed; box-impl is its public JVM form)
    static Object zeroDuration() throws Exception {
        if (durationBox == null) durationBox = kotlin.time.Duration.class.getMethod("box-impl", long.class);
        return durationBox.invoke(null, 0L);
    }

    // A script of jb: its entries in call order, then its last entry again.
    static final class Script {
        final String[] entries;
        int next;

        Script(String field) {
            entries = field.split(",", -1);
        }

        synchronized String take() {
            String entry = entries[Math.min(next, entries.length - 1)];
            next++;
            return entry;
        }
    }

    static java.util.List<String> jobLines(String field) {
        java.util.List<String> lines = new java.util.ArrayList<>();
        if (!field.equals("~")) for (String line : field.split("\\+", -1)) lines.add(dec(line));
        return lines;
    }

    // the rank folder of a file in the job folder (<job>\<rank>\<file>)
    static String rankOf(java.nio.file.Path file) {
        return file.getParent().getFileName().toString();
    }

    // The ProcessRunner of jb: an ffmpeg command (command[0] is JOB_FFMPEG) plays the ffmpeg script, any other the yt-dlp
    // script. A failure of the fake's own file work is an AssertionError, which Kotlin does not catch: the oracle fails loudly.
    static final class JobRunner implements com.xgetsongs.engine.process.ProcessRunner {
        final Script ytDlp;
        final Script ffmpeg;
        final java.util.List<String> ytDlpRanks = java.util.Collections.synchronizedList(new java.util.ArrayList<>());
        final java.util.List<String> ffmpegRanks = java.util.Collections.synchronizedList(new java.util.ArrayList<>());
        final java.util.List<String> ffmetadata = java.util.Collections.synchronizedList(new java.util.ArrayList<>());

        JobRunner(String ytDlp, String ffmpeg) {
            this.ytDlp = new Script(ytDlp);
            this.ffmpeg = new Script(ffmpeg);
        }

        @Override
        public Object run(java.util.List<String> command, kotlin.jvm.functions.Function1<? super String, kotlin.Unit> onStdout,
                          kotlin.jvm.functions.Function1<? super String, kotlin.Unit> onStderr,
                          kotlin.coroutines.Continuation<? super Integer> continuation) {
            try {
                if (command.get(0).equals(JOB_FFMPEG)) {
                    java.nio.file.Path output = java.nio.file.Path.of(command.get(command.size() - 1));
                    ffmpegRanks.add(rankOf(output));
                    ffmetadata.add(enc(java.nio.file.Files.readString(java.nio.file.Path.of(command.get(command.indexOf("ffmetadata") + 2)))));
                    String[] entry = ffmpeg.take().split("/", -1);
                    if (entry[1].equals("1")) java.nio.file.Files.write(output, "tagged-m4a-data".getBytes(StandardCharsets.ISO_8859_1));
                    for (String line : jobLines(entry[2])) onStderr.invoke(line);
                    return Integer.valueOf(Integer.parseInt(entry[0]));
                }
                java.nio.file.Path template = java.nio.file.Path.of(command.get(command.indexOf("-o") + 1));
                java.nio.file.Path dir = template.getParent();
                String name = template.getFileName().toString();
                String id = name.endsWith(".%(ext)s") ? name.substring(0, name.length() - ".%(ext)s".length()) : name;
                ytDlpRanks.add(dir.getFileName().toString());
                String[] entry = ytDlp.take().split("/", -1);
                if (entry[1].equals("1")) java.nio.file.Files.writeString(dir.resolve(id + ".m4a"), "m4a-data");
                if (entry[2].equals("1")) java.nio.file.Files.writeString(dir.resolve(id + ".jpg"), "jpg-data");
                if (!entry[3].equals("-")) java.nio.file.Files.write(dir.resolve(id + ".info.json"), decBytes(entry[3]));
                for (String line : jobLines(entry[4])) onStdout.invoke(line);
                for (String line : jobLines(entry[5])) onStderr.invoke(line);
                return Integer.valueOf(Integer.parseInt(entry[0]));
            } catch (java.io.IOException e) {
                throw new AssertionError("the fake runner of jb could not do its file work", e);
            }
        }
    }

    // The OutputSink of jb (see the put script above).
    static final class JobSink implements com.xgetsongs.engine.output.OutputSink {
        final java.util.Set<String> names = new java.util.HashSet<>();
        final Script script;
        final java.util.List<String> puts = java.util.Collections.synchronizedList(new java.util.ArrayList<>());

        JobSink(String existing, String put) {
            if (!existing.equals("~")) for (String name : existing.split(",", -1)) names.add(dec(name));
            script = new Script(put);
        }

        @Override
        public Object exists(String fileName, kotlin.coroutines.Continuation<? super Boolean> continuation) {
            synchronized (names) {
                return Boolean.valueOf(names.contains(fileName));
            }
        }

        @Override
        public Object put(String fileName, java.nio.file.Path source, boolean overwrite,
                          kotlin.coroutines.Continuation<? super kotlin.Unit> continuation) {
            puts.add(rankOf(source) + "/" + enc(fileName));
            String entry = script.take();
            switch (entry.charAt(0)) {
                case 'S' -> {
                    synchronized (names) {
                        names.add(fileName);
                    }
                    return kotlin.Unit.INSTANCE;
                }
                case 'A' -> Oracle.<RuntimeException>sneakyThrow(new java.nio.file.FileAlreadyExistsException(fileName));
                case 'a' -> {
                    synchronized (names) {
                        names.add(fileName);
                    }
                    Oracle.<RuntimeException>sneakyThrow(new java.nio.file.FileAlreadyExistsException(fileName));
                }
                case 'F' -> {
                    int slash = entry.indexOf('/');
                    int code = Integer.parseInt(entry.substring(1, slash));
                    if (code == 5) Oracle.<RuntimeException>sneakyThrow(new java.nio.file.AccessDeniedException(fileName));
                    Oracle.<RuntimeException>sneakyThrow(new java.io.IOException(dec(entry.substring(slash + 1))));
                }
                default -> { }
            }
            throw new AssertionError("put entry " + entry);
        }
    }

    static String percentOf(Double percent) {
        if (percent == null) return "-";
        if (!Double.isFinite(percent)) return "N";
        return String.format(Locale.ROOT, "%016x", Double.doubleToRawLongBits(percent));
    }

    static String eventOf(Object event) {
        if (event instanceof com.xgetsongs.shared.api.JobEvent.ItemStarted e) {
            return "S," + e.getRank() + "," + enc(e.getVideoId()) + "," + enc(e.getFileName());
        }
        if (event instanceof com.xgetsongs.shared.api.JobEvent.Progress e) {
            return "P," + e.getRank() + "," + (e.getStage() == com.xgetsongs.shared.api.Stage.DOWNLOADING ? "D" : "F") + ","
                + percentOf(e.getPercent());
        }
        if (event instanceof com.xgetsongs.shared.api.JobEvent.ItemDone e) {
            return "D," + e.getRank() + "," + enc(e.getFileName()) + "," + (e.getLyrics() == null ? "-" : e.getLyrics().name());
        }
        if (event instanceof com.xgetsongs.shared.api.JobEvent.ItemSkipped e) return "K," + e.getRank() + "," + enc(e.getReason());
        if (event instanceof com.xgetsongs.shared.api.JobEvent.ItemFailed e) return "F," + e.getRank() + "," + enc(e.getMessage());
        if (event instanceof com.xgetsongs.shared.api.JobEvent.JobDone e) {
            return "J," + e.getStatus().name() + "," + e.getSummary().getSucceeded() + "," + e.getSummary().getSkipped() + ","
                + e.getSummary().getFailed();
        }
        throw new AssertionError("event " + event);
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    static String answerJob(String[] part) throws Exception {
        boolean overwrite = part[2].equals("1");
        boolean includeRank = part[3].equals("1");
        String album = dec(part[4]);
        String albumOverride = dec(part[5]);
        boolean searchLyrics = part[6].equals("1");
        int n = Integer.parseInt(part[8]);
        java.util.List<ResolvedItem> items = new java.util.ArrayList<>();
        for (int i = 0; i < n; i++) {
            int at = 9 + 7 * i;
            items.add(new ResolvedItem(Integer.parseInt(part[at]), dec(part[at + 1]), dec(part[at + 2]), dec(part[at + 3]),
                dec(part[at + 4]), dec(part[at + 5]), part[at + 6].equals("1"), true, null, null));
        }
        int scripts = 9 + 7 * n;
        Script meta = new Script(part[scripts]);
        JobRunner runner = new JobRunner(part[scripts + 1], part[scripts + 2]);
        Script lyricsScript = new Script(part[scripts + 3]);
        JobSink sink = new JobSink(part[7], part[scripts + 4]);
        java.util.List<String> queries = java.util.Collections.synchronizedList(new java.util.ArrayList<>());
        java.util.List<String> fetches = java.util.Collections.synchronizedList(new java.util.ArrayList<>());

        ToolPaths toolPaths = new ToolPaths(java.nio.file.Path.of(JOB_YT_DLP), java.nio.file.Path.of(JOB_FFMPEG),
            java.nio.file.Path.of("C:\\tools\\node.exe"));
        com.xgetsongs.engine.ytdlp.VideoMetadataSource metadata = (videoId, continuation) -> {
            fetches.add(enc(videoId));
            String entry = meta.take();
            if (entry.equals("-")) return null;
            String[] f = entry.split("/", -1);
            return new VideoMeta(dec(f[0]), dec(f[1]), dec(f[2]), dec(f[3]));
        };
        com.xgetsongs.engine.lyrics.LyricsProvider lyrics = new com.xgetsongs.engine.lyrics.LyricsProvider() {
            @Override
            public Object find(LyricsQuery query, kotlin.coroutines.Continuation<? super String> continuation) {
                queries.add(enc(query.getArtist()) + "/" + enc(query.getTitle()) + "/" + enc(query.getAlbum()) + "/"
                    + (query.getDurationSeconds() == null ? "-" : query.getDurationSeconds().toString()));
                String entry = lyricsScript.take();
                if (entry.equals("X")) throw new IllegalStateException("the lookup failed (differential test)");
                return dec(entry);
            }
        };
        com.xgetsongs.engine.job.ItemDownloader downloader = new com.xgetsongs.engine.job.ItemDownloader(runner, () -> toolPaths,
            metadata, lyrics);
        com.xgetsongs.engine.DownloadRequest request = new com.xgetsongs.engine.DownloadRequest(items, sink, overwrite, 1, album,
            includeRank, searchLyrics, albumOverride);
        java.util.List delays = java.util.List.of(zeroDuration(), zeroDuration());

        java.nio.file.Path tempRoot = java.nio.file.Files.createTempDirectory("xgs-diff-jb-");
        try {
            java.util.List<Object> events;
            try {
                events = kotlinx.coroutines.BuildersKt.<java.util.List<Object>>runBlocking(
                    kotlin.coroutines.EmptyCoroutineContext.INSTANCE, (scope, continuation) -> {
                        com.xgetsongs.engine.job.DefaultDownloadService service =
                            new com.xgetsongs.engine.job.DefaultDownloadService(downloader, tempRoot, scope, delays);
                        return kotlinx.coroutines.channels.ChannelsKt.toList(service.start(request).getEvents(),
                            (kotlin.coroutines.Continuation) continuation);
                    });
            } catch (Throwable e) {
                return "THROW " + e.getClass().getName() + " " + enc(String.valueOf(e.getMessage()));
            }
            long remaining;
            try (java.util.stream.Stream<java.nio.file.Path> list = java.nio.file.Files.list(tempRoot)) {
                remaining = list.count();
            }
            StringBuilder out = new StringBuilder("E");
            for (Object event : events) out.append(' ').append(eventOf(event));
            out.append(" T");
            for (String videoId : fetches) out.append(' ').append(videoId);
            out.append(" M");
            for (String text : runner.ffmetadata) out.append(' ').append(text);
            out.append(" Q");
            for (String query : queries) out.append(' ').append(query);
            out.append(" Y");
            for (String rank : runner.ytDlpRanks) out.append(' ').append(rank);
            out.append(" G");
            for (String rank : runner.ffmpegRanks) out.append(' ').append(rank);
            out.append(" P");
            for (String put : sink.puts) out.append(' ').append(put);
            out.append(" R ").append(remaining);
            return out.toString();
        } finally {
            deleteTree(tempRoot);
        }
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
