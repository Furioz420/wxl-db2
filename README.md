# wxl-db2

**Reads modern client database tables, and resolves assets by FileDataID.**

A [WarcraftXL](https://github.com/WarcraftXL/wxl-core) extension. Modern content stopped naming most
assets by path a long time ago; a texture or a model is just a number (a FileDataID) that a database
table maps to a real file. The stock client has no such table and no reader for the modern database
format. This module reads it directly, through the client's own archive set, with no conversion step and
no intermediate file.

See [`store/description.md`](store/description.md) for the full write-up (the two table formats it reads,
and what it publishes for other extensions to use).

## Highlights

- **Direct native read**: WDC1 through WDC5, the full modern database family, decoded straight from the
  client's own archives.
- **FileDataID resolution**: `wxl.fdid`, published for every other extension to turn an id into a client
  path, backed by the game's own texture/model path tables.
- **Declarative table loading**: `wxl.db2`, published so a script can describe a table (name, fields,
  relations) and get typed rows back, without hand-rolling a decoder per table.
- **Retail lighting tables**: `wxl.light`, a schema built on `wxl.db2` itself that answers "what's the
  light here, right now" for any map position and time of day, day-curve blending included.
- **Crash-safe**: malformed or missing tables are a logged failure, never a crash; nothing downstream ever
  sees a null it didn't ask for.

## Requirements

WarcraftXL on a 3.3.5a client, build 12340.

## Building

This extension builds against [wxl-core](https://github.com/WarcraftXL/wxl-core) (branch `v1.1`), which
auto-discovers any folder dropped into its `extensions/` directory, so there's no project file of its own
needed here. See `.github/workflows/release.yml` for the exact steps; every push to `main` builds
the flat `wxl-db2.dll` + `wxl-db2.cfg` Hub package and publishes it as a release. The configuration
file is intentionally present as the stable Hub asset contract; the module currently has no
per-service runtime settings.

## Project layout

```
src/
├── ExtensionApi.hpp/.cpp, Module.cpp   entry points, publishes wxl.db2, wxl.fdid and wxl.light
├── decode/                             the raw table decoders: WDC1/2/3 and WDC5
├── api/                                the declarative table API and the FileDataID resolver
└── schemas/                            declared tables built on top of wxl.db2 itself (e.g. lighting)
```

## License

GPL-3.0-or-later. See the license header in every source file.
