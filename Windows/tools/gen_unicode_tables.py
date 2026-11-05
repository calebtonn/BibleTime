#!/usr/bin/env python3
"""Generate the Unicode property tables of the Windows core from the Unicode Character Database.

    python3 Windows/tools/gen_unicode_tables.py <ucd-dir>
    python3 Windows/tools/gen_unicode_tables.py --download   # fetches the files below from unicode.org

<ucd-dir> is an unpacked UCD of the version Swift's standard library uses (UCD_VERSION), laid out as on
https://www.unicode.org/Public/<version>/ucd/:

    UnicodeData.txt  SpecialCasing.txt  DerivedCoreProperties.txt  extracted/DerivedNumericType.txt
    auxiliary/GraphemeBreakProperty.txt  auxiliary/GraphemeBreakTest.txt  emoji/emoji-data.txt

Writes
    Windows/src/core/GraphemeTables.inc  - Grapheme_Cluster_Break + Extended_Pictographic + Indic_Conjunct_Break,
                                           Alphabetic | Numeric_Type != None, White_Space (Zs + tab), Lowercase_Mapping
    Windows/tests/GraphemeBreakTest.inc  - every case of GraphemeBreakTest.txt, for core_tests

Only the standard library is used, so the output is reproducible anywhere.
"""
import os
import re
import sys
import urllib.request

UCD_VERSION = "15.1.0"
FILES = [
    "UnicodeData.txt",
    "SpecialCasing.txt",
    "DerivedCoreProperties.txt",
    "extracted/DerivedNumericType.txt",
    "auxiliary/GraphemeBreakProperty.txt",
    "auxiliary/GraphemeBreakTest.txt",
    "emoji/emoji-data.txt",
]
HERE = os.path.dirname(os.path.abspath(__file__))
CORE_OUT = os.path.join(HERE, "..", "src", "core", "GraphemeTables.inc")
TEST_OUT = os.path.join(HERE, "..", "tests", "GraphemeBreakTest.inc")

# Grapheme_Cluster_Break values, in the order of bt::unicode::Gcb (Unicode.cpp). Hangul syllables (LV/LVT) are
# computed arithmetically there and left out of the table.
GCB = ["Other", "CR", "LF", "Control", "Extend", "ZWJ", "Regional_Indicator", "Prepend", "SpacingMark",
       "L", "V", "T", "LV", "LVT"]
EXT_PICT = 0x10
INCB = {"Consonant": 0x20, "Linker": 0x40, "Extend": 0x80}
HANGUL_FIRST, HANGUL_LAST = 0xAC00, 0xD7A3


def read_lines(ucd, name):
    with open(os.path.join(ucd, name), encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if line:
                yield [field.strip() for field in line.split(";")]


def code_range(field):
    if ".." in field:
        lo, hi = field.split("..")
        return int(lo, 16), int(hi, 16)
    return int(field, 16), int(field, 16)


def property_ranges(ucd, name, wanted=None, column=1):
    """{value: [(lo, hi)]} for the property in `column` (filtered by `wanted` when given)."""
    out = {}
    for fields in read_lines(ucd, name):
        if len(fields) <= column:
            continue
        if wanted is not None and fields[column] != wanted:
            continue
        value = fields[column + 1] if wanted is not None and len(fields) > column + 1 else fields[column]
        out.setdefault(value, []).append(code_range(fields[0]))
    return out


def merge(points):
    """Sorted code points -> [(lo, hi)]."""
    ranges = []
    for cp in sorted(points):
        if ranges and ranges[-1][1] == cp - 1:
            ranges[-1][1] = cp
        else:
            ranges.append([cp, cp])
    return [tuple(r) for r in ranges]


def unicode_data(ucd):
    """[(cp, general_category, simple_lowercase or None)] with First/Last ranges expanded."""
    rows = []
    first = None
    for fields in read_lines(ucd, "UnicodeData.txt"):
        cp, name, gc, lower = int(fields[0], 16), fields[1], fields[2], fields[13]
        if name.endswith(", First>"):
            first = cp
            continue
        if name.endswith(", Last>"):
            rows.extend((c, gc, None) for c in range(first, cp + 1))
            continue
        rows.append((cp, gc, int(lower, 16) if lower else None))
    return rows


def grapheme_table(ucd):
    value = {}
    for name, ranges in property_ranges(ucd, "auxiliary/GraphemeBreakProperty.txt").items():
        for lo, hi in ranges:
            for cp in range(lo, hi + 1):
                value[cp] = GCB.index(name)
    for lo, hi in property_ranges(ucd, "emoji/emoji-data.txt").get("Extended_Pictographic", []):
        for cp in range(lo, hi + 1):
            value[cp] = value.get(cp, 0) | EXT_PICT
    for fields in read_lines(ucd, "DerivedCoreProperties.txt"):
        if len(fields) == 3 and fields[1] == "InCB":
            lo, hi = code_range(fields[0])
            for cp in range(lo, hi + 1):
                value[cp] = value.get(cp, 0) | INCB[fields[2]]
    for cp in range(HANGUL_FIRST, HANGUL_LAST + 1):
        assert value.get(cp) in (GCB.index("LV"), GCB.index("LVT")), hex(cp)
        del value[cp]
    ranges = []
    for cp in sorted(value):
        v = value[cp]
        if ranges and ranges[-1][1] == cp - 1 and ranges[-1][2] == v:
            ranges[-1][1] = cp
        else:
            ranges.append([cp, cp, v])
    return ranges


def alnum_table(ucd):
    points = set()
    for lo, hi in property_ranges(ucd, "DerivedCoreProperties.txt").get("Alphabetic", []):
        points.update(range(lo, hi + 1))
    for name, ranges in property_ranges(ucd, "extracted/DerivedNumericType.txt").items():
        if name != "None":
            for lo, hi in ranges:
                points.update(range(lo, hi + 1))
    return merge(points)


def lowercase_tables(ucd, rows):
    """Simple mappings as runs (first, last, step, delta) plus the unconditional multi-scalar SpecialCasing ones."""
    special = {}
    for fields in read_lines(ucd, "SpecialCasing.txt"):
        if len(fields) >= 5 and fields[4]:
            continue  # conditional (language or context); String.lowercased() ignores these
        cp = int(fields[0], 16)
        lower = [int(x, 16) for x in fields[1].split()]
        if lower != [cp]:
            special[cp] = lower
    mapping = {cp: lower for cp, _, lower in rows if lower is not None and lower != cp}
    for cp in list(special):
        if len(special[cp]) == 1:
            mapping[cp] = special.pop(cp)[0]
        else:
            mapping.pop(cp, None)
    runs = []
    for cp in sorted(mapping):
        delta = mapping[cp] - cp
        if runs:
            first, last, step, d = runs[-1]
            if d == delta and step in (0, cp - last) and cp - last in (1, 2):
                runs[-1] = [first, cp, cp - last, d]
                continue
        runs.append([cp, cp, 0, delta])
    return [(f, l, s or 1, d) for f, l, s, d in runs], special


def whitespace_table(rows):
    return merge([0x09] + [cp for cp, gc, _ in rows if gc == "Zs"])


def write_core(path, gcb, alnum, lower, special, space):
    out = []
    w = out.append
    w("// Generated by Windows/tools/gen_unicode_tables.py from the Unicode Character Database %s. Do not edit." % UCD_VERSION)
    w("// Included by Unicode.cpp only.")
    w("")
    w("// Grapheme_Cluster_Break (low 4 bits, bt::unicode::Gcb order), 0x10 Extended_Pictographic,")
    w("// 0x20/0x40/0x80 Indic_Conjunct_Break Consonant/Linker/Extend. Unlisted scalars are Other with no flags;")
    w("// Hangul syllables U+AC00..U+D7A3 (LV/LVT) are computed, not listed.")
    w("static const GraphemeRange kGraphemeRanges[] = {")
    for lo, hi, v in gcb:
        w("    {0x%05X, 0x%05X, 0x%02X}," % (lo, hi, v))
    w("};")
    w("")
    w("// Alphabetic or Numeric_Type != None (Swift Character.isLetter || isNumber, from the first scalar).")
    w("static const ScalarRange kAlphaNumericRanges[] = {")
    for lo, hi in alnum:
        w("    {0x%05X, 0x%05X}," % (lo, hi))
    w("};")
    w("")
    w("// White_Space as CharacterSet.whitespaces defines it: general category Zs plus U+0009.")
    w("static const ScalarRange kWhitespaceRanges[] = {")
    for lo, hi in space:
        w("    {0x%05X, 0x%05X}," % (lo, hi))
    w("};")
    w("")
    w("// Simple Lowercase_Mapping (UnicodeData.txt field 13 and single-scalar unconditional SpecialCasing.txt entries):")
    w("// scalars first, first+step, ... last map to scalar + delta.")
    w("static const LowerRun kLowerRuns[] = {")
    for f, l, s, d in lower:
        w("    {0x%05X, 0x%05X, %d, %d}," % (f, l, s, d))
    w("};")
    w("")
    w("// Unconditional multi-scalar lowercase mappings from SpecialCasing.txt.")
    w("static const SpecialLower kSpecialLower[] = {")
    for cp in sorted(special):
        parts = list(special[cp]) + [0] * (3 - len(special[cp]))
        w("    {0x%05X, {0x%05X, 0x%05X, 0x%05X}}," % (cp, *parts))
    w("};")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")


def write_tests(path, ucd):
    out = ["// Generated by Windows/tools/gen_unicode_tables.py from GraphemeBreakTest.txt %s. Do not edit." % UCD_VERSION,
           "// Each case: the number of scalars, the scalars, and the indices (into the scalars) where a cluster starts.",
           "// Included by CoreTests.cpp only.",
           "static const GraphemeCase kGraphemeBreakCases[] = {"]
    with open(os.path.join(ucd, "auxiliary/GraphemeBreakTest.txt"), encoding="utf-8") as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            tokens = line.split()
            scalars, starts = [], []
            for t in tokens:
                if t == "÷":
                    if len(scalars) < len(re.findall(r"[0-9A-Fa-f]+", line)):
                        starts.append(len(scalars))
                elif t != "×":
                    scalars.append(int(t, 16))
            out.append("    {%d, {%s}, {%s}}," % (len(scalars), ", ".join("0x%X" % s for s in scalars),
                                                  ", ".join(str(s) for s in starts)))
    out.append("};")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")


def download(target):
    for name in FILES:
        path = os.path.join(target, name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        url = "https://www.unicode.org/Public/%s/ucd/%s" % (UCD_VERSION, name)
        with urllib.request.urlopen(url) as r, open(path, "wb") as f:
            f.write(r.read())


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    ucd = sys.argv[1]
    if ucd == "--download":
        ucd = os.path.join(HERE, "ucd-" + UCD_VERSION)
        download(ucd)
    rows = unicode_data(ucd)
    lower, special = lowercase_tables(ucd, rows)
    write_core(CORE_OUT, grapheme_table(ucd), alnum_table(ucd), lower, special, whitespace_table(rows))
    write_tests(TEST_OUT, ucd)


if __name__ == "__main__":
    main()
