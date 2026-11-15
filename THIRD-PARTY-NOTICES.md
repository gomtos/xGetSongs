# Third-party notices

이 저장소의 일부는 아래 제3자 저작물의 알고리즘이나 자료를 따른다. 앱을 포장해 배포할 때 Qt와 WebView2의 고지를 여기에 더한다.

## Kotlin standard library

- Project: Kotlin standard library (kotlin-stdlib), https://github.com/JetBrains/kotlin
- License: Apache License, Version 2.0, https://www.apache.org/licenses/LICENSE-2.0
- Copyright: JetBrains s.r.o. and Kotlin Programming Language contributors
- Used in: `qt/core/text/kt_text.cpp`. Two functions follow the algorithms of the Kotlin standard library so that the C++
  port gives the same answers as the Kotlin originals:
  - `xgs::kt::replace` follows `String.replace(oldValue, newValue, ignoreCase)`;
  - the ignore-case loop of `xgs::kt::indexOf` follows `CharSequence.indexOf(other, startIndex, ignoreCase = true)`.

  No source file of the Kotlin standard library is included in this repository.

## Unicode Character Database

- Data: Unicode Character Database (character properties and case mappings, including SpecialCasing), as defined by the
  Unicode Standard
- License: Unicode License V3, https://www.unicode.org/license.txt
- Copyright: Unicode, Inc.
- Used in: `qt/core/text/kt_tables.inc`, which holds per-character properties and case mappings of the Unicode version that
  Java 21 implements. The data was obtained by querying the public API of a Java runtime (`qt/tools/differential/gen-tables.ps1`
  with `Oracle.java`), not copied from a Java runtime's source.
