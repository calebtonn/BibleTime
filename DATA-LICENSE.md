# Scripture data: sources and licensing

The repository's MIT license covers the **code** of BibleTime only (the Swift
sources, the Python build tool and the project files). It does **not** cover
the Scripture text bundled in `Resources/Translations/*.json`. That text comes
from the public-domain editions published by [eBible.org](https://ebible.org)
and is distributed here under the terms quoted below, which are reproduced
from the `copr.htm` file shipped inside each eBible download.

Both translations are in the Public Domain. `Tools/build_verses.py`
regenerates the JSON files from the USFX downloads; it keeps only the verse
text (footnotes, cross-references, section headings, Psalm superscriptions,
Strong's numbers, pilcrows and typographic square brackets are removed, and
whitespace is collapsed). Only chapters 1-23 / verses 1-60 plus a short list
of curated references are included.

Downloaded: 5 November 2026 by the "Verse data" GitHub workflow (`.github/workflows/data.yml`), whose log
records the hashes and `copr.htm` text below.

## King James Version (KJV)

* eBible edition: **King James (Authorized) Version** (eBible id `eng-kjv2006`)
* Download: <https://ebible.org/Scriptures/eng-kjv2006_usfx.zip>
* Edition page: <https://ebible.org/eng-kjv2006>
* eBible stamp: "HTML generated with Haiola by eBible.org 4 Nov 2026 from source files dated 3 Nov 2026"
* sha256 `eng-kjv2006_usfx.zip`: `7ac7a2de6f304fede9d37cc4fc82bcc09916442b5035c9b51a97f7f4339e78fe`
* sha256 `eng-kjv2006_usfx.xml` (the file parsed): `e2c45a1f6afbdc3463fd7aa51fcb138e79a4e60f9132884d3b9b9fb22012f4d8`

From `copr.htm`:

> The King James Version or Authorized Version of the Holy Bible, using the
> standardized text of 1769, protocanon only, with Strong's numbers added.
>
> Public Domain
>
> Letters patent issued by King James with no expiration date means that to
> print this translation in the United Kingdom or import printed copies into
> the UK, you need permission. Currently, the Cambridge University Press, the
> Oxford University Press, and Collins have the exclusive right to print this
> Bible translation in the UK. This royal decree has no effect outside of the
> UK, where this work is firmly in the Public Domain. Please see
> http://www.cambridge.org/about-us/who-we-are/queens-printers-patent and
> https://en.wikipedia.org/wiki/King_James_Version#Copyright_status for more
> information.
>
> This free text of the King James Version of the Holy Bible is brought to you
> courtesy of the Crosswire Bible Society and eBible.org.
>
> You may copy the King James Version of the Holy Bible freely.

## Darby Translation (DBY)

* eBible edition: **Darby Translation**, "The Holy Scriptures, a New Translation from the Original Languages by
  J. N. Darby" (complete edition 1890; eBible id `engDBY`)
* Download: <https://ebible.org/Scriptures/engDBY_usfx.zip>
* Edition page: <https://ebible.org/engDBY>
* eBible stamp: "HTML generated with Haiola by eBible.org 30 Oct 2026 from source files dated 16 Nov 2019"
* sha256 `engDBY_usfx.zip`: `498058e7250d204aa14292625dcdc0f251d6e4ac14daa5ca8098e37689034b27`
* sha256 `engDBY_usfx.xml` (the file parsed): `9993edecce9b6a9d624235e2ae35510c1c5642b6a69035b75085986ba190a2f1`

From `copr.htm`:

> The Holy Scriptures, a New Translation from the Original Languages by J. N. Darby
>
> Public Domain
>
> Translation by: J. N. Darby

## Not bundled: NET Bible

The NET Bible® is copyrighted by Biblical Studies Press, L.L.C. It is free to read and to quote within the
publisher's limits, but distributing the full text electronically requires the publisher's written permission,
so BibleTime does not ship it. Someone with a licensed copy can convert and import it for personal use (see the
README).

## Other generated files

`Resources/curated.json`, `Resources/hour_verses.json` and
`Resources/books.json` contain only Scripture references (book codes,
chapter and verse numbers) and book names; they are part of the project and
covered by the MIT license.
