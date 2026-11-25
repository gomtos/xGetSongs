#pragma once

#include <QString>

namespace xgs::net {

/// Kotlin's `URLEncoder.encode(value, UTF_8).replace("+", "%20")`, written from the public description of URLEncoder: the
/// ASCII letters and digits and `.`, `-`, `*`, `_` stay as they are, a space is `%20` (URLEncoder's `+`, replaced; a `+` of
/// the value itself is `%2B`, so only spaces become `%20`), and every other character is its UTF-8 bytes as `%XX` with
/// upper-case hex digits. An unpaired surrogate, which has no UTF-8 form, is `%3F` (the `?` that the JVM's encoders put in
/// place of what they cannot encode): observed on the JVM by the Q2c Task 4 differential (kind W op lf), as was the rest.
/// Used by the LRCLIB provider and by the Google search address.
QString encodeQueryValue(const QString& value);

}  // namespace xgs::net
