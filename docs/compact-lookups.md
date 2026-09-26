# Compact asset lookups and Dalaran memory pressure

## Candidate, 22 September 2026

The last failing Dalaran run reached 3797.8 MiB committed and 243.8 MiB
reserved, with 54.3 MiB free and a largest free region of 4.2 MiB, two seconds
before the crash. The executable is already x86 large-address-aware on 64-bit
Windows. Farclip remains 1277; the user wants an eventual 2500-3000 view
distance, so lowering farclip is not the chosen fix.

Auditing 317 loose texture files named in the trace found 309 at 512x256,
six at 256x256, one at 128x128, and one at 1024x512. None of these files
escaped the existing 512-edge limit through a missing target mip. This does
not cover every texture used by the client, but gives no reason to revert
the accepted retail NPC bakes.

## Change and ownership

* DB2File replaces per-ID unordered-map nodes with sorted ID/row pairs.
  Duplicate IDs still resolve to the last row. Min/max IDs and row order
  remain unchanged. Lookup is binary search; records and strings stay owned
  by the table.
* FdidResolver indexes model stems as views into its process-lifetime model
  table, avoiding a second heap string for roughly 588,000 paths. Comparison
  still folds case and slashes and strips the final extension. Stable sorting
  preserves first spelling wins for equivalent paths. Do not unload or mutate
  the model table while these views exist.
* Material lookup uses sorted triples with stable source order, preserving
  requested usage, then usage 2, then first-row fallback. The decoded
  TextureFileData table is now local to initialization and is released after
  the independent triples have been built.
* Resolved-path cache lifetimes and published C-string pointers are unchanged.
  No live render resources are force-freed, and no assets or graphics settings
  are changed. WXL_M2_MEMORY_TRACE remains enabled for runtime comparison.

## Validation

The standalone x86 test compared all installed ModelFilePath.db2 (588723 rows),
TextureFilePath.db2 (844199 rows), and TextureFileData.db2 (214433 rows) lookups.
It also checked case/slash/extension aliases, duplicate ID and stem precedence,
missing keys, empty indexes, signed IDs, and randomized material fallback cases.
All passed. The wxl-db2 Release build passed.

Requested live C++ allocation bytes in the old versus compact indexes:

| Structure | Old bytes | Compact bytes | Reduction |
|---|---:|---:|---:|
| Model IDs | 17808227 | 4709784 | 13098443 |
| Model stems | 76905016 | 7064676 | 69840340 |
| Texture IDs | 21895843 | 6753592 | 15142251 |
| Material table and index | 21118645 | 2573196 | 18545449 |

Total measured reduction: 116626483 bytes, approximately 111.2 MiB. This is
requested C++ storage, not a claim that Windows immediately releases exactly
that much committed address space. Allocator metadata, fragmentation, other
tables, and temporary decode allocations are outside this measurement.
Runtime survival is not yet verified; further live-content pressure may remain.

## Reproducing the comparison

From an x86 MSVC developer prompt, build tests/compact_lookup_test.cpp with
/std:c++20 /EHsc /O2, include src/decode, and link src/decode/Db2Decode.cpp,
src/decode/Wdc5.cpp and src/decode/Wdc5Table.cpp. Do not define NDEBUG: the
comparison uses assertions. Run without arguments for synthetic checks, or
pass the DBFilesClient directory as the first argument for the full-table
comparison. Do not link the test's allocation counter into the extension.

## Runtime acceptance

Use a fresh client launch, enter Dalaran, idle as before, and follow the same
route for at least as long as the failed run. Watch for missing models,
textures, equipment or spell effects as well as crashes. Close the client
afterward so the log can be preserved. Confirm the db2-fdid-compact-v1 marker
and compare periodic-v1 memory samples against the original failing run.
Do not claim a resolved crash based solely on a successful build or index test.

The deployment is DLL-only. To roll back, close WoW and restore the backed-up
wxl-db2.dll. Leave the current configs and accepted NPC/shadow DLLs untouched.

## User acceptance

On 22 September 2026 the user reported that the candidate felt better and
that no crashes occurred, and requested committing it. This is acceptance
of the tested route/session, not proof against every future memory-pressure
scenario or validation at the proposed 2500-3000 farclip.
