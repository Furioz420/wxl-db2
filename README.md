# wxl-db2

[Build compatibility and release gate](BUILDING.md)

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

Build this source with the exact compatible core revision recorded in [BUILDING.md](BUILDING.md) and the intended DB2 data. Pull requests and `main` run a Win32 build; only an explicit version tag can publish a release. Runtime data and Hub package contents still need validation before tagging.

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

## Integration and release checks

Build `wxl-db2` as a Win32 Release target against an exact compatible core/API commit. The current repository workflow stages a DLL only; `wxl-db2.cfg`, modern DB2 tables, FileDataID mappings, and localized CSV overlays are separate inputs and are not included in that workflow's DLL-only artifact. Match data schemas and layout hashes to the intended client build. Do not copy an entire retail database or overwrite the Wrath client data to satisfy a missing table.

Verify startup logs, one WDC5/compact lookup, one FileDataID path, and the enabled retail item, model, spell, or lighting consumer that depends on the data. Compare a missing-table path as well as a populated path. The pinned clean-checkout target compiles; separately packaged DLL and client acceptance remain open.

## Credits

The WXL core ABI and original module interfaces come from WarcraftXL contributors. The local v1.1 integration commits in this snapshot are attributed to Furioz in the integration history. Preserve source-file notices and the GPL-3.0-or-later `LICENSE` when redistributing source or binaries.
