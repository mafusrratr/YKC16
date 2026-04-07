# SQLite source in `extraLib/sql`

This directory now contains the official SQLite 3.45.1 amalgamation source.

Source package:
- `https://sqlite.org/2024/sqlite-amalgamation-3450100.zip`

Main files:
- `sqlite3.c`
- `sqlite3.h`
- `sqlite3ext.h`
- `shell.c`

Notes:
- The existing common `extraLib/libsqlite3.so` and `extraLib/imx6ul/libsqlite3.so` are also SQLite 3.45.1.
- The new `Makefile` infers platform output from the active toolchain and writes to:
  - `extraLib/nuc980/libsqlite3.so`
  - `extraLib/imx6ul/libsqlite3.so`
  - `extraLib/jzq/libsqlite3.so`
  - or `extraLib/libsqlite3.so` when no platform is inferred.

Examples:
- Local/native build: `make -f Makefile`
- Remote NUC980 build: `build nuc980 Makefile`
- Inspect build variables: `make -f Makefile info`
