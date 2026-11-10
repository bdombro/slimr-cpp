#pragma once

#include "slimr/json.hpp"

#include <sqlite3.h>

#include <filesystem>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace slimr {

/// SQLite variant input/output value
using SqlValue = std::variant<
    std::monostate,
    int64_t,
    double,
    std::string,
    std::vector<uint8_t>,
    bool
>;

struct RunResult {
    int64_t changes = 0;
    int64_t lastInsertRowid = 0;
};

enum class TransactionMode {
    Immediate,
    Deferred,
    Exclusive
};

struct IterateOptions {
    size_t limit = 0; // 0 = unlimited
    bool readOnlyConnection = false;
    std::string setupSql = "";
};

struct IterateResult {
    std::vector<std::string> columns;
    std::vector<Json> rows;
    bool truncated = false;
};

struct SqliteOptions {
    bool readOnly = false;
    bool enableDefaultPragmas = true;
    std::optional<std::string> pragmas = std::nullopt;
    std::optional<std::variant<std::vector<std::string>, std::map<int, std::string>>> migrations = std::nullopt;
};

namespace detail {

inline void throw_sqlite(sqlite3* db, std::string_view msg) {
    std::string err = std::string(msg);
    if (db) {
        err += ": ";
        err += sqlite3_errmsg(db);
    }
    throw std::runtime_error(err);
}

inline void bind_one(sqlite3* db, sqlite3_stmt* stmt, int idx, const SqlValue& val) {
    int rc = SQLITE_OK;
    std::visit([&](auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            rc = sqlite3_bind_null(stmt, idx);
        } else if constexpr (std::is_same_v<T, int64_t>) {
            rc = sqlite3_bind_int64(stmt, idx, v);
        } else if constexpr (std::is_same_v<T, double>) {
            rc = sqlite3_bind_double(stmt, idx, v);
        } else if constexpr (std::is_same_v<T, std::string>) {
            rc = sqlite3_bind_text(stmt, idx, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
        } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
            rc = sqlite3_bind_blob(stmt, idx, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
        } else if constexpr (std::is_same_v<T, bool>) {
            rc = sqlite3_bind_int(stmt, idx, v ? 1 : 0);
        }
    }, val);
    if (rc != SQLITE_OK) {
        throw_sqlite(db, "Failed to bind parameter at index " + std::to_string(idx));
    }
}

inline void bind_params(sqlite3* db, sqlite3_stmt* stmt, const std::vector<SqlValue>& params) {
    for (size_t i = 0; i < params.size(); ++i) {
        bind_one(db, stmt, static_cast<int>(i + 1), params[i]);
    }
}

inline void bind_params(sqlite3* db, sqlite3_stmt* stmt, const std::map<std::string, SqlValue>& params) {
    for (const auto& [name, val] : params) {
        int idx = sqlite3_bind_parameter_index(stmt, name.c_str());
        if (idx == 0 && !name.empty()) {
            std::string bare = name;
            if (bare[0] == ':' || bare[0] == '$' || bare[0] == '@') {
                bare = bare.substr(1);
            }
            std::string colon = ":" + bare;
            idx = sqlite3_bind_parameter_index(stmt, colon.c_str());
            if (idx == 0) {
                std::string dollar = "$" + bare;
                idx = sqlite3_bind_parameter_index(stmt, dollar.c_str());
                if (idx == 0) {
                    std::string at = "@" + bare;
                    idx = sqlite3_bind_parameter_index(stmt, at.c_str());
                }
            }
        }
        if (idx > 0) {
            bind_one(db, stmt, idx, val);
        }
    }
}

inline Json row_to_json(sqlite3_stmt* stmt) {
    int cols = sqlite3_column_count(stmt);
    Json row = Json::object_t{};
    for (int i = 0; i < cols; ++i) {
        const char* name = sqlite3_column_name(stmt, i);
        int type = sqlite3_column_type(stmt, i);
        std::string key = name ? name : "";
        switch (type) {
            case SQLITE_INTEGER:
                row[key] = sqlite3_column_int64(stmt, i);
                break;
            case SQLITE_FLOAT:
                row[key] = sqlite3_column_double(stmt, i);
                break;
            case SQLITE_TEXT: {
                const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
                row[key] = std::string(txt ? txt : "");
                break;
            }
            case SQLITE_BLOB: {
                const char* blob = reinterpret_cast<const char*>(sqlite3_column_blob(stmt, i));
                int bytes = sqlite3_column_bytes(stmt, i);
                row[key] = std::string(blob ? blob : "", bytes);
                break;
            }
            case SQLITE_NULL:
            default:
                row[key] = std::monostate{};
                break;
        }
    }
    return row;
}

template <typename T>
inline T column_to_val(sqlite3_stmt* stmt, int col) {
    if constexpr (std::is_same_v<T, int64_t>) {
        return sqlite3_column_int64(stmt, col);
    } else if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
        return static_cast<T>(sqlite3_column_int64(stmt, col));
    } else if constexpr (std::is_same_v<T, double> || std::is_floating_point_v<T>) {
        return sqlite3_column_double(stmt, col);
    } else if constexpr (std::is_same_v<T, bool>) {
        return sqlite3_column_int(stmt, col) != 0;
    } else if constexpr (std::is_same_v<T, std::string>) {
        const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
        return std::string(txt ? txt : "");
    } else if constexpr (std::is_same_v<T, SqlValue>) {
        int type = sqlite3_column_type(stmt, col);
        switch (type) {
            case SQLITE_INTEGER: return sqlite3_column_int64(stmt, col);
            case SQLITE_FLOAT: return sqlite3_column_double(stmt, col);
            case SQLITE_TEXT: {
                const char* txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
                return std::string(txt ? txt : "");
            }
            case SQLITE_BLOB: {
                const uint8_t* blob = reinterpret_cast<const uint8_t*>(sqlite3_column_blob(stmt, col));
                int bytes = sqlite3_column_bytes(stmt, col);
                return std::vector<uint8_t>(blob, blob + bytes);
            }
            case SQLITE_NULL:
            default:
                return std::monostate{};
        }
    } else {
        // Fallback for custom types or Json
        Json j = row_to_json(stmt);
        const char* name = sqlite3_column_name(stmt, col);
        std::string key = name ? name : "";
        std::string s;
        (void)glz::write_json(j[key], s);
        T res{};
        (void)glz::read_json(res, s);
        return res;
    }
}

} // namespace detail

class Database;
using SqliteDb = Database;

class Tx {
public:
    Tx(Database& db, int depth) : db_(db), depth_(depth) {}

    template <typename T = Json>
    std::optional<T> get(std::string_view sql, const std::vector<SqlValue>& params = {});
    template <typename T = Json>
    std::optional<T> get(std::string_view sql, const std::map<std::string, SqlValue>& params);

    template <typename T = Json>
    std::vector<T> all(std::string_view sql, const std::vector<SqlValue>& params = {});
    template <typename T = Json>
    std::vector<T> all(std::string_view sql, const std::map<std::string, SqlValue>& params);

    template <typename T = SqlValue>
    std::optional<T> pluck(std::string_view sql, const std::vector<SqlValue>& params = {});
    template <typename T = SqlValue>
    std::optional<T> pluck(std::string_view sql, const std::map<std::string, SqlValue>& params);

    template <typename T = SqlValue>
    std::vector<T> pluckAll(std::string_view sql, const std::vector<SqlValue>& params = {});
    template <typename T = SqlValue>
    std::vector<T> pluckAll(std::string_view sql, const std::map<std::string, SqlValue>& params);

    RunResult run(std::string_view sql, const std::vector<SqlValue>& params = {});
    RunResult run(std::string_view sql, const std::map<std::string, SqlValue>& params);

    void exec(std::string_view sql);

    template <typename Fn>
    auto transaction(Fn&& fn, TransactionMode mode = TransactionMode::Immediate);

    int depth() const noexcept { return depth_; }

private:
    Database& db_;
    int depth_ = 0;
};

class Database {
public:
    struct FunctionDef {
        std::string name;
        std::function<SqlValue(const std::vector<SqlValue>&)> fn;
        bool deterministic = true;
    };

    explicit Database(std::string_view path, SqliteOptions opts = {})
        : path_(path), options_(std::move(opts)) {
        open_db();
    }

    ~Database() {
        close();
    }

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&& other) noexcept {
        move_from(std::move(other));
    }
    Database& operator=(Database&& other) noexcept {
        if (this != &other) {
            close();
            move_from(std::move(other));
        }
        return *this;
    }

    sqlite3* handle() const noexcept { return db_; }
    const std::string& path() const noexcept { return path_; }

    void exec(std::string_view sql) {
        char* errmsg = nullptr;
        int rc = sqlite3_exec(db_, std::string(sql).c_str(), nullptr, nullptr, &errmsg);
        if (rc != SQLITE_OK) {
            std::string err = errmsg ? errmsg : "exec failed";
            sqlite3_free(errmsg);
            throw std::runtime_error("SQLite exec error: " + err);
        }
    }

    template <typename T = Json>
    std::optional<T> get(std::string_view sql, const std::vector<SqlValue>& params = {}) {
        auto rows = all<T>(sql, params);
        if (rows.empty()) return std::nullopt;
        return std::move(rows[0]);
    }

    template <typename T = Json>
    std::optional<T> get(std::string_view sql, const std::map<std::string, SqlValue>& params) {
        auto rows = all<T>(sql, params);
        if (rows.empty()) return std::nullopt;
        return std::move(rows[0]);
    }

    template <typename T = Json>
    std::vector<T> all(std::string_view sql, const std::vector<SqlValue>& params = {}) {
        return query_rows<T>(sql, [&](sqlite3_stmt* stmt) {
            detail::bind_params(db_, stmt, params);
        });
    }

    template <typename T = Json>
    std::vector<T> all(std::string_view sql, const std::map<std::string, SqlValue>& params) {
        return query_rows<T>(sql, [&](sqlite3_stmt* stmt) {
            detail::bind_params(db_, stmt, params);
        });
    }

    template <typename T = SqlValue>
    std::optional<T> pluck(std::string_view sql, const std::vector<SqlValue>& params = {}) {
        auto vals = pluckAll<T>(sql, params);
        if (vals.empty()) return std::nullopt;
        return std::move(vals[0]);
    }

    template <typename T = SqlValue>
    std::optional<T> pluck(std::string_view sql, const std::map<std::string, SqlValue>& params) {
        auto vals = pluckAll<T>(sql, params);
        if (vals.empty()) return std::nullopt;
        return std::move(vals[0]);
    }

    template <typename T = SqlValue>
    std::vector<T> pluckAll(std::string_view sql, const std::vector<SqlValue>& params = {}) {
        return query_pluck<T>(sql, [&](sqlite3_stmt* stmt) {
            detail::bind_params(db_, stmt, params);
        });
    }

    template <typename T = SqlValue>
    std::vector<T> pluckAll(std::string_view sql, const std::map<std::string, SqlValue>& params) {
        return query_pluck<T>(sql, [&](sqlite3_stmt* stmt) {
            detail::bind_params(db_, stmt, params);
        });
    }

    RunResult run(std::string_view sql, const std::vector<SqlValue>& params = {}) {
        return execute_run(sql, [&](sqlite3_stmt* stmt) {
            detail::bind_params(db_, stmt, params);
        });
    }

    RunResult run(std::string_view sql, const std::map<std::string, SqlValue>& params) {
        return execute_run(sql, [&](sqlite3_stmt* stmt) {
            detail::bind_params(db_, stmt, params);
        });
    }

    template <typename Fn>
    auto transaction(Fn&& fn, TransactionMode mode = TransactionMode::Immediate) {
        if (tx_depth_ == 0) {
            std::string begin = "BEGIN ";
            switch (mode) {
                case TransactionMode::Immediate: begin += "IMMEDIATE;"; break;
                case TransactionMode::Deferred:  begin += "DEFERRED;"; break;
                case TransactionMode::Exclusive: begin += "EXCLUSIVE;"; break;
            }
            exec(begin);
            tx_depth_ = 1;
            try {
                Tx tx(*this, 1);
                if constexpr (std::is_void_v<std::invoke_result_t<Fn, Tx&>>) {
                    fn(tx);
                    exec("COMMIT;");
                    tx_depth_ = 0;
                } else {
                    auto res = fn(tx);
                    exec("COMMIT;");
                    tx_depth_ = 0;
                    return res;
                }
            } catch (...) {
                try { exec("ROLLBACK;"); } catch (...) {}
                tx_depth_ = 0;
                throw;
            }
        } else {
            // Nested transaction via SAVEPOINT
            std::string sp = "sp_" + std::to_string(tx_depth_);
            exec("SAVEPOINT " + sp + ";");
            ++tx_depth_;
            try {
                Tx tx(*this, tx_depth_);
                if constexpr (std::is_void_v<std::invoke_result_t<Fn, Tx&>>) {
                    fn(tx);
                    exec("RELEASE " + sp + ";");
                    --tx_depth_;
                } else {
                    auto res = fn(tx);
                    exec("RELEASE " + sp + ";");
                    --tx_depth_;
                    return res;
                }
            } catch (...) {
                try { exec("ROLLBACK TO " + sp + ";"); } catch (...) {}
                try { exec("RELEASE " + sp + ";"); } catch (...) {}
                --tx_depth_;
                throw;
            }
        }
    }

    IterateResult iterateLimited(std::string_view sql,
                                 const IterateOptions& opts = {}) {
        return iterateLimited(sql, std::vector<SqlValue>{}, opts);
    }

    IterateResult iterateLimited(std::string_view sql,
                                 const std::vector<SqlValue>& params,
                                 const IterateOptions& opts) {
        return execute_iterate(sql, opts, [&](sqlite3* targetDb, sqlite3_stmt* stmt) {
            detail::bind_params(targetDb, stmt, params);
        });
    }

    IterateResult iterateLimited(std::string_view sql,
                                 const std::map<std::string, SqlValue>& params,
                                 const IterateOptions& opts) {
        return execute_iterate(sql, opts, [&](sqlite3* targetDb, sqlite3_stmt* stmt) {
            detail::bind_params(targetDb, stmt, params);
        });
    }

    void registerFunction(std::string_view name,
                          std::function<SqlValue(const std::vector<SqlValue>&)> fn,
                          bool deterministic = true) {
        FunctionDef def{std::string(name), std::move(fn), deterministic};
        register_function_internal(db_, def);
        functions_[std::string(name)] = std::move(def);
    }

    void backup(std::string_view destPath) {
        if (destPath != ":memory:") {
            std::filesystem::path p(destPath);
            if (p.has_parent_path()) {
                std::filesystem::create_directories(p.parent_path());
            }
        }
        sqlite3* destDb = nullptr;
        int rc = sqlite3_open(std::string(destPath).c_str(), &destDb);
        if (rc != SQLITE_OK) {
            std::string err = destDb ? sqlite3_errmsg(destDb) : "failed to open backup destination";
            if (destDb) sqlite3_close(destDb);
            throw std::runtime_error("Backup failed: " + err);
        }

        sqlite3_backup* pBackup = sqlite3_backup_init(destDb, "main", db_, "main");
        if (!pBackup) {
            std::string err = sqlite3_errmsg(destDb);
            sqlite3_close(destDb);
            throw std::runtime_error("sqlite3_backup_init failed: " + err);
        }

        rc = sqlite3_backup_step(pBackup, -1);
        (void)sqlite3_backup_finish(pBackup);
        sqlite3_close(destDb);

        if (rc != SQLITE_DONE) {
            throw std::runtime_error("Backup step did not complete cleanly: rc=" + std::to_string(rc));
        }
    }

    void close() noexcept {
        clear_statement_cache();
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
    }

private:
    struct CacheEntry {
        std::string sql;
        sqlite3_stmt* stmt = nullptr;
        bool in_use = false;
    };

    std::string path_;
    SqliteOptions options_;
    sqlite3* db_ = nullptr;
    int tx_depth_ = 0;
    std::map<std::string, FunctionDef> functions_;

    // LRU Cache
    std::list<CacheEntry> lru_list_;
    std::unordered_map<std::string, std::list<CacheEntry>::iterator> lru_map_;
    static constexpr size_t kMaxCacheSize = 256;

    void open_db() {
        if (!options_.readOnly && path_ != ":memory:") {
            std::filesystem::path p(path_);
            if (p.has_parent_path()) {
                std::filesystem::create_directories(p.parent_path());
            }
        }

        int flags = options_.readOnly ? (SQLITE_OPEN_READONLY | SQLITE_OPEN_URI)
                                      : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_URI);
        int rc = sqlite3_open_v2(path_.c_str(), &db_, flags, nullptr);
        if (rc != SQLITE_OK) {
            detail::throw_sqlite(db_, "Failed to open SQLite database at " + path_);
        }

        if (!options_.readOnly) {
            if (options_.enableDefaultPragmas) {
                exec("PRAGMA busy_timeout = 5000; PRAGMA foreign_keys = ON; PRAGMA temp_store = MEMORY;");
                if (path_ != ":memory:") {
                    try {
                        exec("PRAGMA journal_mode = WAL; PRAGMA synchronous = NORMAL;");
                    } catch (...) {}
                }
            }
            if (options_.pragmas.has_value() && !options_.pragmas->empty()) {
                exec(*options_.pragmas);
            }
        }

        if (options_.migrations.has_value()) {
            run_migrations(*options_.migrations);
        }
    }

    void run_migrations(const std::variant<std::vector<std::string>, std::map<int, std::string>>& mig) {
        std::vector<std::pair<int, std::string>> steps;
        if (std::holds_alternative<std::vector<std::string>>(mig)) {
            const auto& vec = std::get<std::vector<std::string>>(mig);
            for (size_t i = 0; i < vec.size(); ++i) {
                steps.emplace_back(static_cast<int>(i + 1), vec[i]);
            }
        } else {
            const auto& m = std::get<std::map<int, std::string>>(mig);
            for (const auto& [ver, sql] : m) {
                steps.emplace_back(ver, sql);
            }
        }

        if (steps.empty()) return;
        std::sort(steps.begin(), steps.end(), [](const auto& a, const auto& b) {
            return a.first < b.first;
        });

        int64_t currentVersion = pluck<int64_t>("PRAGMA user_version;").value_or(0);
        int minVersion = steps.front().first;
        if (currentVersion > 0 && currentVersion < minVersion) {
            throw std::runtime_error("Database user_version (" + std::to_string(currentVersion) +
                                     ") is older than the earliest supported migration (" +
                                     std::to_string(minVersion) +
                                     "). Database needs to be migrated or recreated.");
        }

        std::vector<std::pair<int, std::string>> pending;
        for (const auto& s : steps) {
            if (s.first > currentVersion) {
                pending.push_back(s);
            }
        }

        if (pending.empty()) return;

        exec("BEGIN IMMEDIATE;");
        try {
            for (const auto& [v, sql] : pending) {
                exec(sql);
                exec("PRAGMA user_version = " + std::to_string(v) + ";");
            }
            exec("COMMIT;");
        } catch (...) {
            try { exec("ROLLBACK;"); } catch (...) {}
            throw;
        }
    }

    void register_function_internal(sqlite3* targetDb, const FunctionDef& def) {
        auto fn_ptr = new std::function<SqlValue(const std::vector<SqlValue>&)>(def.fn);
        int flags = SQLITE_UTF8 | (def.deterministic ? SQLITE_DETERMINISTIC : 0);

        int rc = sqlite3_create_function_v2(
            targetDb,
            def.name.c_str(),
            -1,
            flags,
            fn_ptr,
            [](sqlite3_context* ctx, int argc, sqlite3_value** argv) {
                auto* fn = static_cast<std::function<SqlValue(const std::vector<SqlValue>&)>*>(sqlite3_user_data(ctx));
                std::vector<SqlValue> args;
                args.reserve(argc);
                for (int i = 0; i < argc; ++i) {
                    int type = sqlite3_value_type(argv[i]);
                    switch (type) {
                        case SQLITE_INTEGER:
                            args.emplace_back(sqlite3_value_int64(argv[i]));
                            break;
                        case SQLITE_FLOAT:
                            args.emplace_back(sqlite3_value_double(argv[i]));
                            break;
                        case SQLITE_TEXT: {
                            const char* txt = reinterpret_cast<const char*>(sqlite3_value_text(argv[i]));
                            args.emplace_back(std::string(txt ? txt : ""));
                            break;
                        }
                        case SQLITE_BLOB: {
                            const uint8_t* blob = reinterpret_cast<const uint8_t*>(sqlite3_value_blob(argv[i]));
                            int bytes = sqlite3_value_bytes(argv[i]);
                            args.emplace_back(std::vector<uint8_t>(blob, blob + bytes));
                            break;
                        }
                        case SQLITE_NULL:
                        default:
                            args.emplace_back(std::monostate{});
                            break;
                    }
                }
                try {
                    SqlValue res = (*fn)(args);
                    std::visit([&](auto&& v) {
                        using T = std::decay_t<decltype(v)>;
                        if constexpr (std::is_same_v<T, std::monostate>) {
                            sqlite3_result_null(ctx);
                        } else if constexpr (std::is_same_v<T, int64_t>) {
                            sqlite3_result_int64(ctx, v);
                        } else if constexpr (std::is_same_v<T, double>) {
                            sqlite3_result_double(ctx, v);
                        } else if constexpr (std::is_same_v<T, std::string>) {
                            sqlite3_result_text(ctx, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
                        } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
                            sqlite3_result_blob(ctx, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
                        } else if constexpr (std::is_same_v<T, bool>) {
                            sqlite3_result_int(ctx, v ? 1 : 0);
                        }
                    }, res);
                } catch (const std::exception& e) {
                    sqlite3_result_error(ctx, e.what(), -1);
                } catch (...) {
                    sqlite3_result_error(ctx, "Unknown exception in user function", -1);
                }
            },
            nullptr,
            nullptr,
            [](void* ptr) {
                delete static_cast<std::function<SqlValue(const std::vector<SqlValue>&)>*>(ptr);
            }
        );
        if (rc != SQLITE_OK) {
            detail::throw_sqlite(targetDb, "Failed to register custom function " + def.name);
        }
    }

    sqlite3_stmt* checkout_statement(std::string_view sql, bool& is_temporary) {
        std::string sql_str(sql);
        auto it = lru_map_.find(sql_str);
        if (it != lru_map_.end()) {
            auto list_it = it->second;
            if (!list_it->in_use) {
                list_it->in_use = true;
                lru_list_.splice(lru_list_.begin(), lru_list_, list_it);
                sqlite3_reset(list_it->stmt);
                sqlite3_clear_bindings(list_it->stmt);
                is_temporary = false;
                return list_it->stmt;
            }
        }

        // Prepare new
        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql_str.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            detail::throw_sqlite(db_, "Prepare failed for SQL: " + sql_str);
        }

        if (it != lru_map_.end() && it->second->in_use) {
            // Already active in re-entrant call; return temporary statement
            is_temporary = true;
            return stmt;
        }

        if (lru_list_.size() >= kMaxCacheSize) {
            auto& back = lru_list_.back();
            lru_map_.erase(back.sql);
            if (back.stmt) sqlite3_finalize(back.stmt);
            lru_list_.pop_back();
        }

        lru_list_.push_front(CacheEntry{sql_str, stmt, true});
        lru_map_[sql_str] = lru_list_.begin();
        is_temporary = false;
        return stmt;
    }

    void return_statement(std::string_view sql, sqlite3_stmt* stmt, bool is_temporary) noexcept {
        if (is_temporary) {
            if (stmt) sqlite3_finalize(stmt);
            return;
        }
        std::string sql_str(sql);
        auto it = lru_map_.find(sql_str);
        if (it != lru_map_.end()) {
            it->second->in_use = false;
            sqlite3_reset(stmt);
            sqlite3_clear_bindings(stmt);
        } else {
            sqlite3_finalize(stmt);
        }
    }

    void clear_statement_cache() noexcept {
        for (auto& entry : lru_list_) {
            if (entry.stmt) sqlite3_finalize(entry.stmt);
        }
        lru_list_.clear();
        lru_map_.clear();
    }

    template <typename T, typename Binder>
    std::vector<T> query_rows(std::string_view sql, Binder&& binder) {
        bool is_temp = false;
        sqlite3_stmt* stmt = checkout_statement(sql, is_temp);
        std::vector<T> results;
        try {
            binder(stmt);
            while (true) {
                int rc = sqlite3_step(stmt);
                if (rc == SQLITE_ROW) {
                    if constexpr (std::is_same_v<T, Json>) {
                        results.push_back(detail::row_to_json(stmt));
                    } else {
                        Json j = detail::row_to_json(stmt);
                        std::string s;
                        (void)glz::write_json(j, s);
                        T val{};
                        (void)glz::read_json(val, s);
                        results.push_back(std::move(val));
                    }
                } else if (rc == SQLITE_DONE) {
                    break;
                } else {
                    detail::throw_sqlite(db_, "Step failed for query");
                }
            }
        } catch (...) {
            return_statement(sql, stmt, is_temp);
            throw;
        }
        return_statement(sql, stmt, is_temp);
        return results;
    }

    template <typename T, typename Binder>
    std::vector<T> query_pluck(std::string_view sql, Binder&& binder) {
        bool is_temp = false;
        sqlite3_stmt* stmt = checkout_statement(sql, is_temp);
        std::vector<T> results;
        try {
            binder(stmt);
            while (true) {
                int rc = sqlite3_step(stmt);
                if (rc == SQLITE_ROW) {
                    results.push_back(detail::column_to_val<T>(stmt, 0));
                } else if (rc == SQLITE_DONE) {
                    break;
                } else {
                    detail::throw_sqlite(db_, "Step failed for pluck query");
                }
            }
        } catch (...) {
            return_statement(sql, stmt, is_temp);
            throw;
        }
        return_statement(sql, stmt, is_temp);
        return results;
    }

    template <typename Binder>
    RunResult execute_run(std::string_view sql, Binder&& binder) {
        bool is_temp = false;
        sqlite3_stmt* stmt = checkout_statement(sql, is_temp);
        RunResult res;
        try {
            binder(stmt);
            int rc = sqlite3_step(stmt);
            if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
                detail::throw_sqlite(db_, "Run step failed");
            }
            res.changes = sqlite3_changes(db_);
            res.lastInsertRowid = sqlite3_last_insert_rowid(db_);
        } catch (...) {
            return_statement(sql, stmt, is_temp);
            throw;
        }
        return_statement(sql, stmt, is_temp);
        return res;
    }

    template <typename Binder>
    IterateResult execute_iterate(std::string_view sql, const IterateOptions& opts, Binder&& binder) {
        sqlite3* targetDb = db_;
        sqlite3* tempDb = nullptr;
        bool resetQueryOnly = false;

        if (opts.readOnlyConnection && path_ != ":memory:") {
            int flags = SQLITE_OPEN_READONLY | SQLITE_OPEN_URI;
            int rc = sqlite3_open_v2(path_.c_str(), &tempDb, flags, nullptr);
            if (rc != SQLITE_OK) {
                detail::throw_sqlite(tempDb, "Failed to open read-only connection");
            }
            sqlite3_busy_timeout(tempDb, 5000);
            for (const auto& [_, def] : functions_) {
                register_function_internal(tempDb, def);
            }
            if (!opts.setupSql.empty()) {
                char* err = nullptr;
                sqlite3_exec(tempDb, opts.setupSql.c_str(), nullptr, nullptr, &err);
                if (err) {
                    std::string e = err;
                    sqlite3_free(err);
                    sqlite3_close(tempDb);
                    throw std::runtime_error("setupSql failed on tempDb: " + e);
                }
            }
            sqlite3_exec(tempDb, "PRAGMA query_only = ON;", nullptr, nullptr, nullptr);
            targetDb = tempDb;
        } else {
            if (!opts.setupSql.empty()) {
                exec(opts.setupSql);
            }
            if (opts.readOnlyConnection) {
                exec("PRAGMA query_only = ON;");
                resetQueryOnly = true;
            }
        }

        struct Guard {
            sqlite3* temp;
            sqlite3* target;
            bool reset;
            ~Guard() {
                if (reset && target) {
                    sqlite3_exec(target, "PRAGMA query_only = OFF;", nullptr, nullptr, nullptr);
                }
                if (temp) {
                    sqlite3_close(temp);
                }
            }
        } guard{tempDb, targetDb, resetQueryOnly};

        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(targetDb, std::string(sql).c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            detail::throw_sqlite(targetDb, "Iterate prepare failed");
        }

        IterateResult res;
        try {
            binder(targetDb, stmt);
            int cols = sqlite3_column_count(stmt);
            for (int i = 0; i < cols; ++i) {
                const char* name = sqlite3_column_name(stmt, i);
                res.columns.emplace_back(name ? name : "");
            }

            while (true) {
                if (opts.limit > 0 && res.rows.size() >= opts.limit) {
                    int next_rc = sqlite3_step(stmt);
                    if (next_rc == SQLITE_ROW) {
                        res.truncated = true;
                    }
                    break;
                }
                rc = sqlite3_step(stmt);
                if (rc == SQLITE_ROW) {
                    res.rows.push_back(detail::row_to_json(stmt));
                } else if (rc == SQLITE_DONE) {
                    break;
                } else {
                    detail::throw_sqlite(targetDb, "Iterate step failed");
                }
            }
        } catch (...) {
            sqlite3_finalize(stmt);
            throw;
        }
        sqlite3_finalize(stmt);
        return res;
    }

    void move_from(Database&& o) noexcept {
        path_ = std::move(o.path_);
        options_ = std::move(o.options_);
        db_ = o.db_;
        o.db_ = nullptr;
        tx_depth_ = o.tx_depth_;
        o.tx_depth_ = 0;
        functions_ = std::move(o.functions_);
        lru_list_ = std::move(o.lru_list_);
        lru_map_ = std::move(o.lru_map_);
    }
};

// Implementations for Tx methods
template <typename T>
inline std::optional<T> Tx::get(std::string_view sql, const std::vector<SqlValue>& params) {
    return db_.get<T>(sql, params);
}
template <typename T>
inline std::optional<T> Tx::get(std::string_view sql, const std::map<std::string, SqlValue>& params) {
    return db_.get<T>(sql, params);
}
template <typename T>
inline std::vector<T> Tx::all(std::string_view sql, const std::vector<SqlValue>& params) {
    return db_.all<T>(sql, params);
}
template <typename T>
inline std::vector<T> Tx::all(std::string_view sql, const std::map<std::string, SqlValue>& params) {
    return db_.all<T>(sql, params);
}
template <typename T>
inline std::optional<T> Tx::pluck(std::string_view sql, const std::vector<SqlValue>& params) {
    return db_.pluck<T>(sql, params);
}
template <typename T>
inline std::optional<T> Tx::pluck(std::string_view sql, const std::map<std::string, SqlValue>& params) {
    return db_.pluck<T>(sql, params);
}
template <typename T>
inline std::vector<T> Tx::pluckAll(std::string_view sql, const std::vector<SqlValue>& params) {
    return db_.pluckAll<T>(sql, params);
}
template <typename T>
inline std::vector<T> Tx::pluckAll(std::string_view sql, const std::map<std::string, SqlValue>& params) {
    return db_.pluckAll<T>(sql, params);
}
inline RunResult Tx::run(std::string_view sql, const std::vector<SqlValue>& params) {
    return db_.run(sql, params);
}
inline RunResult Tx::run(std::string_view sql, const std::map<std::string, SqlValue>& params) {
    return db_.run(sql, params);
}
inline void Tx::exec(std::string_view sql) {
    db_.exec(sql);
}
template <typename Fn>
inline auto Tx::transaction(Fn&& fn, TransactionMode mode) {
    return db_.transaction(std::forward<Fn>(fn), mode);
}

} // namespace slimr
