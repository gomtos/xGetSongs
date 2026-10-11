import com.xgetsongs.shared.filename.FilenameFormatter;
import com.xgetsongs.shared.input.ClassifyResult;
import com.xgetsongs.shared.input.InputClassifier;
import com.xgetsongs.shared.input.ParsedInput;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;

/**
 * The Kotlin originals (shared-jvm.jar) as an oracle for the TypeScript ports. Reads one case per line from stdin, tab-separated;
 * a string is the hex of its UTF-16 units (4 digits each), "." for the empty string and "-" for null. Writes one answer per line.
 *
 *   F rank includeRank(0|1) artist title        -> OK <hex> | ERR
 *   D title                                      -> OK <hex>
 *   G isPlaylist(0|1) playlistTitle typed        -> OK <hex> | NULL
 *   C raw                                        -> P <id> <alsoVideoId> | V <id> | REJECT <REASON>
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
        for (int i = 0; i < text.length(); i++) out.append(String.format("%04x", (int) text.charAt(i)));
        return out.toString();
    }

    public static void main(String[] args) throws Exception {
        BufferedReader in = new BufferedReader(new InputStreamReader(System.in, StandardCharsets.US_ASCII));
        StringBuilder out = new StringBuilder();
        for (String line = in.readLine(); line != null; line = in.readLine()) {
            String[] part = line.split("\t", -1);
            switch (part[0]) {
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
        System.out.print(out);
    }
}
