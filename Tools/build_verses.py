#!/usr/bin/env python3
"""
build_verses.py - data pipeline for the BibleTime screen saver.

Turns eBible.org USFX files (public-domain English Bibles) into the compact
JSON resources bundled with the screen saver:

  Resources/Translations/<id>.json   one file per translation
  Resources/curated.json             famous verses per clock time ("H:MM")
  Resources/hour_verses.json         time-themed verses for the top of the hour
  Resources/books.json               the 66 Protestant books, names and chapter counts

Python 3.9+, standard library only.  Run it with `python3 -I` so that nothing
from the (untrusted) data directories can be imported by accident.

Sub-commands
------------
  build     Build one translation JSON from a USFX file.
  lists     Build curated.json / hour_verses.json / books.json from the
            already-built translation files.
  all       build + lists for every entry in TRANSLATIONS (see below).
  verify    Print statistics and sample verses for the generated resources.

Typical use (from the repository root):

  python3 -I Tools/build_verses.py all \
      --data-dir /path/to/ebible --curated /path/to/curated_verses.json \
      --hours /path/to/hour_verses.json --out-dir Resources [--download]

  python3 -I Tools/build_verses.py build \
      --source /path/to/eng-kjv2006_usfx.xml --copr /path/to/copr.htm \
      --id kjv --name "King James Version" --abbreviation KJV \
      --url https://ebible.org/Scriptures/eng-kjv2006_usfx.zip \
      --curated /path/to/curated_verses.json --hours /path/to/hour_verses.json \
      --out Resources/Translations/kjv.json

  python3 -I Tools/build_verses.py lists \
      --translations Resources/Translations/kjv.json Resources/Translations/web.json \
                     Resources/Translations/asv.json \
      --books-source /path/to/eng-kjv2006_usfx.xml \
      --curated /path/to/curated_verses.json --hours /path/to/hour_verses.json \
      --out-dir Resources

  python3 -I Tools/build_verses.py verify --resources Resources \
      [--curated /path/to/curated_verses.json --hours /path/to/hour_verses.json]

Translation JSON schema
-----------------------
  {
    "id": "kjv", "name": "King James Version", "abbreviation": "KJV",
    "language": "en",
    "source": {"publisher": "eBible.org", "edition": "...", "url": "...",
               "sourceFilesDated": "...", "generated": "...", "sha256": "...",
               "license": "Public Domain"},
    "verses": [ [usfmBookCode, chapter, verse, text, flags], ... ]
  }

A verse is included iff (chapter <= 23 and verse <= 60) or its reference is
in the curated / hour-verse lists.  Verses with empty text (textually omitted
in that translation) are left out.  `flags` is a bitmask:

  1  EXCLUDE   reads badly on its own (genealogies, name lists, census
               numbers, fragments starting lowercase or with "(", and a
               fixed set of list-heavy chapter ranges)
  2  PENALIZE  soft: trailing comma/dash, dialogue tags, very short (unless
               it is a complete sentence), or the sentence continues into
               the next verse (next verse starts lowercase)

Curated and hour-verse references are always flag 0.

`verify` prints statistics and, when --curated/--hours are given, exits 1 if
the generated lists do not match them or a translation lacks a listed verse.
"""

import argparse
import hashlib
import html
import io
import json
import os
import re
import sys
import urllib.request
import xml.etree.ElementTree as ET
import zipfile

# ---------------------------------------------------------------------------
# Configuration for `all`
# ---------------------------------------------------------------------------

EBIBLE_SCRIPTURES = "https://ebible.org/Scriptures/"

# Each entry: our id -> eBible translation id, display name, abbreviation.
# Source file for `all` is <data-dir>/<ebible_id>/<ebible_id>_usfx.xml plus
# <data-dir>/<ebible_id>/copr.htm (both come out of the eBible USFX zip).
TRANSLATIONS = [
    {"id": "kjv", "ebible_id": "eng-kjv2006", "name": "King James Version", "abbreviation": "KJV"},
    {"id": "web", "ebible_id": "engwebp", "name": "World English Bible", "abbreviation": "WEB"},
    {"id": "asv", "ebible_id": "eng-asv", "name": "American Standard Version", "abbreviation": "ASV"},
]


def ebible_zip_url(ebible_id):
    return EBIBLE_SCRIPTURES + ebible_id + "_usfx.zip"


# ---------------------------------------------------------------------------
# Inclusion window and flag rules
# ---------------------------------------------------------------------------

MAX_CHAPTER = 23   # clock hours 1-12 fit easily; 23 leaves room for 24h clocks
MAX_VERSE = 60

FLAG_EXCLUDE = 1
FLAG_PENALIZE = 2

# Reference-based exclusions: (book, first chapter, last chapter, first verse,
# last verse).  None for a verse bound means the whole chapter.
EXCLUDE_RANGES = [
    ("GEN", 5, 5, None, None),
    ("GEN", 10, 10, None, None),
    ("GEN", 11, 11, 10, 32),
    ("EXO", 6, 6, 14, 25),
    ("NUM", 1, 4, None, None),
    ("NUM", 7, 7, None, None),
    ("NUM", 10, 10, 14, 27),    # standard-bearers of the camps
    ("NUM", 13, 13, 4, 15),     # the twelve spies
    ("JOS", 12, 12, 9, 24),     # the thirty-one kings
    ("1CH", 1, 9, None, None),
    ("EZR", 2, 2, None, None),
    ("EZR", 8, 8, None, None),
    ("EZR", 10, 10, 18, 44),
    ("NEH", 3, 3, None, None),
    ("NEH", 7, 7, None, None),
    ("NEH", 10, 12, None, None),
    ("MAT", 1, 1, 1, 17),
    ("LUK", 3, 3, 23, 38),
    ("ROM", 16, 16, 5, 15),     # greetings roster
]

RE_BEGAT = re.compile(r"\bbegat\b")
RE_SONS_OF = re.compile(r"\bthe sons? of\b")
# Three or more capitalised names joined by commas and/or "and" (KJV style:
# "Shem, Ham, and Japheth." / "Peter and James and John").
RE_NAME_LIST = re.compile(r"^(?:And )?(?:[A-Z][a-z’-]+,? (?:and )?){3,}[A-Z][a-z’-]+[.,;:]?$")
RE_CENSUS = re.compile(r"\b(?:thousand|hundred)\b")
RE_TRAILING = re.compile(r"[,—-]\s*$")
RE_DIALOGUE_START = re.compile(r"^And (?:he|she|they) (?:said|answered)")
RE_DIALOGUE_TAG = re.compile(r"(?:said|saith|spake|answered)\s+(?:unto|to)\s+(?:him|her|them)\b.*,\s*$")
MIN_LENGTH = 25
MIN_SENTENCE_WORDS = 3   # a short verse this long ending in . ! ? is a sentence, not a fragment

QUOTE_CHARS = "“‘\"'"          # “ ‘ " '
CLOSE_QUOTE_CHARS = "”’\"'"    # ” ’ " '


def strip_leading_quotes(text):
    return text.lstrip(QUOTE_CHARS)


def starts_lowercase(text):
    """True if the first character after any opening quotes is lowercase,
    i.e. the verse is the tail of a sentence begun in the previous verse."""
    return strip_leading_quotes(text)[:1].islower()


def is_complete_sentence(text):
    body = text.rstrip(CLOSE_QUOTE_CHARS).rstrip()
    return body[-1:] in ".!?" and len(body.split()) >= MIN_SENTENCE_WORDS


def in_excluded_range(code, chapter, verse):
    for book, c0, c1, v0, v1 in EXCLUDE_RANGES:
        if book != code or chapter < c0 or chapter > c1:
            continue
        if v0 is None:
            return True
        if c0 == c1:
            if v0 <= verse <= v1:
                return True
        elif chapter == c0:
            if verse >= v0:
                return True
        elif chapter == c1:
            if verse <= v1:
                return True
        else:
            return True
    return False


def compute_flags(code, chapter, verse, text, whitelist, next_text=None):
    """Return the flags bitmask for one verse of one translation.

    `next_text` is the text of the following verse in the same chapter (or
    None / ""), used to spot verses whose sentence runs on into the next
    verse."""
    if (code, chapter, verse) in whitelist:
        return 0
    flags = 0
    if in_excluded_range(code, chapter, verse):
        flags |= FLAG_EXCLUDE
    if starts_lowercase(text) or text[:1] == "(":
        flags |= FLAG_EXCLUDE
    if RE_BEGAT.search(text):
        flags |= FLAG_EXCLUDE
    if len(RE_SONS_OF.findall(text)) >= 3:
        flags |= FLAG_EXCLUDE
    if RE_NAME_LIST.match(text):
        flags |= FLAG_EXCLUDE
    if len(RE_CENSUS.findall(text)) >= 3:
        flags |= FLAG_EXCLUDE
    if RE_TRAILING.search(text):
        flags |= FLAG_PENALIZE
    if RE_DIALOGUE_START.match(text) or RE_DIALOGUE_TAG.search(text):
        flags |= FLAG_PENALIZE
    if len(text) < MIN_LENGTH and not is_complete_sentence(text):
        flags |= FLAG_PENALIZE
    if next_text and starts_lowercase(next_text):
        flags |= FLAG_PENALIZE
    return flags


# ---------------------------------------------------------------------------
# Books (Protestant canon, 66) with display names and name aliases
# ---------------------------------------------------------------------------

# (usfm code, display name, short name, extra aliases)
BOOKS = [
    ("GEN", "Genesis", "Gen", ()),
    ("EXO", "Exodus", "Exod", ("Ex",)),
    ("LEV", "Leviticus", "Lev", ()),
    ("NUM", "Numbers", "Num", ()),
    ("DEU", "Deuteronomy", "Deut", ()),
    ("JOS", "Joshua", "Josh", ()),
    ("JDG", "Judges", "Judg", ()),
    ("RUT", "Ruth", "Ruth", ()),
    ("1SA", "1 Samuel", "1 Sam", ("I Samuel",)),
    ("2SA", "2 Samuel", "2 Sam", ("II Samuel",)),
    ("1KI", "1 Kings", "1 Kgs", ("I Kings",)),
    ("2KI", "2 Kings", "2 Kgs", ("II Kings",)),
    ("1CH", "1 Chronicles", "1 Chr", ("I Chronicles",)),
    ("2CH", "2 Chronicles", "2 Chr", ("II Chronicles",)),
    ("EZR", "Ezra", "Ezra", ()),
    ("NEH", "Nehemiah", "Neh", ()),
    ("EST", "Esther", "Esth", ()),
    ("JOB", "Job", "Job", ()),
    ("PSA", "Psalm", "Ps", ("Psalms", "Psa", "Pss")),
    ("PRO", "Proverbs", "Prov", ("Proverb",)),
    ("ECC", "Ecclesiastes", "Eccl", ("Qoheleth",)),
    ("SNG", "Song of Solomon", "Song", ("Song of Songs", "Canticles", "Canticle of Canticles")),
    ("ISA", "Isaiah", "Isa", ()),
    ("JER", "Jeremiah", "Jer", ()),
    ("LAM", "Lamentations", "Lam", ()),
    ("EZK", "Ezekiel", "Ezek", ()),
    ("DAN", "Daniel", "Dan", ()),
    ("HOS", "Hosea", "Hos", ()),
    ("JOL", "Joel", "Joel", ()),
    ("AMO", "Amos", "Amos", ()),
    ("OBA", "Obadiah", "Obad", ()),
    ("JON", "Jonah", "Jonah", ()),
    ("MIC", "Micah", "Mic", ()),
    ("NAM", "Nahum", "Nah", ()),
    ("HAB", "Habakkuk", "Hab", ()),
    ("ZEP", "Zephaniah", "Zeph", ()),
    ("HAG", "Haggai", "Hag", ()),
    ("ZEC", "Zechariah", "Zech", ()),
    ("MAL", "Malachi", "Mal", ()),
    ("MAT", "Matthew", "Matt", ()),
    ("MRK", "Mark", "Mark", ()),
    ("LUK", "Luke", "Luke", ()),
    ("JHN", "John", "John", ()),
    ("ACT", "Acts", "Acts", ("Acts of the Apostles",)),
    ("ROM", "Romans", "Rom", ()),
    ("1CO", "1 Corinthians", "1 Cor", ("I Corinthians",)),
    ("2CO", "2 Corinthians", "2 Cor", ("II Corinthians",)),
    ("GAL", "Galatians", "Gal", ()),
    ("EPH", "Ephesians", "Eph", ()),
    ("PHP", "Philippians", "Phil", ()),
    ("COL", "Colossians", "Col", ()),
    ("1TH", "1 Thessalonians", "1 Thess", ("I Thessalonians",)),
    ("2TH", "2 Thessalonians", "2 Thess", ("II Thessalonians",)),
    ("1TI", "1 Timothy", "1 Tim", ("I Timothy",)),
    ("2TI", "2 Timothy", "2 Tim", ("II Timothy",)),
    ("TIT", "Titus", "Titus", ()),
    ("PHM", "Philemon", "Phlm", ()),
    ("HEB", "Hebrews", "Heb", ()),
    ("JAS", "James", "Jas", ()),
    ("1PE", "1 Peter", "1 Pet", ("I Peter",)),
    ("2PE", "2 Peter", "2 Pet", ("II Peter",)),
    ("1JN", "1 John", "1 John", ("I John",)),
    ("2JN", "2 John", "2 John", ("II John",)),
    ("3JN", "3 John", "3 John", ("III John",)),
    ("JUD", "Jude", "Jude", ()),
    ("REV", "Revelation", "Rev", ("Revelations", "Apocalypse", "Revelation of John")),
]

BOOK_ORDER = {b[0]: i for i, b in enumerate(BOOKS)}
BOOK_CODES = set(BOOK_ORDER)


def _norm_name(name):
    return re.sub(r"[^a-z0-9]", "", name.lower())


BOOK_BY_NAME = {}
for _code, _name, _short, _aliases in BOOKS:
    for _n in (_code, _name, _short) + tuple(_aliases):
        BOOK_BY_NAME[_norm_name(_n)] = _code

RE_REF = re.compile(r"^\s*(.+?)\s+(\d+):(\d+)\s*$")


def parse_ref(ref):
    """'Song of Solomon 2:4' -> ('SNG', 2, 4).  Raises ValueError if unknown."""
    m = RE_REF.match(ref)
    if not m:
        raise ValueError("cannot parse reference %r" % ref)
    code = BOOK_BY_NAME.get(_norm_name(m.group(1)))
    if code is None:
        raise ValueError("unknown book name in reference %r" % ref)
    return (code, int(m.group(2)), int(m.group(3)))


# ---------------------------------------------------------------------------
# Curated / hour verse lists
# ---------------------------------------------------------------------------

RE_SLOT = re.compile(r"^(\d{1,2}):(\d{2})$")


def load_curated(path):
    """{"H:MM": ["John 3:16", ...]} -> ordered list of (slot, [(code,c,v), ...])."""
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    if not isinstance(data, dict):
        raise ValueError("%s: expected a JSON object" % path)
    out = []
    for slot, refs in data.items():
        m = RE_SLOT.match(slot)
        if not m:
            raise ValueError("%s: bad slot key %r" % (path, slot))
        hour, minute = int(m.group(1)), int(m.group(2))
        if not (1 <= hour <= 12 and 1 <= minute <= 59):
            raise ValueError("%s: slot %r out of range" % (path, slot))
        parsed = []
        for ref in refs:
            code, chapter, verse = parse_ref(ref)
            if (chapter, verse) != (hour, minute):
                raise ValueError("%s: %r does not belong in slot %r" % (path, ref, slot))
            parsed.append((code, chapter, verse))
        out.append(("%d:%02d" % (hour, minute), parsed))
    return out


def load_hours(path):
    """["Psalm 90:12", ...] -> [(code,c,v), ...]."""
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    if not isinstance(data, list):
        raise ValueError("%s: expected a JSON array" % path)
    return [parse_ref(ref) for ref in data]


def whitelist_from(curated, hours):
    refs = set()
    for _slot, lst in curated:
        refs.update(lst)
    refs.update(hours)
    return refs


# ---------------------------------------------------------------------------
# copr.htm metadata
# ---------------------------------------------------------------------------

RE_TITLE = re.compile(r"<title>(.*?)</title>", re.S | re.I)
RE_GENERATED = re.compile(
    r"HTML generated with .*?Haiola.*?eBible\.org</a>\s*([0-9]{1,2}\s+[A-Za-z]{3,9}\s+[0-9]{4})"
    r"\s+from source files dated\s+([0-9]{1,2}\s+[A-Za-z]{3,9}\s+[0-9]{4})", re.S)


def parse_copr(path):
    with open(path, encoding="utf-8-sig") as f:
        text = f.read()
    m = RE_TITLE.search(text)
    edition = html.unescape(re.sub(r"\s+", " ", m.group(1))).strip() if m else ""
    m = RE_GENERATED.search(text)
    generated = re.sub(r"\s+", " ", m.group(1)) if m else ""
    dated = re.sub(r"\s+", " ", m.group(2)) if m else ""
    if not edition or not generated or not dated:
        raise ValueError("%s: could not find edition / generation stamps" % path)
    return {"edition": edition, "sourceFilesDated": dated, "generated": generated}


# ---------------------------------------------------------------------------
# USFX parsing
# ---------------------------------------------------------------------------

# Elements whose entire content is dropped (their tail text is kept, because
# the tail belongs to the enclosing element).
SKIP_TAGS = {
    "f", "x", "fe",          # footnotes, cross references, endnotes
    "d",                     # Psalm superscriptions
    "s", "r", "sr", "mr", "ms", "mt",  # headings
    "h", "toc", "id", "ide", "rem", "cl", "cp", "ca", "va", "vp",
    "fig", "periph",
}

# Elements whose text is set off from the surrounding words (e.g. "Selah").
SPACED_TAGS = {"qs"}

RE_SPACES = re.compile(r"\s+")


def clean_text(raw):
    """Pilcrows and the square brackets some editions print around 'Selah'
    or italicised words are dropped (eBible's own VPL export does the same),
    then whitespace is collapsed."""
    text = raw.replace("¶", " ").replace("[", "").replace("]", "")
    return RE_SPACES.sub(" ", text).strip()


class _BookWalker(object):
    """Collects verse text from one <book> element."""

    def __init__(self, code):
        self.code = code
        self.chapter = None
        self.max_chapter = 0
        self.current = None      # (code, chapter, verse) while inside a verse
        self.parts = {}          # key -> list of text fragments
        self.bridged = []        # (code, chapter, first, last) for "18-19" ids

    def _add(self, text, suppressed=False):
        if self.current is not None and text and not suppressed:
            self.parts.setdefault(self.current, []).append(text)

    def _open(self, vid):
        self.current = None
        m = re.match(r"^\s*(\d+)(?:\s*-\s*(\d+))?", vid or "")
        if not m:
            return
        first = int(m.group(1))
        chapter = self.chapter if self.chapter is not None else 1
        if m.group(2):
            self.bridged.append((self.code, chapter, first, int(m.group(2))))
        self.current = (self.code, chapter, first)
        self.parts.setdefault(self.current, [])

    def walk(self, elem, suppressed=False):
        """Walk `elem` in document order.  `suppressed` is True inside a
        skipped element (heading, footnote, ...): its text is dropped, but
        verse / chapter milestones inside it are still honoured."""
        tag = elem.tag
        if tag == "v":
            self._open(elem.get("id"))
        elif tag == "ve":
            self.current = None
        elif tag == "c":
            self.current = None
            cid = elem.get("id") or ""
            if cid.isdigit():
                self.chapter = int(cid)
                self.max_chapter = max(self.max_chapter, self.chapter)
        else:
            inner = suppressed or tag in SKIP_TAGS
            if tag in SPACED_TAGS:
                self._add(" ", inner)
            self._add(elem.text, inner)
            for child in elem:
                self.walk(child, inner)
            if tag in SPACED_TAGS:
                self._add(" ", inner)
        self._add(elem.tail, suppressed)

    def finish(self):
        self.current = None
        return {k: clean_text("".join(v)) for k, v in self.parts.items()}


def parse_usfx(path, wanted=BOOK_CODES):
    """Return (verses, chapters, bridged).

    verses:   {(code, chapter, verse): text}  (text may be "" for omitted verses)
    chapters: {code: number of chapters}
    bridged:  list of (code, chapter, first, last) verse bridges seen
    """
    verses = {}
    chapters = {}
    bridged = []
    for _event, elem in ET.iterparse(path, events=("end",)):
        if elem.tag != "book":
            continue
        code = elem.get("id")
        if code in wanted:
            walker = _BookWalker(code)
            walker.walk(elem)
            verses.update(walker.finish())
            chapters[code] = walker.max_chapter
            bridged.extend(walker.bridged)
        elem.clear()
    return verses, chapters, bridged


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def sha256_of(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def write_json(path, obj):
    d = os.path.dirname(path)
    if d:
        os.makedirs(d, exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(json.dumps(obj, separators=(",", ":"), ensure_ascii=False))
        f.write("\n")


def read_json(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def verse_sort_key(item):
    return (BOOK_ORDER[item[0]], item[1], item[2])


def log(msg):
    sys.stderr.write(msg + "\n")


# ---------------------------------------------------------------------------
# build
# ---------------------------------------------------------------------------

def require_file(path, hint):
    """Exit with a one-line message if `path` does not exist."""
    if not os.path.isfile(path):
        sys.exit("error: missing %s (%s)" % (path, hint))


def build_translation(source, copr, tid, name, abbreviation, url, curated, hours, out):
    require_file(source, "USFX source; download %s" % url)
    require_file(copr, "copr.htm from the same zip; download %s" % url)
    whitelist = whitelist_from(curated, hours)
    meta = parse_copr(copr)
    log("[%s] parsing %s" % (tid, source))
    verses, chapters, bridged = parse_usfx(source)
    for b in bridged:
        log("[%s] note: bridged verse %s %d:%d-%d stored under verse %d" % (tid, b[0], b[1], b[2], b[3], b[2]))

    rows = []
    empty = []
    for (code, chapter, verse), text in verses.items():
        key = (code, chapter, verse)
        if not ((chapter <= MAX_CHAPTER and verse <= MAX_VERSE) or key in whitelist):
            continue
        if not text:
            empty.append(key)
            continue
        next_text = verses.get((code, chapter, verse + 1))
        rows.append([code, chapter, verse, text,
                     compute_flags(code, chapter, verse, text, whitelist, next_text)])
    rows.sort(key=verse_sort_key)
    for key in sorted(empty, key=verse_sort_key):
        log("[%s] omitted (empty text): %s %d:%d" % (tid, key[0], key[1], key[2]))
    missing = sorted((k for k in whitelist if k not in verses or not verses[k]), key=verse_sort_key)
    for key in missing:
        log("[%s] whitelisted reference not present: %s %d:%d" % (tid, key[0], key[1], key[2]))

    doc = {
        "id": tid,
        "name": name,
        "abbreviation": abbreviation,
        "language": "en",
        "source": {
            "publisher": "eBible.org",
            "edition": meta["edition"],
            "url": url,
            "sourceFilesDated": meta["sourceFilesDated"],
            "generated": meta["generated"],
            "sha256": sha256_of(source),
            "license": "Public Domain",
        },
        "verses": rows,
    }
    write_json(out, doc)
    log("[%s] wrote %s (%d verses, %d books, sha256 %s)" % (
        tid, out, len(rows), len(chapters), doc["source"]["sha256"][:12]))
    return doc, chapters


# ---------------------------------------------------------------------------
# lists (curated.json, hour_verses.json, books.json)
# ---------------------------------------------------------------------------

def present_in_all(translation_docs):
    """Set of (code, chapter, verse) with non-empty text in every translation."""
    sets = []
    for doc in translation_docs:
        sets.append(set((v[0], v[1], v[2]) for v in doc["verses"] if v[3]))
    common = sets[0]
    for s in sets[1:]:
        common = common & s
    return common


def build_lists(translation_docs, curated, hours, chapters, out_dir):
    common = present_in_all(translation_docs)
    ids = ",".join(d["id"] for d in translation_docs)

    curated_out = {}
    for slot, refs in curated:
        kept = []
        for ref in refs:
            if ref in common:
                kept.append(list(ref))
            else:
                log("[lists] dropped curated %s %d:%d (slot %s): not in all of %s" % (ref[0], ref[1], ref[2], slot, ids))
        if kept:
            curated_out[slot] = kept
        else:
            log("[lists] slot %s has no curated verses left" % slot)
    write_json(os.path.join(out_dir, "curated.json"), curated_out)

    hours_out = []
    for ref in hours:
        if ref in common:
            hours_out.append(list(ref))
        else:
            log("[lists] dropped hour verse %s %d:%d: not in all of %s" % (ref[0], ref[1], ref[2], ids))
    write_json(os.path.join(out_dir, "hour_verses.json"), hours_out)

    books_out = []
    for code, name, short, _aliases in BOOKS:
        if code not in chapters or chapters[code] == 0:
            raise ValueError("no chapters found for %s in the books source" % code)
        books_out.append({"code": code, "name": name, "short": short, "chapters": chapters[code]})
    write_json(os.path.join(out_dir, "books.json"), books_out)
    log("[lists] wrote curated.json (%d slots), hour_verses.json (%d), books.json (%d)" % (
        len(curated_out), len(hours_out), len(books_out)))


# ---------------------------------------------------------------------------
# download (for `all --download`)
# ---------------------------------------------------------------------------

def download_usfx(ebible_id, data_dir):
    url = ebible_zip_url(ebible_id)
    dest_dir = os.path.join(data_dir, ebible_id)
    os.makedirs(dest_dir, exist_ok=True)
    zip_path = os.path.join(dest_dir, ebible_id + "_usfx.zip")
    log("[download] %s -> %s" % (url, zip_path))
    with urllib.request.urlopen(url, timeout=60) as resp, open(zip_path, "wb") as f:
        while True:
            chunk = resp.read(1 << 20)
            if not chunk:
                break
            f.write(chunk)
    log("[download] sha256 %s  %s" % (sha256_of(zip_path), zip_path))
    with zipfile.ZipFile(zip_path) as z:
        for info in z.infolist():
            name_ = info.filename
            parts = name_.replace("\\", "/").split("/")
            if name_.startswith(("/", "\\")) or os.path.isabs(name_) or ".." in parts:
                raise ValueError("%s: refusing unsafe zip member %r" % (zip_path, name_))
            base = os.path.basename(name_)
            if not base or info.is_dir():
                continue
            with z.open(info) as src, open(os.path.join(dest_dir, base), "wb") as dst:
                dst.write(src.read())


# ---------------------------------------------------------------------------
# verify
# ---------------------------------------------------------------------------

SAMPLE_REFS = [
    ("JHN", 3, 16), ("PHP", 4, 13), ("COL", 3, 14), ("PSA", 23, 1), ("GEN", 1, 1),
    ("MAT", 6, 33), ("JUD", 1, 24), ("ROM", 14, 24),
]
RE_STRONGS = re.compile(r"\b[GH]\d{3,5}\b")


def slot_ranges(slots):
    """[(h, m), ...] -> 'H:MM-H:MM, H:MM, ...' with runs of consecutive
    minutes within one hour collapsed."""
    out = []
    run = None
    for h, m in sorted(slots):
        if run and run[0] == h and run[2] + 1 == m:
            run[2] = m
        else:
            if run:
                out.append(run)
            run = [h, m, m]
    if run:
        out.append(run)
    return ", ".join("%d:%02d" % (h, a) if a == b else "%d:%02d-%d:%02d" % (h, a, h, b) for h, a, b in out)


def verify(resources_dir, curated_path=None, hours_path=None):
    tdir = os.path.join(resources_dir, "Translations")
    ok = True
    translations = []
    for fname in sorted(os.listdir(tdir)):
        if not fname.endswith(".json"):
            continue
        path = os.path.join(tdir, fname)
        doc = read_json(path)
        translations.append(doc)
        verses = doc["verses"]
        by_ref = {(v[0], v[1], v[2]): v for v in verses}
        print("== %s (%s) %s, %d bytes" % (doc["id"], doc["abbreviation"], path, os.path.getsize(path)))
        print("   edition: %s | source files dated %s | generated %s | sha256 %s" % (
            doc["source"]["edition"], doc["source"]["sourceFilesDated"],
            doc["source"]["generated"], doc["source"]["sha256"]))
        print("   total verses: %d" % len(verses))
        counts = {}
        for v in verses:
            counts[v[4]] = counts.get(v[4], 0) + 1
        print("   flags: " + ", ".join("%d -> %d" % (k, counts[k]) for k in sorted(counts)))
        print("   usable (bit 1 clear): %d" % sum(1 for v in verses if not (v[4] & FLAG_EXCLUDE)))
        slots = set((v[1], v[2]) for v in verses if not (v[4] & FLAG_EXCLUDE) and 1 <= v[1] <= 12 and 1 <= v[2] <= 59)
        empty = [(h, m) for h in range(1, 13) for m in range(1, 60) if (h, m) not in slots]
        print("   12h slots covered: %d / 708" % (708 - len(empty)))
        print("   12h slots with no usable verse: %d%s" % (
            len(empty), (" (" + slot_ranges(empty) + ")") if empty else ""))
        bad = [v for v in verses if any(ch in v[3] for ch in "¶<>") or RE_STRONGS.search(v[3])]
        print("   verses containing pilcrow/</>/Strong's numbers: %d" % len(bad))
        for v in bad[:5]:
            print("     %s %d:%d %r" % (v[0], v[1], v[2], v[3][:80]))
        ok = ok and not bad
        ordered = all(verse_sort_key(verses[i]) < verse_sort_key(verses[i + 1]) for i in range(len(verses) - 1))
        print("   canonical order & unique: %s" % ordered)
        ok = ok and ordered
        for ref in SAMPLE_REFS:
            v = by_ref.get(ref)
            print("   %s %d:%d [%s]: %s" % (ref[0], ref[1], ref[2], v[4] if v else "-", v[3] if v else "(absent)"))
        if ("PSA", 23, 1) in by_ref and "Psalm" in by_ref[("PSA", 23, 1)][3]:
            print("   ERROR: Psalm 23:1 still contains its title")
            ok = False

    generated = {}
    for fname in ("curated.json", "hour_verses.json", "books.json"):
        path = os.path.join(resources_dir, fname)
        data = read_json(path)
        generated[fname] = data
        n = sum(len(v) for v in data.values()) if isinstance(data, dict) else len(data)
        print("== %s: %d bytes, %d entries" % (path, os.path.getsize(path), n))

    if curated_path or hours_path:
        ok = verify_lists(translations, generated, curated_path, hours_path) and ok
    return ok


def _fmt_ref(ref):
    return "%s %d:%d" % tuple(ref[:3])


def verify_lists(translations, generated, curated_path, hours_path):
    """Compare the generated curated.json / hour_verses.json with the
    human-readable input lists and check every listed reference exists in
    every translation.  Returns False on any mismatch."""
    ok = True
    expected = set()
    if curated_path:
        want = set()
        for slot, refs in load_curated(curated_path):
            for ref in refs:
                want.add((slot,) + ref)
                expected.add(ref)
        have = set()
        for slot, refs in generated["curated.json"].items():
            for ref in refs:
                have.add((slot, ref[0], ref[1], ref[2]))
        missing = sorted(want - have)
        extra = sorted(have - want)
        print("== curated.json vs %s: %d listed, %d generated, %d missing, %d extra" % (
            curated_path, len(want), len(have), len(missing), len(extra)))
        for item in missing:
            print("   ERROR: missing from curated.json: slot %s %s" % (item[0], _fmt_ref(item[1:])))
        for item in extra:
            print("   ERROR: not in input list: slot %s %s" % (item[0], _fmt_ref(item[1:])))
        ok = ok and not missing and not extra
    if hours_path:
        want = set(load_hours(hours_path))
        expected |= want
        have = set((r[0], r[1], r[2]) for r in generated["hour_verses.json"])
        missing = sorted(want - have, key=verse_sort_key)
        extra = sorted(have - want, key=verse_sort_key)
        print("== hour_verses.json vs %s: %d listed, %d generated, %d missing, %d extra" % (
            hours_path, len(want), len(have), len(missing), len(extra)))
        for ref in missing:
            print("   ERROR: missing from hour_verses.json: %s" % _fmt_ref(ref))
        for ref in extra:
            print("   ERROR: not in input list: %s" % _fmt_ref(ref))
        ok = ok and not missing and not extra
    for doc in translations:
        present = set((v[0], v[1], v[2]) for v in doc["verses"] if v[3])
        lacking = sorted(expected - present, key=verse_sort_key)
        by_ref = {(v[0], v[1], v[2]): v[4] for v in doc["verses"]}
        flagged = sorted((r for r in expected & present if by_ref.get(r)), key=verse_sort_key)
        print("== %s: %d listed references, %d absent, %d with non-zero flags" % (
            doc["id"], len(expected), len(lacking), len(flagged)))
        for ref in lacking:
            print("   ERROR: %s lacks %s" % (doc["id"], _fmt_ref(ref)))
        for ref in flagged:
            print("   ERROR: %s has flags %d on listed %s" % (doc["id"], by_ref[ref], _fmt_ref(ref)))
        ok = ok and not lacking and not flagged
    return ok


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd")
    sub.required = True

    p = sub.add_parser("build", help="build one translation JSON from a USFX file")
    p.add_argument("--source", required=True, help="USFX xml file")
    p.add_argument("--copr", required=True, help="copr.htm from the same eBible zip")
    p.add_argument("--id", required=True, help="translation id, e.g. kjv")
    p.add_argument("--name", required=True)
    p.add_argument("--abbreviation", required=True)
    p.add_argument("--url", required=True, help="download URL of the zip the source came from")
    p.add_argument("--curated", required=True, help="curated_verses.json (human-readable refs)")
    p.add_argument("--hours", required=True, help="hour_verses.json (human-readable refs)")
    p.add_argument("--out", required=True)

    p = sub.add_parser("lists", help="build curated.json, hour_verses.json, books.json")
    p.add_argument("--translations", required=True, nargs="+", help="generated translation JSON files")
    p.add_argument("--books-source", required=True, help="USFX xml used to count chapters (KJV)")
    p.add_argument("--curated", required=True)
    p.add_argument("--hours", required=True)
    p.add_argument("--out-dir", required=True)

    p = sub.add_parser("all", help="build everything in TRANSLATIONS")
    p.add_argument("--data-dir", required=True, help="dir with <ebible_id>/<ebible_id>_usfx.xml and copr.htm")
    p.add_argument("--curated", required=True)
    p.add_argument("--hours", required=True)
    p.add_argument("--out-dir", required=True, help="Resources directory")
    p.add_argument("--download", action="store_true", help="download missing USFX zips from eBible.org")

    p = sub.add_parser("verify", help="print statistics about generated resources")
    p.add_argument("--resources", required=True)
    p.add_argument("--curated", help="curated_verses.json to check curated.json against")
    p.add_argument("--hours", help="hour_verses.json (human-readable) to check against")

    args = ap.parse_args(argv)

    if args.cmd == "build":
        curated = load_curated(args.curated)
        hours = load_hours(args.hours)
        build_translation(args.source, args.copr, args.id, args.name, args.abbreviation,
                          args.url, curated, hours, args.out)
        return 0

    if args.cmd == "lists":
        curated = load_curated(args.curated)
        hours = load_hours(args.hours)
        docs = [read_json(p) for p in args.translations]
        _verses, chapters, _bridged = parse_usfx(args.books_source)
        build_lists(docs, curated, hours, chapters, args.out_dir)
        return 0

    if args.cmd == "all":
        curated = load_curated(args.curated)
        hours = load_hours(args.hours)
        docs = []
        chapters = None
        for t in TRANSLATIONS:
            d = os.path.join(args.data_dir, t["ebible_id"])
            source = os.path.join(d, t["ebible_id"] + "_usfx.xml")
            copr = os.path.join(d, "copr.htm")
            if not (os.path.isfile(source) and os.path.isfile(copr)):
                if args.download:
                    download_usfx(t["ebible_id"], args.data_dir)
                else:
                    missing_path = source if not os.path.isfile(source) else copr
                    sys.exit("error: missing %s; download %s into %s (or pass --download)" % (
                        missing_path, ebible_zip_url(t["ebible_id"]), d))
            out = os.path.join(args.out_dir, "Translations", t["id"] + ".json")
            doc, ch = build_translation(source, copr, t["id"], t["name"], t["abbreviation"],
                                        ebible_zip_url(t["ebible_id"]), curated, hours, out)
            docs.append(doc)
            if t["id"] == "kjv":
                chapters = ch
        if chapters is None:
            chapters = ch
        build_lists(docs, curated, hours, chapters, args.out_dir)
        return 0

    if args.cmd == "verify":
        return 0 if verify(args.resources, args.curated, args.hours) else 1

    return 2


if __name__ == "__main__":
    sys.exit(main())
