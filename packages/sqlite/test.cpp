#define SLIMR_TEST_MAIN
#include "test.hpp"
#include "sqlite.hpp"

#include <atomic>
#include <cstdint>
#include <thread>

using namespace slimr;

TEST_SUITE("SQLite Database") {
    TEST_CASE("Basic CRUD, statement cache, and FTS5") {
        Database db(":memory:");
        db.exec("CREATE TABLE items (id INTEGER PRIMARY KEY, title TEXT, score REAL);");

        // Insert
        auto r1 = db.run("INSERT INTO items (title, score) VALUES (?, ?);", {std::string("first"), 10.5});
        CHECK(r1.changes == 1);
        CHECK(r1.lastInsertRowid == 1);

        auto r2 = db.run("INSERT INTO items (title, score) VALUES (?, ?);", {std::string("second"), 20.0});
        CHECK(r2.changes == 1);
        CHECK(r2.lastInsertRowid == 2);

        // Get single
        auto row = db.get("SELECT * FROM items WHERE id = ?;", {int64_t(1)});
        CHECK(row.has_value());
        CHECK((*row)["title"].get<std::string>() == "first");
        CHECK((*row)["score"].get<double>() == 10.5);

        // All rows
        auto rows = db.all("SELECT * FROM items ORDER BY id;");
        CHECK(rows.size() == 2);
        CHECK(rows[1]["title"].get<std::string>() == "second");

        // Pluck and pluckAll
        auto title = db.pluck<std::string>("SELECT title FROM items WHERE id = ?;", {int64_t(2)});
        CHECK(title.has_value());
        CHECK(*title == "second");

        auto allScores = db.pluckAll<double>("SELECT score FROM items ORDER BY id;");
        CHECK(allScores.size() == 2);
        CHECK(allScores[0] == 10.5);
        CHECK(allScores[1] == 20.0);

        // FTS5 check
        db.exec("CREATE VIRTUAL TABLE docs USING fts5(content);");
        db.run("INSERT INTO docs (content) VALUES (?);", {std::string("apple banana orange")});
        db.run("INSERT INTO docs (content) VALUES (?);", {std::string("banana kiwi peach")});

        auto matchCount = db.pluck<int64_t>("SELECT count(*) FROM docs WHERE docs MATCH ?;", {std::string("banana")});
        CHECK(matchCount.value_or(0) == 2);
    }

    TEST_CASE("Named parameters binding") {
        Database db(":memory:");
        db.exec("CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, email TEXT);");

        // Bare name, colon, dollar, at prefix
        std::map<std::string, SqlValue> params1{
            {"name", std::string("Alice")},
            {"email", std::string("alice@example.com")}
        };
        db.run("INSERT INTO users (name, email) VALUES (:name, :email);", params1);

        std::map<std::string, SqlValue> params2{
            {":name", std::string("Bob")},
            {"email", std::string("bob@example.com")}
        };
        db.run("INSERT INTO users (name, email) VALUES ($name, @email);", params2);

        auto count = db.pluck<int64_t>("SELECT count(*) FROM users;");
        CHECK(count.value_or(0) == 2);

        auto bob = db.get("SELECT name FROM users WHERE email = :email;", std::map<std::string, SqlValue>{{"email", std::string("bob@example.com")}});
        CHECK(bob.has_value());
        CHECK((*bob)["name"].get<std::string>() == "Bob");
    }

    TEST_CASE("Migrations: vector (1..N)") {
        Database db(":memory:", SqliteOptions{
            .migrations = std::vector<std::string>{
                "CREATE TABLE v1 (x INT);",
                "CREATE TABLE v2 (y TEXT);"
            }
        });

        int64_t ver = db.pluck<int64_t>("PRAGMA user_version;").value_or(0);
        CHECK(ver == 2);

        db.run("INSERT INTO v1 VALUES (10);");
        db.run("INSERT INTO v2 VALUES ('ok');");
        CHECK(db.pluck<int64_t>("SELECT x FROM v1;").value_or(0) == 10);
        CHECK(db.pluck<std::string>("SELECT y FROM v2;").value_or("") == "ok");
    }

    TEST_CASE("Migrations: squashed map and stale version check") {
        // Squashed migration starting at version 10
        Database db(":memory:", SqliteOptions{
            .migrations = std::map<int, std::string>{
                {10, "CREATE TABLE baseline (id INT);"}
            }
        });
        CHECK(db.pluck<int64_t>("PRAGMA user_version;").value_or(0) == 10);

        // Stale database test: user_version is 5, but min migration is 10 -> throws!
        testing::TempDirGuard staleDir("stale_db_");
        std::string stalePath = (staleDir.path / "stale.db").string();
        {
            Database staleDb(stalePath);
            staleDb.exec("PRAGMA user_version = 5;");
        }
        CHECK_THROWS_AS((Database(stalePath, SqliteOptions{
            .migrations = std::map<int, std::string>{
                {10, "CREATE TABLE baseline (id INT);"}
            }
        })), std::runtime_error);
    }

    TEST_CASE("Transactions and nested Savepoints") {
        Database db(":memory:");
        db.exec("CREATE TABLE logs (msg TEXT);");

        // Successful transaction
        db.transaction([&](Tx& tx) {
            tx.run("INSERT INTO logs VALUES ('tx1');");
        });
        CHECK(db.pluck<int64_t>("SELECT count(*) FROM logs;").value_or(0) == 1);

        // Rollback transaction on throw
        try {
            db.transaction([&](Tx& tx) {
                tx.run("INSERT INTO logs VALUES ('tx2');");
                throw std::runtime_error("abort");
            });
        } catch (...) {}
        CHECK(db.pluck<int64_t>("SELECT count(*) FROM logs;").value_or(0) == 1);

        // Nested savepoint commit and partial rollback
        db.transaction([&](Tx& outer) {
            outer.run("INSERT INTO logs VALUES ('outer');");

            // Successful inner savepoint
            outer.transaction([&](Tx& inner) {
                inner.run("INSERT INTO logs VALUES ('inner1');");
            });

            // Failed inner savepoint
            try {
                outer.transaction([&](Tx& inner2) {
                    inner2.run("INSERT INTO logs VALUES ('inner2');");
                    throw std::runtime_error("inner abort");
                });
            } catch (...) {}
        });

        auto msgs = db.pluckAll<std::string>("SELECT msg FROM logs ORDER BY rowid;");
        CHECK(msgs.size() == 3);
        CHECK(msgs[0] == "tx1");
        CHECK(msgs[1] == "outer");
        CHECK(msgs[2] == "inner1");
    }

    TEST_CASE("Custom scalar function") {
        Database db(":memory:");
        db.registerFunction("add_ten", [](const std::vector<SqlValue>& args) -> SqlValue {
            if (args.empty()) return int64_t(0);
            int64_t val = std::get<int64_t>(args[0]);
            return val + 10;
        });

        auto res = db.pluck<int64_t>("SELECT add_ten(32);");
        CHECK(res.value_or(0) == 42);
    }

    TEST_CASE("iterateLimited with columns, limit, and readOnlyConnection") {
        testing::TempDirGuard dirGuard("sqlite_test_");
        std::string dbPath = (dirGuard.path / "test.db").string();

        {
            Database db(dbPath);
            db.exec("CREATE TABLE records (id INT, val TEXT);");
            for (int i = 1; i <= 5; ++i) {
                db.run("INSERT INTO records VALUES (?, ?);", {int64_t(i), "val" + std::to_string(i)});
            }
        }

        Database db(dbPath);
        // Test limit and truncation
        auto iterRes = db.iterateLimited("SELECT * FROM records ORDER BY id;", IterateOptions{
            .limit = 3,
            .readOnlyConnection = true,
            .setupSql = "PRAGMA query_only = ON;"
        });

        CHECK(iterRes.columns.size() == 2);
        CHECK(iterRes.columns[0] == "id");
        CHECK(iterRes.columns[1] == "val");
        CHECK(iterRes.rows.size() == 3);
        CHECK(iterRes.truncated == true);
    }

    TEST_CASE("Database online backup") {
        testing::TempDirGuard dirGuard("sqlite_backup_");
        std::string srcPath = (dirGuard.path / "src.db").string();
        std::string dstPath = (dirGuard.path / "dst.db").string();

        Database src(srcPath);
        src.exec("CREATE TABLE data (content TEXT);");
        src.run("INSERT INTO data VALUES ('backup this payload');");

        src.backup(dstPath);

        Database dst(dstPath);
        auto payload = dst.pluck<std::string>("SELECT content FROM data;");
        CHECK(payload.has_value());
        CHECK(*payload == "backup this payload");
    }

    TEST_CASE("Verified WAL/NORMAL/FK/busy_timeout pragmas on writable and readOnly DBs") {
        testing::TempDirGuard dirGuard("sqlite_pragmas_");
        std::string dbPath = (dirGuard.path / "pragmas.db").string();

        {
            Database db(dbPath);
            CHECK(db.pluck<std::string>("PRAGMA journal_mode;").value_or("") == "wal");
            CHECK(db.pluck<int64_t>("PRAGMA synchronous;").value_or(-1) == 1);
            CHECK(db.pluck<int64_t>("PRAGMA foreign_keys;").value_or(0) == 1);
            CHECK(db.pluck<int64_t>("PRAGMA temp_store;").value_or(0) == 2);
            CHECK(db.pluck<int64_t>("PRAGMA busy_timeout;").value_or(0) == 5000);
            db.exec("CREATE TABLE t (id INTEGER PRIMARY KEY);");
        }

        {
            Database ro(dbPath, SqliteOptions{.readOnly = true});
            CHECK(ro.pluck<int64_t>("PRAGMA busy_timeout;").value_or(0) == 5000);
            CHECK(ro.pluck<int64_t>("PRAGMA foreign_keys;").value_or(0) == 1);
        }

        std::string missingPath = (dirGuard.path / "does_not_exist.db").string();
        CHECK_THROWS_AS((Database(missingPath, SqliteOptions{.readOnly = true})), std::runtime_error);
    }

    TEST_CASE("get() and pluck() step only the first row and reuse cached statements") {
        Database db(":memory:");
        db.exec("CREATE TABLE items (id INTEGER PRIMARY KEY);");
        db.transaction([&](Tx& tx) {
            for (int64_t i = 1; i <= 100; ++i) {
                tx.run("INSERT INTO items (id) VALUES (?);", {i});
            }
        });

        int64_t calls = 0;
        db.registerFunction("count_step", [&](const std::vector<SqlValue>& args) -> SqlValue {
            ++calls;
            return args[0];
        }, /*deterministic=*/false);

        auto row = db.get("SELECT count_step(id) AS val FROM items ORDER BY id;");
        CHECK(row.has_value());
        CHECK((*row)["val"].get<int64_t>() == 1);
        CHECK(calls == 1);

        calls = 0;
        auto val = db.pluck<int64_t>("SELECT count_step(id) FROM items ORDER BY id;");
        CHECK(val.has_value());
        CHECK(*val == 1);
        CHECK(calls == 1);

        // Reuse the same cached statement for get() again, then full all()
        calls = 0;
        auto val2 = db.pluck<int64_t>("SELECT count_step(id) FROM items ORDER BY id;");
        CHECK(val2.value_or(0) == 1);
        CHECK(calls == 1);

        calls = 0;
        auto allVals = db.pluckAll<int64_t>("SELECT count_step(id) FROM items ORDER BY id;");
        CHECK(allVals.size() == 100);
        CHECK(calls == 100);
    }

    TEST_CASE("Embedded NUL bytes round-trip in TEXT across queries and scalar functions") {
        Database db(":memory:");
        db.exec("CREATE TABLE payloads (id INTEGER PRIMARY KEY, txt TEXT NOT NULL);");
        std::string embedded("a\0b", 3);
        std::string leading("\0\0z", 3);

        db.run("INSERT INTO payloads (txt) VALUES (?);", {embedded});
        db.run("INSERT INTO payloads (txt) VALUES (?);", {leading});

        auto p1 = db.pluck<std::string>("SELECT txt FROM payloads WHERE id = 1;");
        REQUIRE(p1.has_value());
        CHECK(p1->size() == 3);
        CHECK(*p1 == embedded);

        auto p2 = db.get("SELECT txt FROM payloads WHERE id = 2;");
        REQUIRE(p2.has_value());
        auto s2 = (*p2)["txt"].get<std::string>();
        CHECK(s2.size() == 3);
        CHECK(s2 == leading);

        auto sv = db.pluck<SqlValue>("SELECT txt FROM payloads WHERE id = 1;");
        REQUIRE(sv.has_value());
        REQUIRE(std::holds_alternative<std::string>(*sv));
        CHECK(std::get<std::string>(*sv) == embedded);

        db.registerFunction("echo_txt", [](const std::vector<SqlValue>& args) -> SqlValue {
            return args.at(0);
        });
        auto fnOut = db.pluck<std::string>("SELECT echo_txt(?);", {embedded});
        REQUIRE(fnOut.has_value());
        CHECK(fnOut->size() == 3);
        CHECK(*fnOut == embedded);
    }

    TEST_CASE("Strict column conversion errors and optional NULL handling") {
        Database db(":memory:");
        db.exec("CREATE TABLE vals (id INTEGER PRIMARY KEY, s TEXT, n INTEGER);");
        db.run("INSERT INTO vals (s, n) VALUES ('not_int', 300);");
        db.run("INSERT INTO vals (s, n) VALUES (NULL, NULL);");

        CHECK_THROWS_AS(db.pluck<int64_t>("SELECT s FROM vals WHERE id = 1;"), std::runtime_error);
        CHECK_THROWS_AS(db.pluck<int8_t>("SELECT n FROM vals WHERE id = 1;"), std::overflow_error);
        CHECK_THROWS_AS(db.pluck<int64_t>("SELECT n FROM vals WHERE id = 2;"), std::runtime_error);
        CHECK_THROWS_AS(db.pluck<std::string>("SELECT s FROM vals WHERE id = 2;"), std::runtime_error);

        auto optNull = db.pluck<std::optional<int64_t>>("SELECT n FROM vals WHERE id = 2;");
        REQUIRE(optNull.has_value());
        CHECK(!optNull->has_value());

        auto optPresent = db.pluck<std::optional<int64_t>>("SELECT n FROM vals WHERE id = 1;");
        REQUIRE(optPresent.has_value());
        REQUIRE(optPresent->has_value());
        CHECK(**optPresent == 300);
    }

    TEST_CASE("Statement cache does not evict in-use statements during reentrant queries") {
        Database db(":memory:");
        db.exec("CREATE TABLE nums (v INTEGER);");
        db.run("INSERT INTO nums VALUES (7);");

        db.registerFunction("nested_lookup", [&](const std::vector<SqlValue>& args) -> SqlValue {
            // Fill >256 distinct statements while the outer query holds an in-use cached statement
            for (int i = 0; i < 270; ++i) {
                auto v = db.pluck<int64_t>("SELECT " + std::to_string(i) + ";");
                (void)v;
            }
            return args[0];
        }, /*deterministic=*/false);

        auto res = db.pluck<int64_t>("SELECT nested_lookup(v) FROM nums;");
        CHECK(res.value_or(0) == 7);
    }

    TEST_CASE("Concurrent WAL reader/writer across independent connections") {
        CHECK(sqlite3_threadsafe() != 0);

        testing::TempDirGuard dirGuard("sqlite_conc_");
        std::string dbPath = (dirGuard.path / "conc.db").string();

        {
            Database init(dbPath);
            init.exec("CREATE TABLE events (id INTEGER PRIMARY KEY, seq INTEGER NOT NULL);");
        }

        constexpr int kTotalWrites = 80;
        std::atomic<bool> writerDone{false};
        std::atomic<int> readErrors{0};
        std::atomic<int> writeErrors{0};
        int64_t maxObserved = 0;

        std::thread writer([&]() {
            try {
                Database wdb(dbPath);
                for (int i = 1; i <= kTotalWrites; ++i) {
                    wdb.transaction([&](Tx& tx) {
                        tx.run("INSERT INTO events (seq) VALUES (?);", {int64_t(i)});
                    });
                }
            } catch (...) {
                writeErrors.fetch_add(1);
            }
            writerDone.store(true);
        });

        std::thread reader([&]() {
            try {
                Database rdb(dbPath, SqliteOptions{.readOnly = true});
                while (!writerDone.load()) {
                    auto c = rdb.pluck<int64_t>("SELECT count(*) FROM events;");
                    if (c && *c > maxObserved) {
                        maxObserved = *c;
                    }
                    auto iter = rdb.iterateLimited("SELECT seq FROM events ORDER BY id DESC;", IterateOptions{
                        .limit = 5,
                        .readOnlyConnection = true
                    });
                    (void)iter;
                }
            } catch (...) {
                readErrors.fetch_add(1);
            }
        });

        writer.join();
        reader.join();

        CHECK(writeErrors.load() == 0);
        CHECK(readErrors.load() == 0);

        Database verify(dbPath, SqliteOptions{.readOnly = true});
        CHECK(verify.pluck<int64_t>("SELECT count(*) FROM events;").value_or(0) == kTotalWrites);
    }
}
