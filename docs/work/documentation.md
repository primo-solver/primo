# Documentation replacement

**Status: replacement prepared; consolidated reader acceptance pending.**

The current manual is in `docs/`; the historical manual remains editable in
`docs-history/`. `make docs` builds both incrementally and assembles one site.
`make docs-full` rebuilds both strictly. `make docs-serve` serves the assembled
site locally. The old draft commands have been removed.

The compatibility manifest preserves 1,306 original page/fragment targets.
Retired pages route to the historical manual. Current pages retain their own
anchors and route obsolete fragments to the archive; ordinary links remain
available without JavaScript. The assembler refuses missing archived targets.

The [closing review](../review.md) separates technical verification from reader
acceptance. The [publication checklist](publication.md) owns hosting, rights,
CI and launch qualification. No website is published by these local commands.
