
#include "redismodule.h"
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

#define DB_FILE "/data/archive.db"

// Initialize SQLite DB and create table if not exists
int init_db(sqlite3 **db) {
    if (sqlite3_open(DB_FILE, db) != SQLITE_OK) return REDISMODULE_ERR;

    const char *sql = "CREATE TABLE IF NOT EXISTS archive (key TEXT PRIMARY KEY, value TEXT);";
    char *errmsg = NULL;
    if (sqlite3_exec(*db, sql, NULL, NULL, &errmsg) != SQLITE_OK) {
        sqlite3_free(errmsg);
        sqlite3_close(*db);
        return REDISMODULE_ERR;
    }
    return REDISMODULE_OK;
}

// ARCHIVE.SAVE key
int ArchiveSaveCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    if (argc != 2) return RedisModule_WrongArity(ctx);

    RedisModule_AutoMemory(ctx);

    RedisModuleKey *key = RedisModule_OpenKey(ctx, argv[1], REDISMODULE_READ);
    if (RedisModule_KeyType(key) != REDISMODULE_KEYTYPE_STRING)
        return RedisModule_ReplyWithError(ctx, "ERR key is not a string");

    size_t len;
    const char *val = RedisModule_StringDMA(key, &len, REDISMODULE_READ);

    sqlite3 *db;
    if (init_db(&db) != REDISMODULE_OK)
        return RedisModule_ReplyWithError(ctx, "ERR opening SQLite");

    const char *sql = "REPLACE INTO archive (key, value) VALUES (?, ?);";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return RedisModule_ReplyWithError(ctx, "ERR prepare failed");
    }

    const char *key_str = RedisModule_StringPtrLen(argv[1], NULL);
    sqlite3_bind_text(stmt, 1, key_str, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, val, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    if (rc != SQLITE_DONE)
        return RedisModule_ReplyWithError(ctx, "ERR SQLite insert failed");

    RedisModule_Call(ctx, "DEL", "s", argv[1]);
    return RedisModule_ReplyWithSimpleString(ctx, "OK");
}

// ARCHIVE.GET key
int ArchiveGetCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    if (argc != 2) return RedisModule_WrongArity(ctx);

    RedisModule_AutoMemory(ctx);

    sqlite3 *db;
    if (init_db(&db) != REDISMODULE_OK)
        return RedisModule_ReplyWithError(ctx, "ERR opening SQLite");

    const char *sql = "SELECT value FROM archive WHERE key = ?;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        return RedisModule_ReplyWithError(ctx, "ERR prepare failed");
    }

    const char *key_str = RedisModule_StringPtrLen(argv[1], NULL);
    sqlite3_bind_text(stmt, 1, key_str, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const unsigned char *val = sqlite3_column_text(stmt, 0);
        RedisModule_ReplyWithStringBuffer(ctx, (const char *)val, strlen((const char *)val));
    } else {
        RedisModule_ReplyWithNull(ctx);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return REDISMODULE_OK;
}

// ARCHIVE.SWEEP pattern timestamp_field max_timestamp
int ArchiveSweepCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    if (argc != 4) return RedisModule_WrongArity(ctx);

    RedisModule_AutoMemory(ctx);
    long long max_ts;
    if (RedisModule_StringToLongLong(argv[3], &max_ts) != REDISMODULE_OK)
        return RedisModule_ReplyWithError(ctx, "ERR invalid timestamp");

    RedisModuleCallReply *keys = RedisModule_Call(ctx, "KEYS", "s", argv[1]);
    size_t count = RedisModule_CallReplyLength(keys);
    int archived = 0;

    for (size_t i = 0; i < count; i++) {
        RedisModuleCallReply *key = RedisModule_CallReplyArrayElement(keys, i);
        RedisModuleString *key_str = RedisModule_CreateStringFromCallReply(key);

        RedisModuleKey *rk = RedisModule_OpenKey(ctx, key_str, REDISMODULE_READ);
        if (RedisModule_KeyType(rk) != REDISMODULE_KEYTYPE_STRING) continue;

        size_t len;
        const char *val = RedisModule_StringDMA(rk, &len, REDISMODULE_READ);
        cJSON *json = cJSON_ParseWithLength(val, len);
        if (!json) continue;

        const char *field = RedisModule_StringPtrLen(argv[2], NULL);
        cJSON *ts_node = cJSON_GetObjectItemCaseSensitive(json, field);
        if (!cJSON_IsNumber(ts_node)) {
            cJSON_Delete(json);
            continue;
        }

        if ((long long)ts_node->valuedouble <= max_ts) {
            RedisModule_Call(ctx, "ARCHIVE.SAVE", "s", key_str);
            archived++;
        }

        cJSON_Delete(json);
    }

    RedisModule_ReplyWithLongLong(ctx, archived);
    return REDISMODULE_OK;
}

// Module entry point
int RedisModule_OnLoad(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    if (RedisModule_Init(ctx, "archive", 1, REDISMODULE_APIVER_1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx, "archive.save", ArchiveSaveCommand, "write", 1, 1, 1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx, "archive.get", ArchiveGetCommand, "readonly", 1, 1, 1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx, "archive.sweep", ArchiveSweepCommand, "write", 1, 1, 1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    return REDISMODULE_OK;
}
