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
import java.util.Locale;
import java.util.TreeMap;
import kotlin.text.CharsKt;
import kotlin.text.StringsKt;

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
 *
 * Only for qt/tools/differential/gen-tables.ps1:
 *   ... Oracle.java casetable      Unicode case data as the JVM's public API reports it: String.uppercase()/lowercase() of
 *                                  each single unit where it is not uppercaseChar()/lowercaseChar() (special casing), and
 *                                  the case mappings of the code points above U+FFFF
 */
public class Oracle {
    static String dec(String hex) {
        if (hex.equals("-")) return null;
        if (hex.equals(".")) return "";
        StringBuilder out = new StringBuilder();
        for (int i = 0; i < hex.length(); i += 4) out.append((char) Integer.parseInt(hex.substring(i, i + 4), 16));
        return out.toString();
    }

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

    public static void main(String[] args) throws Exception {
        StringBuilder out = new StringBuilder();
        if (args.length > 0) {
            switch (args[0]) {
                case "chartable" -> chartable(out);
                case "constants" -> constants(out);
                case "unicodecase" -> unicodecase(out);
                case "casetable" -> casetable(out);
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
        print(out);
    }

    static void print(StringBuilder out) {
        PrintStream stdout = new PrintStream(System.out, false, StandardCharsets.US_ASCII);
        stdout.print(out);
        stdout.flush();
    }
}
