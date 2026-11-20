#pragma once

#include <QString>
#include <QStringList>

namespace xgs::process {

/// The one command line for CreateProcessW that a program's C runtime splits back into exactly arguments (argv), by
/// Microsoft's documented rules ("Parsing C++ command-line arguments", and CommandLineToArgvW):
///   - arguments are separated by spaces or tabs, and a double-quoted part may hold them;
///   - backslashes are literal unless they come right before a double quote;
///   - 2n backslashes before a `"` are n backslashes and the `"` opens or closes a quoted part; 2n + 1 backslashes before a
///     `"` are n backslashes and a literal `"`.
/// So an argument that is empty or holds a space, a tab, a line feed, a vertical tab or a `"` is put in quotes, every `"` in
/// it is preceded by a backslash, and the backslashes right before a `"` or before the closing quote are doubled. Any other
/// argument is written as it is. No character is special to the C runtime besides these (`%`, `&`, `^`, `!` and the like are
/// only special to cmd.exe, which is never involved).
///
/// The first argument is the program: the C runtime takes it up to the next `"` (when it starts with one) or the first space
/// or tab, without the backslash rules; a program path (which cannot hold `"` or end in `\`) comes out the same under both.
QString windowsCommandLine(const QStringList& arguments);

}  // namespace xgs::process
