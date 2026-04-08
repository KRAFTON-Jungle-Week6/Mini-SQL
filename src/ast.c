#include "ast.h"

#include "util.h"

#include <stdlib.h>

void free_literal(Literal *literal) {
    if (literal == NULL) {
        return;
    }

    free(literal->text);
    literal->text = NULL;
}

Literal clone_literal(const Literal *literal) {
    Literal copy;

    copy.type = literal->type;
    copy.text = sql_strdup(literal->text);
    return copy;
}

static void free_column_list(ColumnList *columns) {
    if (columns == NULL) {
        return;
    }

    sql_free_string_array(columns->items, columns->count);
    columns->items = NULL;
    columns->count = 0;
}

void free_statement(Statement *statement) {
    size_t index;

    if (statement == NULL) {
        return;
    }

    switch (statement->type) {
        case AST_SELECT_STATEMENT:
            free(statement->as.select_stmt.table_name);
            free_column_list(&statement->as.select_stmt.columns);
            if (statement->as.select_stmt.has_where) {
                free(statement->as.select_stmt.where.column_name);
                free_literal(&statement->as.select_stmt.where.value);
            }
            break;
        case AST_INSERT_STATEMENT:
            free(statement->as.insert_stmt.table_name);
            for (index = 0; index < statement->as.insert_stmt.value_count; ++index) {
                free_literal(&statement->as.insert_stmt.values[index]);
            }
            free(statement->as.insert_stmt.values);
            break;
    }
}
