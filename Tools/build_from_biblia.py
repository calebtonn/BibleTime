#!/usr/bin/env python3
"""
build_from_biblia.py - build a BibleTime translation file from the Biblia API.

Faithlife's Biblia API (https://bibliaapi.com) serves the verse text of a few
dozen Bibles, including some that are not public domain (for example the Legacy
Standard Bible, id `lgcystndrdbblsb`).  This script fetches every chapter the
clock can reach and writes a translation JSON in the same format as
build_verses.py, ready for "Import..." in the BibleTime options or for
~/Library/Application Support/BibleTime/Translations/.

    export BIBLIA_API_KEY=...        # your own key from bibliaapi.com
    python3 -I Tools/build_from_biblia.py --bible lgcystndrdbblsb --id lsb \
        --name "Legacy Standard Bible" --abbreviation LSB \
        --publisher "The Lockman Foundation" \
        --license "Copyright 2021 The Lockman Foundation. Personal use only." \
        --out ~/Desktop/lsb.json

The text you download is governed by the publisher's terms and by Biblia's
terms of use.  Keep the result for your own use; do not commit or share it.

Raw responses are cached (default ~/Library/Caches/BibleTime/biblia/<bible>/)
so an interrupted run resumes where it stopped and re-runs do not hit the API.

Python 3.9+, standard library only.  Run with `python3 -I`.
"""

import argparse
import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

# build_verses.py lives next to this file; -I keeps the script directory off sys.path on purpose,
# so add exactly that directory back.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_verses  # noqa: E402

API = "https://api.biblia.com/v1/bible/content/{bible}.txt"
# One verse per line, no headings, no red letters, no footnotes: "<chapter>:<verse>\t<text>".
PARAMS = {
    "formatting": "none",
    "redLetter": "false",
    "footnotes": "false",
    "paragraphs": "false",
    "fullText": "false",
    "eachVerse": "[ChapterNum]:[VerseNum]\t[VerseText]\n",
}
# The API does not honour "\n" in the template, so a chapter arrives as one line:
# "3:1<TAB>text 3:2<TAB>text ...". Psalm superscriptions come as "23:<TAB>A Psalm of David."
# with no verse number. Verses are therefore split on the markers, not on lines.
MARKER = re.compile(r"(?:^|\s)(\d+):(\d*)(?:-(\d+))?\t")


def log(msg):
    print(msg, file=sys.stderr)


def load_books(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def chapters_to_fetch(books, whitelist):
    """(code, name, chapter) for every chapter the clock can reach, plus whitelisted ones."""
    wanted = set()
    for code, chapter, _verse in whitelist:
        wanted.add((code, chapter))
    out = []
    for book in books:
        for chapter in range(1, book["chapters"] + 1):
            if chapter <= build_verses.MAX_CHAPTER or (book["code"], chapter) in wanted:
                out.append((book["code"], book["name"], chapter))
    return out


def fetch(bible, passage, key, cache_dir, delay, retries=4):
    cache = os.path.join(cache_dir, re.sub(r"[^A-Za-z0-9]+", "_", passage) + ".txt")
    if os.path.exists(cache):
        with open(cache, encoding="utf-8") as f:
            return f.read(), True
    query = dict(PARAMS)
    query["passage"] = passage
    query["key"] = key
    url = API.format(bible=urllib.parse.quote(bible)) + "?" + urllib.parse.urlencode(query)
    last = None
    for attempt in range(retries):
        try:
            with urllib.request.urlopen(url, timeout=60) as response:
                text = response.read().decode("utf-8")
            break
        except urllib.error.HTTPError as e:
            if e.code in (401, 403):
                sys.exit("error: the API rejected the key (%s). Set BIBLIA_API_KEY or pass --key." % e.code)
            if e.code == 404:
                sys.exit("error: unknown bible id %r or passage %r (HTTP 404)" % (bible, passage))
            last = e
            time.sleep(2 ** attempt * 2)
        except (urllib.error.URLError, TimeoutError) as e:
            last = e
            time.sleep(2 ** attempt * 2)
    else:
        sys.exit("error: giving up on %s after %d attempts: %s" % (passage, retries, last))
    os.makedirs(cache_dir, exist_ok=True)
    with open(cache, "w", encoding="utf-8") as f:
        f.write(text)
    time.sleep(delay)
    return text, False


def balance_quotes(text):
    """Translations in the NASB tradition open every verse of continuing speech with a quotation mark that
    closes verses later. Shown alone, such a verse would carry a stray mark, so drop an unmatched leading
    opening quote or trailing closing quote (matched pairs inside the verse are kept)."""
    if text.startswith("\u201c") and "\u201d" not in text:
        text = text[1:].lstrip()
    if text.endswith("\u201d") and "\u201c" not in text:
        text = text[:-1].rstrip()
    return text


def parse_chapter(text, code, chapter):
    """Return {(code, chapter, verse): text} for one chapter's response."""
    verses = {}
    markers = list(MARKER.finditer(text))
    for i, m in enumerate(markers):
        c, v = int(m.group(1)), m.group(2)
        if c != chapter or not v:
            continue  # another chapter's spill-over, or a superscription/heading without a verse number
        start = m.end()
        end = markers[i + 1].start() if i + 1 < len(markers) else len(text)
        body = text[start:end].replace("\u00b6", " ")
        body = re.sub(r"\s+", " ", body).strip()
        body = balance_quotes(body)
        if body:
            # A bridged verse ("3:5-6") is stored under the first number, like build_verses.py does.
            verses[(code, chapter, int(v))] = body
    return verses


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bible", required=True, help="Biblia bible id, e.g. lgcystndrdbblsb (see the Logos connector's list)")
    ap.add_argument("--id", required=True, help="translation id for BibleTime, e.g. lsb")
    ap.add_argument("--name", required=True)
    ap.add_argument("--abbreviation", required=True)
    ap.add_argument("--publisher", default="")
    ap.add_argument("--license", default="Copyrighted; personal use only",
                    help="copyright notice recorded in the file")
    ap.add_argument("--key", help="Biblia API key (default: $BIBLIA_API_KEY)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--books", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Resources", "books.json"))
    ap.add_argument("--curated", help="default: Tools/lists/curated_verses.json")
    ap.add_argument("--hours", help="default: Tools/lists/hour_verses.json")
    ap.add_argument("--cache-dir", help="default: ~/Library/Caches/BibleTime/biblia/<bible>")
    ap.add_argument("--delay", type=float, default=0.5, help="seconds to wait between requests (default 0.5)")
    ap.add_argument("--only", help='comma-separated passages for a quick test, e.g. "John 3,Psalm 23"')
    args = ap.parse_args()

    key = args.key or os.environ.get("BIBLIA_API_KEY")
    if not key:
        sys.exit("error: no API key; set BIBLIA_API_KEY or pass --key")
    if not re.match(r"^[a-z0-9][a-z0-9_-]*$", args.id):
        sys.exit("error: --id must be lowercase letters, digits, - or _")
    cache_dir = args.cache_dir or os.path.expanduser("~/Library/Caches/BibleTime/biblia/%s" % args.bible)

    books = load_books(args.books)
    by_name = {b["name"].lower(): b for b in books}
    whitelist = build_verses.whitelist_from(
        build_verses.load_curated(args.curated or build_verses.default_list("curated_verses.json")),
        build_verses.load_hours(args.hours or build_verses.default_list("hour_verses.json")))

    if args.only:
        targets = []
        for item in args.only.split(","):
            item = item.strip()
            m = re.match(r"^(.*?)\s+(\d+)$", item)
            if not m or m.group(1).lower() not in by_name:
                sys.exit("error: --only expects entries like 'John 3' using the names in books.json (got %r)" % item)
            book = by_name[m.group(1).lower()]
            targets.append((book["code"], book["name"], int(m.group(2))))
    else:
        targets = chapters_to_fetch(books, whitelist)

    log("[%s] fetching %d chapters from Biblia (%s)" % (args.id, len(targets), args.bible))
    verses = {}
    cached = 0
    for i, (code, name, chapter) in enumerate(targets, 1):
        passage = "%s %d" % (name, chapter)
        text, from_cache = fetch(args.bible, passage, key, cache_dir, args.delay)
        cached += from_cache
        got = parse_chapter(text, code, chapter)
        if not got:
            log("[%s] warning: no verses parsed for %s" % (args.id, passage))
        verses.update(got)
        if i % 50 == 0 or i == len(targets):
            log("[%s] %d/%d chapters, %d verses so far (%d from cache)" % (args.id, i, len(targets), len(verses), cached))

    rows = []
    for (code, chapter, verse), text in verses.items():
        key_ = (code, chapter, verse)
        if not ((chapter <= build_verses.MAX_CHAPTER and verse <= build_verses.MAX_VERSE) or key_ in whitelist):
            continue
        if not text:
            continue
        next_text = verses.get((code, chapter, verse + 1))
        rows.append([code, chapter, verse, text,
                     build_verses.compute_flags(code, chapter, verse, text, whitelist, next_text)])
    rows.sort(key=build_verses.verse_sort_key)
    missing = sorted((k for k in whitelist if k not in verses or not verses[k]), key=build_verses.verse_sort_key)
    for k in missing:
        log("[%s] note: whitelisted reference not present: %s %d:%d" % (args.id, k[0], k[1], k[2]))

    doc = {
        "id": args.id,
        "name": args.name,
        "abbreviation": args.abbreviation,
        "language": "en",
        "source": {
            "publisher": args.publisher,
            "edition": args.name,
            "url": "https://bibliaapi.com (bible id %s)" % args.bible,
            "sourceFilesDated": "",
            "generated": time.strftime("%Y-%m-%d"),
            "sha256": "",
            "license": args.license,
        },
        "verses": rows,
    }
    out = os.path.expanduser(args.out)
    build_verses.write_json(out, doc)
    log("[%s] wrote %s (%d verses)" % (args.id, out, len(rows)))
    if not args.only and len(rows) < 5000:
        log("[%s] warning: fewer than 5000 verses; BibleTime will refuse to import this file" % args.id)


if __name__ == "__main__":
    main()
