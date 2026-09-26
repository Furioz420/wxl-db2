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
- **Input validation**: malformed or missing tables are reported as load failures. Check startup logs and consumer behavior with the intended data set.

## Requirements

WarcraftXL on a 3.3.5a client, build 12340.

## Building

This source snapshot follows WXL integration commit `db5f1b5`. Build it with the matching core/API revision and the intended DB2 data; the moving upstream `v1.1` branch is not an exact compatibility pin. The imported `.github/workflows/release.yml` publishes from `main`, so keep this PR in draft until its core pin, Hub package contents, and runtime data are validated.

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
