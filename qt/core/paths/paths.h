#pragma once

#include <QString>

#include <optional>

// Path strings that behave like Kotlin's java.nio.file.Path on Windows (the engine builds every path with Path.of and
// resolve). The behaviour was taken only from the answers the differential oracle observed (kind W, qt/README.md), not from
// any JDK source. In short:
//  - A separator is `\` or `/`; a run of separators is one, and a trailing one is dropped. The string uses `\`.
//  - The root is a drive (`C:\`, or `C:` for a drive-relative path; an ASCII letter only), `\` (the current drive's root),
//    or a UNC root `\\host\share\` (two or more separators in front; the host and the share must both be there). A leading
//    `\\?\` (exactly that) followed by `X:` and a separator, or by `UNC\`, is dropped.
//  - No other clean-up: `.` and `..` stay, and so do trailing spaces and dots.
//  - A path with `< > : " | ? *` or a unit below U+0020 (outside a drive's `:` and the `\\?\` prefix) is not a path.
// Unpaired surrogates are ordinary units.
namespace xgs::paths {

/// Kotlin's `Path.of(text).toString()`. Empty when Path.of throws for the text (InvalidPathException). No clean-up
/// (`.` and `..` stay).
std::optional<QString> fromText(const QString& text);

/// `dir.resolve(name).toString()`: name when name is absolute. dir must be a result of fromText. Empty when either is not a
/// path. The result is not cleaned again (like Kotlin, `C:\a` resolve `C:` is `C:\a\`).
std::optional<QString> child(const QString& dir, const QString& name);

/// `Path.fileName`: the last part of a path (a result of fromText). Empty when the path is a root only (or not a path); the
/// empty path's file name is the empty string.
std::optional<QString> fileName(const QString& path);

/// kotlin.io.path `nameWithoutExtension`: the file name up to its last `.`, the empty string when there is no file name.
QString nameWithoutExtension(const QString& path);

/// `path.resolveSibling(name).toString()`: the parent of path resolved with name, or name (as fromText gives it) when path
/// has no parent. Empty when either is not a path.
std::optional<QString> sibling(const QString& path, const QString& name);

}  // namespace xgs::paths
