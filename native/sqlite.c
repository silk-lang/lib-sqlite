/* SQLite's amalgamation and the entry points that read its internals, as one translation unit, so
 * the public value accessors these call are inlined. */
#include "sqlite3.c"

/* Whether every byte of text is below 0x80. */
static int silkSqliteAscii(const unsigned char *text, int length) {
    int i = 0;
    for (; i + 8 <= length; i += 8) {
        u64 word;
        memcpy(&word, text + i, 8);
        if (word & 0x8080808080808080ULL) return 0;
    }
    unsigned char bits = 0;
    for (; i < length; i++) bits |= text[i];
    return bits < 0x80;
}

/* Reads the first `count` columns of the row that sqlite3_step just returned, as the sqlite3_column_*
 * accessors would. meta[2 * i] is the fundamental type (SQLITE_INTEGER .. SQLITE_NULL), with 0x100 set
 * for text whose bytes are all ASCII, and meta[2 * i + 1] the length in bytes of text or a blob. By
 * type, integers[i], reals[i] or data[i] holds the value; the other two are left alone. Text is UTF-8 and stays valid
 * until the statement is stepped, reset or finalized. Returns SQLITE_OK, SQLITE_NOMEM when text or a
 * blob could not be produced, or SQLITE_MISUSE when there is no current row or too few columns. */
int silk_sqlite_row(sqlite3_stmt *statement, int count, sqlite3_int64 *integers, double *reals,
                    const void **data, int *meta) {
    Vdbe *vdbe = (Vdbe *)statement;
    if (vdbe == 0) return SQLITE_MISUSE;
    sqlite3 *db = vdbe->db;
    int code = SQLITE_OK;
    sqlite3_mutex_enter(db->mutex);
    Mem *row = vdbe->pResultRow;
    if (row == 0 || count < 0 || count > vdbe->nResColumn) {
        code = SQLITE_MISUSE;
        count = 0;
    }
    for (int i = 0; i < count; i++) {
        sqlite3_value *value = &row[i];
        int length = 0;
        /* The common representations are read in place; anything else (text in another
         * encoding or without its terminator, zero-filled blobs, numbers kept as text) goes
         * through the accessors, which convert. */
        u16 flags = value->flags;
        if ((flags & (MEM_Int | MEM_Str | MEM_Blob)) == MEM_Int) {
            integers[i] = value->u.i;
            meta[2 * i] = SQLITE_INTEGER;
            meta[2 * i + 1] = 0;
            continue;
        }
        if ((flags & (MEM_Real | MEM_Str | MEM_Blob)) == MEM_Real) {
            reals[i] = value->u.r;
            meta[2 * i] = SQLITE_FLOAT;
            meta[2 * i + 1] = 0;
            continue;
        }
        if ((flags & (MEM_Str | MEM_Term | MEM_Zero | MEM_Int | MEM_Real | MEM_IntReal | MEM_Blob)) == (MEM_Str | MEM_Term)
            && value->enc == SQLITE_UTF8) {
            data[i] = value->z;
            meta[2 * i] = silkSqliteAscii((const unsigned char *)value->z, value->n) ? SQLITE_TEXT | 0x100 : SQLITE_TEXT;
            meta[2 * i + 1] = value->n;
            continue;
        }
        int type = sqlite3_value_type(value);
        if (type == SQLITE_INTEGER) {
            integers[i] = sqlite3_value_int64(value);
        } else if (type == SQLITE_FLOAT) {
            reals[i] = sqlite3_value_double(value);
        } else if (type == SQLITE_TEXT) {
            const unsigned char *text = sqlite3_value_text(value);
            if (text == 0) { code = SQLITE_NOMEM; break; }
            length = sqlite3_value_bytes(value);
            data[i] = text;
            if (silkSqliteAscii(text, length)) type |= 0x100;
        } else if (type == SQLITE_BLOB) {
            const void *blob = sqlite3_value_blob(value);
            length = sqlite3_value_bytes(value);
            if (blob == 0 && length != 0) { code = SQLITE_NOMEM; break; }
            data[i] = blob;
        }
        meta[2 * i] = type;
        meta[2 * i + 1] = length;
    }
    /* As columnMallocFailure after each sqlite3_column_* call. */
    vdbe->rc = sqlite3ApiExit(db, vdbe->rc);
    sqlite3_mutex_leave(db->mutex);
    return code;
}
