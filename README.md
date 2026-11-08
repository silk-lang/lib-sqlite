# SQLite for Silk

SQLite for Silk. The package compiles SQLite 3.54.1 from its amalgamation (fetched from sqlite.org
and pinned by SHA-256 in `silk.toml`) into every program that depends on it, so nothing has to be
installed on the machine.

Extracted from [`silk-lang/nonstd` at `7d6c3693a9ad1fec24127985626ab4224f9f1995`](https://github.com/silk-lang/nonstd/tree/7d6c3693a9ad1fec24127985626ab4224f9f1995/db/sqlite).
The package name remains `db-sqlite`, with the same `sqlite` and `sys` exports.

## Install

Add this repository as a dependency in your `silk.toml`, pinning `rev` to the commit you use:

```toml
[dependencies]
sqlite = { git = "https://github.com/silk-lang/lib-sqlite", rev = "<commit>", package = "db-sqlite" }
```

When migrating from `nonstd`, replace the Git URL and revision and remove `path = "db/sqlite"`.
The imports and API stay the same. Run `silk use` to install the compiler pinned in this package's
manifest when working on the library.

```silk
import sqlite.sqlite { Connection, SqliteError, TransactionKind }
import silk.mem.allocator { Allocator, OutOfMemoryError }

effect fn demo() -> () ! SqliteError | OutOfMemoryError ? &mut Allocator {
  let db = run Connection.open("notes.db")
  run db.execute("PRAGMA journal_mode = WAL; CREATE TABLE IF NOT EXISTS notes (id INTEGER PRIMARY KEY, body BLOB)")

  let mut transaction = run db.begin(TransactionKind.Immediate)
  let mut insert = run db.prepare("INSERT INTO notes (body) VALUES (?)")
  run insert.bindBlob(1, b"hello")
  drop run insert.execute()
  run transaction.commit()

  let mut query = run db.prepare("SELECT id, body FROM notes WHERE id > ?")
  run query.bindInt(1, 0)
  while run query.step() {
    let id = query.columnInt(0)
    let body = query.columnBlob(1) // borrowed until the next step
  }
}
```

## API

- `Connection.open(path)`, `openWith(path, OpenMode)` and `openMemory()`. Dropping a connection
  closes it. Statements prepared on it stay valid after that (SQLite closes the database once the
  last statement is gone), so a struct can keep a connection and its prepared statements together.
- `Connection.execute(sql)` runs a script of statements that return no rows (schemas, migrations,
  pragmas). `Connection.prepare(sql)` compiles exactly one statement, prepared for reuse.
- `Statement`: `bindInt`, `bindFloat`, `bindText`, `bindBlob`, `bindNull`, `bindZeroBlob`,
  `bindValue`, `parameterIndex(":name")`, `step`, `execute` (run to the end and reset, returns the
  changed rows), `reset`, `clearBindings`, and the column readers `columnInt`, `columnFloat`,
  `columnText`, `columnTextBytes`, `columnBlob`, `columnValue`, `columnType`, `columnIsNull` and
  `columnName`. Parameters count from 1, columns from 0. Text and blob reads borrow the statement
  and need no copy.
- `Connection.begin(TransactionKind)` returns a `Transaction` guard: `commit()` or `rollback()`,
  and dropping it uncommitted rolls back, so a failure in the middle leaves the database unchanged.
- `SqliteError` has the extended result `code` (such as 2067 for `SQLITE_CONSTRAINT_UNIQUE`),
  SQLite's `message`, and `primary()`, `isBusy()` and `isConstraint()`. The primary codes are
  constants (`ERROR`, `BUSY`, `CONSTRAINT`, ...).
- `changes`, `totalChanges`, `lastInsertRowId`, `isAutocommit`, `busyTimeout`, `version` and
  `versionNumber`.

The `sys` module has the raw C functions for anything the safe API does not cover yet: user
functions and window functions, the authorizer, sessions and changesets, backups, serialization,
column metadata, limits and extension loading. Calls that can run a registered callback are marked
`@invokesDeferredCallbacks`, so Silk callbacks can be registered with them.

## Build options

SQLite is built thread-safe (`SQLITE_THREADSAFE=1`) with FTS5, R*Tree and the math functions, plus
the JSON functions that are always on. It also has the session extension (changesets and
patchsets, with the pre-update hook it needs), column metadata (`sqlite3_column_table_name` and the
rest) and `sqlite3_load_extension`; loading through the SQL `load_extension()` function stays off
until a connection turns it on with `SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION`. Double-quoted string
literals are off (`SQLITE_DQS=0`), as SQLite recommends, so `"name"` is always an identifier.
Shared cache is left out.

## Platforms

macOS, Linux and Windows. macOS and Linux use Silk's native manifest support to fetch, verify,
compile and cache SQLite automatically. Use LLVM Clang 22; on macOS, set `SDKROOT` to
`$(xcrun --sdk macosx --show-sdk-path)` if needed. The browser is not supported.

On Windows, run the following from an x64 Visual Studio developer PowerShell 7.2+ with LLVM 22 on
`PATH` (or `SILK_CLANG` pointing to `clang.exe`):

```powershell
./scripts/prepare-windows.ps1
silk test --root src/tests.silk
```

SQLite needs Windows SDK headers and CRT import libraries that Silk's native compiler invocation
does not discover. The PowerShell preparation script verifies the pinned source archive, builds
SQLite and the shim into `.cache/sqlite/windows/silksqlite.lib`, and copies the required import
libraries. It sets only the `sqlite` archive override in `$SILK_HOME/native-overrides.toml`
(`~/.silk` by default), preserving other overrides. Repeat preparation after changing the SQLite
pin, native flags, shim, or LLVM/SDK installation. Windows consumers run the same preparation from
a checkout of this repository before building their own package. No Python tooling is required.

## Tests

```sh
silk use
silk format --check
silk check
silk test --root src/tests.silk
silk build
silk doc -o build/doc.json
```

To work offline, point the `sqlite` archive at a directory holding `sqlite3.c` and `sqlite3.h` in
`$SILK_HOME/native-overrides.toml`:

```toml
[native.override.sqlite]
path = "/path/to/sqlite-amalgamation"
```
