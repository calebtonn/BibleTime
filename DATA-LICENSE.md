# Scripture data: sources and licensing

The repository's MIT license covers the **code** of BibleTime only (the Swift
sources, the Python build tool and the project files). It does **not** cover
the Scripture text bundled in `Resources/Translations/*.json`. That text comes
from the public-domain editions published by [eBible.org](https://ebible.org)
and is distributed here under the terms quoted below, which are reproduced
from the `copr.htm` file shipped inside each eBible download.

All three translations are in the Public Domain. `Tools/build_verses.py`
regenerates the JSON files from the USFX downloads; it keeps only the verse
text (footnotes, cross-references, section headings, Psalm superscriptions,
Strong's numbers, pilcrows and typographic square brackets are removed, and
whitespace is collapsed). Only chapters 1-23 / verses 1-60 plus a short list
of curated references are included.

Downloaded: 3 November 2026 (data as published by eBible.org on 30 October 2026).

## King James Version (KJV)

* eBible edition: **King James (Authorized) Version** (eBible id `eng-kjv2006`)
* Download: <https://ebible.org/Scriptures/eng-kjv2006_usfx.zip>
* Edition page: <https://ebible.org/eng-kjv2006>
* eBible stamp: "HTML generated with Haiola by eBible.org 30 Oct 2026 from source files dated 26 Sep 2026"
* sha256 `eng-kjv2006_usfx.zip`: `007cd93edfcbedb22b571882ec728bef5650638a4045fccda2e8e1a70e944690`
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

## World English Bible (WEB)

* eBible edition: **World English Bible** (eBible id `engwebp`; the
  66-book edition that renders the divine name as "LORD" — not the "Classic"
  `eng-web` edition with the Apocrypha and "Yahweh")
* Download: <https://ebible.org/Scriptures/engwebp_usfx.zip>
* Edition page: <https://ebible.org/engwebp>
* eBible stamp: "2020 stable text edition" / "HTML generated with Haiola by eBible.org 30 Oct 2026 from source files dated 30 Oct 2026"
* sha256 `engwebp_usfx.zip`: `66f99b22a380d8ef8dbd9d401806fc02be3650e5fe3dbb0cec4d9693b53e1e44`
* sha256 `engwebp_usfx.xml` (the file parsed): `c43cb484e4b3f36e6cd977d8bdcf8a51425fd9bf1e9412d9e8b8b240f2932bf0`

From `copr.htm`:

> The World English Bible is in the Public Domain. That means that it is not
> copyrighted. However, "World English Bible" is a Trademark of eBible.org.
>
> You may copy, publish, proclaim, distribute, redistribute, sell, give away,
> quote, memorize, read publicly, broadcast, transmit, share, back up, post on
> the Internet, print, reproduce, preach, teach from, and use the World English
> Bible as much as you want, and others may also do so. All we ask is that if
> you CHANGE the actual text of the World English Bible in any way, you not
> call the result the World English Bible any more. This is to avoid
> confusion, not to limit your freedom. The Holy Bible is God's Word. It
> belongs to God. He gave it to us freely, and we who have worked on this
> translation freely give it to you by dedicating it to the Public Domain.
>
> The master copy of this Bible translation is posted on
> https://eBible.org/web/ and at https://WorldEnglish.Bible.
>
> This is the updated World English Bible with the 66-book protocanon only.

BibleTime does not change the wording of the World English Bible text. The
build tool only omits apparatus (footnotes, cross-references, headings, Psalm
superscriptions) and normalises whitespace, so the verses are shown under the
name "World English Bible". If you modify the verse text itself, the trademark
clause above requires you to stop calling the result the World English Bible.

## American Standard Version (ASV)

* eBible edition: **American Standard Version (1901)** (eBible id `eng-asv`)
* Download: <https://ebible.org/Scriptures/eng-asv_usfx.zip>
* Edition page: <https://ebible.org/eng-asv>
* eBible stamp: "HTML generated with Haiola by eBible.org 30 Oct 2026 from source files dated 30 Oct 2026"
* sha256 `eng-asv_usfx.zip`: `ef54be42be5eba7a0a40a132007df00d0e1fd56fca3a7d172355a97d1b5ad7bd`
* sha256 `eng-asv_usfx.xml` (the file parsed): `31ba1861a6a9de52b8a862d675e21e4ec4e7bc74a75c5e61ec62f95612645633`

From `copr.htm`:

> The American Standard Version of the Holy Bible, first published in 1901.
>
> Public Domain
>
> This public domain Bible translation is brought to you courtesy of eBible.org.
>
> The American Standard Version of the Holy Bible is in the Public Domain.
> Copy freely.

## Other generated files

`Resources/curated.json`, `Resources/hour_verses.json` and
`Resources/books.json` contain only Scripture references (book codes,
chapter and verse numbers) and book names; they are part of the project and
covered by the MIT license.
