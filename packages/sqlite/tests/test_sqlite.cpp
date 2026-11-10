#define SLIMR_TEST_MAIN
#include "slimr/test.hpp"
#include "slimr/sqlite.hpp"

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
}
