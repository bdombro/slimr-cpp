# slimr::sqlite (`packages/sqlite`)

Fast, RAII SQLite 3 + FTS5 wrapper for C++26 — built directly on macOS/system `-lsqlite3` with WAL defaults, a 256-statement LRU cache, `PRAGMA user_version` migrations, nested `SAVEPOINT` transactions, and isolated read-only cursors.

Borrowed directly from the design of [`@slimr/sqlite`](https://www.npmjs.com/package/@slimr/sqlite).

## Why `slimr::sqlite`?

Raw C `sqlite3*` APIs require repetitive `sqlite3_prepare_v2` / `sqlite3_bind_*` / `sqlite3_step` / `sqlite3_finalize` boilerplate, manual statement caching, and careful pragma configuration. `slimr::sqlite` gives you a single-header C++26 `Database` class with production defaults out of the box:

- **Production WAL defaults:** Automatically configures `PRAGMA busy_timeout = 5000`, `PRAGMA foreign_keys = ON`, `PRAGMA temp_store = MEMORY`, `PRAGMA journal_mode = WAL`, and `PRAGMA synchronous = NORMAL` on writable file databases.
- **256-statement LRU cache:** Prepared `sqlite3_stmt*` handles are cached and automatically reset/unbound after each call, and evicted cleanly on `exec()` or destructor teardown.
- **Positional & flexible named parameters:** Bind positional vectors (`{int64_t(1), "foo"}`) or named maps (`{{"name", "Alice"}}`) matching `:name`, `$name`, or `@name` placeholders in SQL.
- **Unified `migrations` on open:** Pass either an ordered `std::vector<std::string>` (`1..N`) or a version-keyed `std::map<int, std::string>` (supporting squashed baselines and stale intermediate version guards).
- **Safe read-only agent queries (`iterateLimited`):** Opens a separate `SQLITE_OPEN_READONLY` connection, registers custom scalar functions, executes optional `setupSql` (such as `TEMP VIEW` creation), locks down `PRAGMA query_only = ON`, and steps up to `limit + 1` rows to report `{ columns, rows, truncated }`.

---

## Quick Start

```cpp
#include <slimr/sqlite.hpp>

using namespace slimr;

Database db("./data/app.db", SqliteOptions{
    .migrations = std::vector<std::string>{
        R"(CREATE TABLE users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            email TEXT UNIQUE NOT NULL
        );)"
    }
});

// Insert with named parameters (:name, $name, or @name)
auto res = db.run(
    "INSERT INTO users (name, email) VALUES (:name, :email);",
    std::map<std::string, SqlValue>{
        {"name", std::string("Alice")},
        {"email", std::string("alice@example.com")}
    }
);

// Query a single row as slimr::Json
auto user = db.get("SELECT * FROM users WHERE id = ?;", {res.lastInsertRowid});

// Query all rows
auto allUsers = db.all("SELECT id, name FROM users ORDER BY name ASC;");

// Pluck scalar values directly (first column of first row / all rows)
auto count = db.pluck<int64_t>("SELECT count(*) FROM users;");
auto emails = db.pluckAll<std::string>("SELECT email FROM users ORDER BY id ASC;");
```

---

## Configuration (`SqliteOptions`)

| Field | Type | Default | Description |
| --- | --- | --- | --- |
| `readonly` | `bool` | `false` | Open with `SQLITE_OPEN_READONLY` (skips write pragmas and migrations). |
| `pragmas` | `std::optional<std::string>` | WAL + safe defaults | Custom pragma SQL executed after defaults, or `""` to skip extra pragmas. |
| `disableDefaultPragmas` | `bool` | `false` | Set `true` to skip all default pragmas (`WAL`, `foreign_keys`, etc.). |
| `migrations` | `std::variant<std::monostate, std::vector<std::string>, std::map<int, std::string>>` | `std::monostate{}` | Schema migrations tracked via `PRAGMA user_version` and executed atomically inside `BEGIN IMMEDIATE`. |
| `functions` | `std::map<std::string, ScalarFunction>` | `{}` | Custom C++ scalar functions registered on open (and forwarded to `readOnlyConnection` in `iterateLimited`). |

---

## Guides

### Migrations (`vector` vs Squashed `map`)

```cpp
// 1. Incremental vector (versions 1, 2, ...)
Database db1(":memory:", SqliteOptions{
    .migrations = std::vector<std::string>{
        "CREATE TABLE items (id INTEGER PRIMARY KEY, title TEXT);",
        "ALTER TABLE items ADD COLUMN priority INT DEFAULT 0;"
    }
});

// 2. Squashed version map (starts at version 3; refuses stale DBs with 0 < user_version < 3)
Database db2(":memory:", SqliteOptions{
    .migrations = std::map<int, std::string>{
        {3, "CREATE TABLE items (id INTEGER PRIMARY KEY, title TEXT, priority INT DEFAULT 0);"},
        {4, "CREATE INDEX idx_items_priority ON items(priority);"}
    }
});
```

### Transactions & Nested Savepoints

```cpp
db.transaction([&](Tx& outer) {
    outer.run("INSERT INTO logs VALUES ('outer');");

    // Nested transaction automatically uses SAVEPOINT / RELEASE / ROLLBACK TO
    outer.transaction([&](Tx& inner) {
        inner.run("INSERT INTO logs VALUES ('inner');");
    });
});
```

### Limited Iteration & Isolated Read-Only Connections (`iterateLimited`)

```cpp
auto result = db.iterateLimited(
    "SELECT * FROM v_items ORDER BY id;",
    IterateOptions{
        .limit = 200,
        .readOnlyConnection = true,
        .setupSql = "CREATE TEMP VIEW v_items AS SELECT id, title FROM items;"
    }
);
// result.columns -> std::vector<std::string>
// result.rows    -> std::vector<Json>
// result.truncated -> bool
```

### Custom Scalar Functions & Online Backup

```cpp
db.registerFunction("add_tax", [](const std::vector<SqlValue>& args) -> SqlValue {
    return std::get<double>(args.at(0)) * 1.1;
});

db.backup("./backups/app-backup.db");
```

---

## Cheat Sheet

| Goal | API |
| --- | --- |
| Open DB with migrations | `Database db(path, {.migrations = ...})` |
| Single row (`std::optional<Json>`) | `db.get(sql, params)` |
| Multiple rows (`std::vector<Json>`) | `db.all(sql, params)` |
| Single scalar (`std::optional<T>`) | `db.pluck<T>(sql, params)` (`int64_t`, `double`, `std::string`) |
| Vector of scalars (`std::vector<T>`) | `db.pluckAll<T>(sql, params)` |
| Write statement (`{changes, lastInsertRowid}`) | `db.run(sql, params)` |
| Multi-statement DDL | `db.exec(sql)` |
| Atomic transaction / savepoint | `db.transaction([&](Tx& tx) { ... })` |
| Capped read-only query | `db.iterateLimited(sql, {.limit = 200, .readOnlyConnection = true})` |
| Online backup | `db.backup(destPath)` |
