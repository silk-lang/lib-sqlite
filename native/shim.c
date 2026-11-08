/* Entry points for Silk where the plain SQLite C API needs a NUL-terminated string, a destructor
 * sentinel or a loop over several statements. Everything else is called directly. */
#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>

int silk_sqlite_open(const char *path, size_t length, int flags, sqlite3 **out) {
    char *terminated = malloc(length + 1);
    if (terminated == NULL) {
        *out = NULL;
        return SQLITE_NOMEM;
    }
    if (length > 0) memcpy(terminated, path, length);
    terminated[length] = 0;
    int code = sqlite3_open_v2(terminated, out, flags, NULL);
    free(terminated);
    if (code == SQLITE_OK) sqlite3_extended_result_codes(*out, 1);
    return code;
}

size_t silk_sqlite_strlen(const char *text) {
    return text == NULL ? 0 : strlen(text);
}

int silk_sqlite_prepare(sqlite3 *db, const char *sql, size_t length, unsigned int flags,
                        sqlite3_stmt **out, size_t *consumed) {
    const char *tail = sql;
    if (length > 0x7ffffffe) {
        *out = NULL;
        *consumed = 0;
        return SQLITE_TOOBIG;
    }
    int code = sqlite3_prepare_v3(db, length == 0 ? "" : sql, (int)length, flags, out, &tail);
    *consumed = (length == 0 || tail == NULL) ? length : (size_t)(tail - sql);
    return code;
}

/* Runs every statement of `sql` in order, stepping each to completion and discarding rows. */
int silk_sqlite_exec(sqlite3 *db, const char *sql, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        sqlite3_stmt *statement = NULL;
        size_t consumed = 0;
        int code = silk_sqlite_prepare(db, sql + offset, length - offset, 0, &statement, &consumed);
        if (code != SQLITE_OK) return code;
        if (consumed == 0) consumed = length - offset;
        offset += consumed;
        if (statement == NULL) continue; /* whitespace or a comment */
        do {
            code = sqlite3_step(statement);
        } while (code == SQLITE_ROW);
        if (code != SQLITE_DONE) {
            sqlite3_finalize(statement);
            return code;
        }
        code = sqlite3_finalize(statement);
        if (code != SQLITE_OK) return code;
    }
    return SQLITE_OK;
}

int silk_sqlite_bind_text(sqlite3_stmt *statement, int index, const char *text, size_t length) {
    /* A null pointer would bind SQL NULL, so empty text binds a literal empty string. */
    return sqlite3_bind_text64(statement, index, length == 0 ? "" : text, length, SQLITE_TRANSIENT,
                               SQLITE_UTF8);
}

int silk_sqlite_bind_blob(sqlite3_stmt *statement, int index, const void *bytes, size_t length) {
    /* A null pointer would bind SQL NULL, so an empty blob binds a zero-length blob. */
    if (length == 0) return sqlite3_bind_zeroblob(statement, index, 0);
    return sqlite3_bind_blob64(statement, index, bytes, length, SQLITE_TRANSIENT);
}
