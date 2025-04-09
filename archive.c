
#include "redismodule.h"
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cJSON.h"

#define DB_FILE "archive.db"
/**
 * Parse ISO8601 timestamp string to time_t
 * Supports formats:
 * - "2025-04-09T05:20:55+09:00"
 * - "2025-04-09T05:20:55.548239+00:00"
 * - "2025-04-09T05:20:55Z"
 * - "2025-04-09T05:20:55"
 */
time_t parse_iso8601(const char *timestamp_str) {
    struct tm tm = {0};
    char buf[32];
    int tz_hour = 0, tz_min = 0;
    char sign = '+';

    // Copy timestamp to buffer with null termination
    strncpy(buf, timestamp_str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    // Truncate at fractional seconds
    char *dot = strchr(buf, '.');
    if (dot) *dot = '\0';

    // Find timezone indicator
    char *tz = strchr(timestamp_str, '+');
    if (!tz) tz = strchr(timestamp_str, '-');
    if (!tz) tz = strchr(timestamp_str, 'Z');

    // Parse timezone offset if present
    if (tz && *tz != 'Z') {
        sign = *tz;
        sscanf(tz + 1, "%2d:%2d", &tz_hour, &tz_min);
    }

    // Convert to time structure
    strptime(buf, "%Y-%m-%dT%H:%M:%S", &tm);
    time_t t = mktime(&tm);

    // Apply timezone adjustment
    int offset = (tz_hour * 3600 + tz_min * 60);
    if (sign == '+') {
        t -= offset;
    } else if (sign == '-') {
        t += offset;
    }

    return t;
}

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
        long long ts_value = -1;
        if (cJSON_IsNumber(ts_node)) {
            ts_value = (long long)ts_node->valuedouble;
        } else if (cJSON_IsString(ts_node)) {
            ts_value = parse_iso8601(ts_node->valuestring);
        }

        if (ts_value == -1) {
            cJSON_Delete(json);
            continue;
        }

        if (ts_value <= max_ts) {
            RedisModule_Call(ctx, "ARCHIVE.SAVE", "s", key_str);
            archived++;
        }

        cJSON_Delete(json);
    }

    RedisModule_ReplyWithLongLong(ctx, archived);
    return REDISMODULE_OK;
}

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
