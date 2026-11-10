#define SLIMR_TEST_MAIN
#include "slimr/test.hpp"
#include "slimr/json.hpp"
#include "slimr/reflect.hpp"
#include "slimr/yaml.hpp"
#include "slimr/sqlite.hpp"
#include "slimr/curl.hpp"
#include "slimr/ws.hpp"
#include "slimr/bugsink.hpp"
#include "slimr/mcp.hpp"
#include "slimr/encoding.hpp"
#include "slimr/proc.hpp"
#include "slimr/time.hpp"
#include "slimr/text.hpp"

#include <iostream>

using namespace slimr;

// ============================================================================
// 1. JSON & Struct Reflection Tests
// ============================================================================

struct SimpleTestStruct {
    Field<std::string, "The name of the item"> name;
    Desc<std::optional<int>, "Optional count"> count;
    OptField<bool, "Flag indicating active state"> active{true};
};

struct EmptyStruct {};

TEST_SUITE("JSON & Struct Reflection") {
    TEST_CASE("JSON read and write") {
        Json j = Json::object_t{};
        j["hello"] = "world";
        j["num"] = 123;
        std::string s = slimr::write_json(j);
        CHECK(s.find("\"hello\":\"world\"") != std::string::npos);

        auto parsed = parse_json<Json>(s);
        CHECK(parsed.has_value());
        CHECK((*parsed)["hello"].get<std::string>() == "world");
        CHECK((*parsed)["num"].get<int64_t>() == 123);
    }

    TEST_CASE("Field and Desc serialization/deserialization") {
        SimpleTestStruct obj{"Item1", 42, false};
        std::string s = write_json(obj);
        CHECK(s.find("\"name\":\"Item1\"") != std::string::npos);
        CHECK(s.find("\"count\":42") != std::string::npos);
        CHECK(s.find("\"active\":false") != std::string::npos);

        SimpleTestStruct readBack;
        auto ec = read_json(readBack, s);
        CHECK(!ec);
        CHECK(readBack.name.get() == "Item1");
        CHECK(readBack.count.get().has_value());
        CHECK(*readBack.count.get() == 42);
        CHECK(readBack.active.get() == false);
    }

    TEST_CASE("MCP Schema Generation") {
        Json sc = schema<SimpleTestStruct>();
        CHECK(sc["type"].get<std::string>() == "object");
        CHECK(sc["additionalProperties"].get<bool>() == false);

        auto props = sc["properties"];
        CHECK(props.contains("name"));
        CHECK(props["name"]["type"].get<std::string>() == "string");
        CHECK(props["name"]["description"].get<std::string>() == "The name of the item");

        CHECK(props.contains("count"));
        CHECK(props["count"]["type"].get<std::string>() == "integer");
        CHECK(props["count"]["description"].get<std::string>() == "Optional count");

        CHECK(props.contains("active"));
        CHECK(props["active"]["type"].get<std::string>() == "boolean");
        CHECK(props["active"]["description"].get<std::string>() == "Flag indicating active state");

        // Check required array: "name" is required; "count" (optional) and "active" (OptField) are not.
        auto req = sc["required"];
        CHECK(req.is_array());
        auto reqArr = req.get<Json::array_t>();
        bool hasName = false;
        bool hasCount = false;
        bool hasActive = false;
        for (const auto& r : reqArr) {
            std::string fieldName = r.get<std::string>();
            if (fieldName == "name") hasName = true;
            if (fieldName == "count") hasCount = true;
            if (fieldName == "active") hasActive = true;
        }
        CHECK(hasName == true);
        CHECK(hasCount == false);
        CHECK(hasActive == false);
    }

    TEST_CASE("Empty struct schema generation") {
        Json sc = schema<EmptyStruct>();
        CHECK(sc["type"].get<std::string>() == "object");
        CHECK(sc["properties"].empty());
        CHECK(sc["required"].empty());
        CHECK(sc["additionalProperties"].get<bool>() == false);
    }
}

// ============================================================================
// 2. SQLite Database Tests
// ============================================================================

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

// ============================================================================
// 3. MCP Protocol & CLI Tests
// ============================================================================

struct AddInput {
    Field<int, "First operand"> a;
    Field<int, "Second operand"> b;
};

TEST_SUITE("MCP Protocol & CLI") {
    TEST_CASE("PluginApp initialize and tools/list") {
        PluginApp app("test-app", "1.2.3");
        app.instructions = "Test instructions for agent";
        app.resources.push_back(Resource{
            .uri = "test://notes",
            .name = "test_resource",
            .description = "Test resource text",
            .mimeType = "text/plain",
            .load = []() { return "Hello from resource!"; }
        });

        app.tool<AddInput>("add", "Adds two numbers", [](const AddInput& in) {
            Json res = Json::object_t{};
            res["sum"] = in.a.get() + in.b.get();
            return res;
        });

        // Initialize
        Json initReq = Json::object_t{};
        initReq["jsonrpc"] = "2.0";
        initReq["id"] = 1;
        initReq["method"] = "initialize";
        initReq["params"]["protocolVersion"] = "2025-11-25";

        Json initResp = app.dispatchMcpJson(initReq);
        CHECK(initResp["jsonrpc"].get<std::string>() == "2.0");
        CHECK(initResp["result"]["protocolVersion"].get<std::string>() == "2025-11-25");
        CHECK(initResp["result"]["serverInfo"]["name"].get<std::string>() == "test-app");
        CHECK(initResp["result"]["serverInfo"]["version"].get<std::string>() == "1.2.3");
        CHECK(initResp["result"]["instructions"].get<std::string>() == "Test instructions for agent");

        // Ping
        Json pingReq = Json::object_t{};
        pingReq["jsonrpc"] = "2.0";
        pingReq["id"] = 2;
        pingReq["method"] = "ping";
        Json pingResp = app.dispatchMcpJson(pingReq);
        CHECK(pingResp["jsonrpc"].get<std::string>() == "2.0");
        CHECK(pingResp["id"].get<int64_t>() == 2);

        // tools/list (should include 'add' and auto-added 'mcp_resource_read')
        Json listReq = Json::object_t{};
        listReq["jsonrpc"] = "2.0";
        listReq["id"] = 3;
        listReq["method"] = "tools/list";
        Json listResp = app.dispatchMcpJson(listReq);
        auto tools = listResp["result"]["tools"].get<Json::array_t>();
        CHECK(tools.size() == 2);

        bool foundAdd = false;
        bool foundResourceRead = false;
        for (const auto& t : tools) {
            std::string tName = t["name"].get<std::string>();
            if (tName == "add") foundAdd = true;
            if (tName == "mcp_resource_read") foundResourceRead = true;
        }
        CHECK(foundAdd == true);
        CHECK(foundResourceRead == true);
    }

    TEST_CASE("tools/call: tool execution and media content blocks") {
        PluginApp app("media-app");

        app.tool<AddInput>("add", "Adds two numbers", [](const AddInput& in) {
            Json res = Json::object_t{};
            res["sum"] = in.a.get() + in.b.get();
            return res;
        });

        app.tool<std::monostate>("get_image", "Returns a mock image block", [](const std::monostate&) {
            return McpResult::image("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==", "image/png");
        });

        // Call add
        Json callAdd = Json::object_t{};
        callAdd["jsonrpc"] = "2.0";
        callAdd["id"] = 10;
        callAdd["method"] = "tools/call";
        callAdd["params"]["name"] = "add";
        callAdd["params"]["arguments"]["a"] = 20;
        callAdd["params"]["arguments"]["b"] = 22;

        Json addResp = app.dispatchMcpJson(callAdd);
        CHECK(addResp["result"]["isError"].get<bool>() == false);
        auto contents = addResp["result"]["content"].get<Json::array_t>();
        CHECK(contents.size() == 1);
        CHECK(contents[0]["type"].get<std::string>() == "text");
        CHECK(contents[0]["text"].get<std::string>().find("\"sum\":42") != std::string::npos);

        // Call get_image
        Json callImg = Json::object_t{};
        callImg["jsonrpc"] = "2.0";
        callImg["id"] = 11;
        callImg["method"] = "tools/call";
        callImg["params"]["name"] = "get_image";
        callImg["params"]["arguments"] = Json::object_t{};

        Json imgResp = app.dispatchMcpJson(callImg);
        CHECK(imgResp["result"]["isError"].get<bool>() == false);
        auto imgContents = imgResp["result"]["content"].get<Json::array_t>();
        CHECK(imgContents.size() == 1);
        CHECK(imgContents[0]["type"].get<std::string>() == "image");
        CHECK(imgContents[0]["mimeType"].get<std::string>() == "image/png");
        CHECK(imgContents[0]["data"].get<std::string>().find("iVBOR") != std::string::npos);
    }

    TEST_CASE("Resources list, read, and mcp_resource_read fallback") {
        PluginApp app("res-app");
        app.resources.push_back(Resource{
            .uri = "docs://intro",
            .name = "intro",
            .description = "Introduction doc",
            .mimeType = "text/markdown",
            .load = []() { return "# Intro Header"; }
        });

        // resources/list
        Json listReq = Json::object_t{};
        listReq["jsonrpc"] = "2.0";
        listReq["id"] = 20;
        listReq["method"] = "resources/list";
        Json listResp = app.dispatchMcpJson(listReq);
        auto resArr = listResp["result"]["resources"].get<Json::array_t>();
        CHECK(resArr.size() == 1);
        CHECK(resArr[0]["uri"].get<std::string>() == "docs://intro");

        // resources/read
        Json readReq = Json::object_t{};
        readReq["jsonrpc"] = "2.0";
        readReq["id"] = 21;
        readReq["method"] = "resources/read";
        readReq["params"]["uri"] = "docs://intro";
        Json readResp = app.dispatchMcpJson(readReq);
        auto contents = readResp["result"]["contents"].get<Json::array_t>();
        CHECK(contents.size() == 1);
        CHECK(contents[0]["text"].get<std::string>() == "# Intro Header");

        // tools/call with mcp_resource_read
        Json toolCallReq = Json::object_t{};
        toolCallReq["jsonrpc"] = "2.0";
        toolCallReq["id"] = 22;
        toolCallReq["method"] = "tools/call";
        toolCallReq["params"]["name"] = "mcp_resource_read";
        toolCallReq["params"]["arguments"]["uri"] = "docs://intro";
        Json toolCallResp = app.dispatchMcpJson(toolCallReq);
        CHECK(toolCallResp["result"]["isError"].get<bool>() == false);
        auto toolContents = toolCallResp["result"]["content"].get<Json::array_t>();
        CHECK(toolContents[0]["text"].get<std::string>() == "# Intro Header");
    }

    TEST_CASE("Gateway multi-plugin mounting and routing") {
        PluginApp notesApp("obsidian");
        notesApp.instructions = "Notes instructions";
        notesApp.tool<std::monostate>("read", "Reads note", [](const std::monostate&) {
            return "note content";
        });

        PluginApp logApp("personal-log");
        logApp.instructions = "Log instructions";
        logApp.tool<std::monostate>("search", "Searches log", [](const std::monostate&) {
            return "log entry";
        });

        Gateway gw;
        gw.mount("notes", notesApp);
        gw.mount("log", logApp);

        CHECK(gw.tools().contains("notes_read"));
        CHECK(gw.tools().contains("log_search"));
        CHECK(gw.instructions.find("## notes\nNotes instructions") != std::string::npos);
        CHECK(gw.instructions.find("## log\nLog instructions") != std::string::npos);

        // Dispatch call to notes_read
        Json callNotes = Json::object_t{};
        callNotes["jsonrpc"] = "2.0";
        callNotes["id"] = 30;
        callNotes["method"] = "tools/call";
        callNotes["params"]["name"] = "notes_read";
        callNotes["params"]["arguments"] = Json::object_t{};

        Json notesResp = gw.dispatchMcpJson(callNotes);
        CHECK(notesResp["result"]["isError"].get<bool>() == false);
        auto contents = notesResp["result"]["content"].get<Json::array_t>();
        CHECK(contents[0]["text"].get<std::string>() == "note content");
    }

    TEST_CASE("CLI direct invocation") {
        PluginApp app("cli-app");
        app.tool<AddInput>("add", "Adds two numbers", [](const AddInput& in) {
            Json res = Json::object_t{};
            res["result"] = in.a.get() + in.b.get();
            return res;
        });

        // Test tool with arguments via argv
        char arg0[] = "cli-app";
        char arg1[] = "add";
        char arg2[] = "{\"a\":15,\"b\":27}";
        char* argv[] = {arg0, arg1, arg2, nullptr};

        // Redirect stdout to capture
        std::stringstream ss;
        auto* oldBuf = std::cout.rdbuf(ss.rdbuf());
        int code = app.run(3, argv);
        std::cout.rdbuf(oldBuf);

        CHECK(code == 0);
        std::string output = ss.str();
        CHECK(output.find("\"result\":42") != std::string::npos);
    }
}

// ============================================================================
// 4. Bugsink DSN & Envelope Formatting Tests
// ============================================================================

TEST_SUITE("Bugsink Reporter") {
    TEST_CASE("DSN parsing") {
        auto parsed = BugsinkReporter::parseDsn("https://8973845e128c4204ba7a7e02eaba6a17@bugsink.btek.cc/4");
        CHECK(parsed.valid == true);
        CHECK(parsed.protocol == "https");
        CHECK(parsed.publicKey == "8973845e128c4204ba7a7e02eaba6a17");
        CHECK(parsed.host == "bugsink.btek.cc");
        CHECK(parsed.projectId == "4");

        BugsinkReporter reporter("https://key123@example.com/99");
        CHECK(reporter.envelopeUrl() == "https://example.com/api/99/envelope/?sentry_key=key123&sentry_version=7");
    }

    TEST_CASE("Envelope NDJSON formatting") {
        BugsinkReporter reporter("https://pub@sentry.local/1");
        Json payload = Json::object_t{};
        payload["message"] = "test event";

        std::string env = reporter.formatEnvelope(payload, "event123", "2026-11-10T12:00:00Z");

        // Verify it contains exactly 3 newline-separated JSON objects
        std::vector<std::string> lines;
        std::stringstream ss(env);
        std::string line;
        while (std::getline(ss, line)) {
            if (!line.empty()) lines.push_back(line);
        }

        CHECK(lines.size() == 3);
        CHECK(lines[0].find("\"event_id\":\"event123\"") != std::string::npos);
        CHECK(lines[0].find("\"sent_at\":\"2026-11-10T12:00:00Z\"") != std::string::npos);
        CHECK(lines[1] == "{\"type\":\"event\",\"content_type\":\"application/json\"}");
        CHECK(lines[2].find("\"message\":\"test event\"") != std::string::npos);
    }

    TEST_CASE("Environment override disables on empty string") {
        setenv("TEST_BUGSINK_DSN", "", 1);
        BugsinkReporter reporter("https://pub@sentry.local/1", "TEST_BUGSINK_DSN");
        CHECK(reporter.effectiveDsn() == "");
        CHECK(reporter.envelopeUrl() == "");
        unsetenv("TEST_BUGSINK_DSN");
    }
}

// ============================================================================
// 5. YAML & Frontmatter Tests
// ============================================================================

struct YamlMainConfig {
    std::string name;
    int port{8080};
    bool enabled{true};
    std::vector<std::string> tags;
};

TEST_SUITE("YAML & Frontmatter") {
    TEST_CASE("parse_yaml basic values and structures") {
        std::string yaml_doc = "name: slimr-yaml\nport: 9000\nenabled: true\ntags:\n  - fast\n  - cpp26\n";
        auto res = parse_yaml<Json>(yaml_doc);
        REQUIRE(res.has_value());
        const Json& j = *res;
        CHECK(j["name"].get<std::string>() == "slimr-yaml");
        CHECK(j["port"].get<int64_t>() == 9000);
        CHECK(j["enabled"].get<bool>() == true);
        REQUIRE(j["tags"].is_array());
        CHECK(j["tags"].get<Json::array_t>().size() == 2);
    }

    TEST_CASE("to_yaml emission and roundtrip") {
        Json j = Json::object_t{};
        j["title"] = "Test Title";
        j["count"] = 42;

        std::string out = to_yaml(j);
        CHECK(out.find("title: Test Title") != std::string::npos);
        CHECK(out.find("count: 42") != std::string::npos);

        auto roundtrip = parse_yaml<Json>(out);
        REQUIRE(roundtrip.has_value());
        CHECK((*roundtrip)["title"].get<std::string>() == "Test Title");
        CHECK((*roundtrip)["count"].get<int64_t>() == 42);
    }

    TEST_CASE("parse_frontmatter with valid frontmatter") {
        std::string md = "---\ntitle: Note 1\nauthor: Brian\n---\n\n# Body Heading\nThis is content.";
        auto split = parse_frontmatter(md);
        REQUIRE(split.has_value());
        REQUIRE(split->frontmatter.has_value());
        CHECK((*split->frontmatter)["title"].get<std::string>() == "Note 1");
        CHECK(split->body.find("# Body Heading") != std::string::npos);
    }
}

// ============================================================================
// 6. Binary & Text Encoding Tests
// ============================================================================

TEST_SUITE("Binary & Text Encoding") {
    TEST_CASE("Hex encode and decode") {
        std::string raw = "Hello, World!";
        std::string hex = hex_encode(raw);
        CHECK(hex == "48656c6c6f2c20576f726c6421");
        auto decoded = hex_decode(hex);
        REQUIRE(decoded.has_value());
        CHECK(*decoded == raw);
    }

    TEST_CASE("RFC 4648 Base64 encode and decode") {
        CHECK(base64_encode("foobar") == "Zm9vYmFy");
        auto d = base64_decode("Zm9vYmFy");
        REQUIRE(d.has_value());
        CHECK(*d == "foobar");
    }

    TEST_CASE("Base64url encode and decode") {
        std::string raw = "subjects?_d=1&foo+bar";
        std::string b64url = base64url_encode(raw, false);
        auto d = base64url_decode(b64url);
        REQUIRE(d.has_value());
        CHECK(*d == raw);
    }
}

// ============================================================================
// 7. Subprocess Execution Tests
// ============================================================================

TEST_SUITE("Subprocess Execution") {
    TEST_CASE("Basic command execution and stdout capture") {
        auto res = slimr::proc::exec({"echo", "hello proc"});
        CHECK(res.exit_code == 0);
        CHECK(res.stdout_str == "hello proc\n");
    }

    TEST_CASE("Stdin data piping") {
        slimr::proc::ProcOptions opt;
        opt.stdin_data = "piped data\n";
        auto res = slimr::proc::exec({"cat"}, opt);
        CHECK(res.exit_code == 0);
        CHECK(res.stdout_str == "piped data\n");
    }

    TEST_CASE("Timeout handling") {
        slimr::proc::ProcOptions opt;
        opt.timeout_ms = 150;
        auto res = slimr::proc::exec({"sleep", "2"}, opt);
        CHECK(res.timed_out == true);
    }
}

// ============================================================================
// 8. Timezone & Civil Date Tests
// ============================================================================

TEST_SUITE("Timezone & Civil Date") {
    TEST_CASE("format and parse ISO-8601 UTC") {
        int64_t sec = 1768469400; // 2026-01-15T09:30:00Z
        CHECK(format_iso8601_utc(sec) == "2026-01-15T09:30:00Z");
        auto parsed = parse_iso8601_utc("2026-01-15T09:30:00Z");
        REQUIRE(parsed.has_value());
        CHECK(*parsed == sec);
    }

    TEST_CASE("civil_day_bounds Europe/Dublin") {
        auto winter = civil_day_bounds("2026-01-15", "Europe/Dublin");
        REQUIRE(winter.has_value());
        CHECK(format_iso8601_utc(winter->start_sec) == "2026-01-15T00:00:00Z");

        auto summer = civil_day_bounds("2026-07-01", "Europe/Dublin");
        REQUIRE(summer.has_value());
        CHECK(format_iso8601_utc(summer->start_sec) == "2026-06-30T23:00:00Z");
    }
}

// ============================================================================
// 9. Text & HTML Sanitization Tests
// ============================================================================

TEST_SUITE("Text & HTML Sanitization") {
    TEST_CASE("clean_text removes invisible and bidi characters") {
        std::string raw = "Hello\xe2\x80\x8b World\xef\xbb\xbf!";
        CHECK(clean_text(raw) == "Hello World!");
    }

    TEST_CASE("html_to_text basic conversion and sanitization") {
        std::string html = "<h1>Title</h1><p>Visit <a href=\"https://example.com\">Site</a></p><script>alert(1)</script><div style=\"display:none\">secret</div>";
        std::string text = html_to_text(html);
        CHECK(text.find("Title") != std::string::npos);
        CHECK(text.find("Visit Site (https://example.com)") != std::string::npos);
        CHECK(text.find("alert") == std::string::npos);
        CHECK(text.find("secret") == std::string::npos);
    }
}
