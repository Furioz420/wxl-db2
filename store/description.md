# DB2 Tables

> Modern content stopped naming most of its assets by path. A texture or a model is a number, and a
> database table somewhere maps that number to the file it actually is. The stock client has never heard
> of that table, or the format it ships in.

This module reads that table directly, through the client's own archive set. No conversion step, no
intermediate file, nothing written to disk. It doesn't render anything and it doesn't load any single
asset itself; it exists so every other module can turn a bare number into a real path.

---

## Two table families, one reader

The modern database format went through several revisions. This module reads all of them, split by how
much a caller needs to know about a table up front:

- **WDC1 / WDC2 / WDC3** decode into a flat, fixed layout with no schema needed ahead of time. This is
  what the game's own texture and model path tables ship as, so it's what the FileDataID resolver below
  is built on.
- **WDC5** is read declaratively: a caller describes a table (its name, its fields, how it relates to
  other tables) and gets typed rows back by field name instead of a fragile numeric position. This is what
  a script reaches for to read a table this module has no built-in knowledge of.

Both paths share the same bitpacked/palletized/common-data record model modern tables actually use;
nothing here assumes a table is stored the simple way.

## What it publishes

- **`wxl.fdid`** resolves a FileDataID to a client path, backed by the game's own texture and model path
  tables. Every extension that loads a modern asset by number reads through this.
- **`wxl.db2`** is the declarative table service: hand it a definition, get back rows addressable by field
  name. Meant for scripts and extensions that need a table this module doesn't already resolve on its own.
- **`wxl.light`** answers "what's the light here, right now": the winning light point for a map position,
  its parameter set walked and blended across the day, ready to hand a consumer every colour and fog
  figure it needs. Built entirely on `wxl.db2` itself, as the first declared schema of the kind other
  extensions can add under their own name.

### Lighting, worked out from the tables alone

The tables describe the lighting model themselves, not just its numbers: a light is a point in the world
with a falloff, and it names a parameter set. That set is unrolled over the day in half-minute stamps,
every colour and fog figure the game needs, keyed by set and time. Evaluating "here, now" walks that
day-curve and interpolates between the two surrounding stamps, then crossfades the nearest point light
over the map's own global one across its falloff ring, exactly like walking across a zone boundary. The
result is cached once a frame rather than recomputed per consumer, so grading, water ambient and the sky
never disagree about what frame they're answering for.

## Safety

A table that fails to decode, a definition that doesn't match what's on disk, a missing file: all of it is
a logged failure, not a crash. A resolver that finds nothing returns nothing, and every caller already
treats that as "asset not found" rather than a fault.

## Requirements

WarcraftXL on a 3.3.5a client, build **12340**.
