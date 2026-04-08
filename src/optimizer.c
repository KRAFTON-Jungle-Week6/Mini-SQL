#include "optimizer.h"

#include "util.h"

#include <stdio.h>
#include <stdlib.h>

static void remove_column_at(ColumnList *columns, size_t index) {
    size_t cursor;

    free(columns->items[index]);
    for (cursor = index; cursor + 1 < columns->count; ++cursor) {
        columns->items[cursor] = columns->items[cursor + 1];
    }
    columns->count--;
}

static int deduplicate_projection_columns(ColumnList *columns) {
    size_t left;
    size_t right;

    if (columns->is_star) {
        return 1;
    }

    for (left = 0; left < columns->count; ++left) {
        right = left + 1;
        while (right < columns->count) {
            if (sql_case_equal(columns->items[left], columns->items[right])) {
                remove_column_at(columns, right);
                continue;
            }
            right++;
        }
    }

    return 1;
}

int optimize_statement(Statement *statement, char *error_buf, size_t error_buf_size) {
    if (statement == NULL) {
        snprintf(error_buf, error_buf_size, "최적화할 문장이 없습니다.");
        return 0;
    }

    if (statement->type != AST_SELECT_STATEMENT) {
        return 1;
    }

    if (!deduplicate_projection_columns(&statement->as.select_stmt.columns)) {
        snprintf(error_buf, error_buf_size, "projection 최적화 중 실패했습니다.");
        return 0;
    }

    return 1;
}
