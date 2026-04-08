#include "executor.h"

#include "storage.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char **items;
    size_t count;
} RequiredColumnList;

static void free_required_columns(RequiredColumnList *columns) {
    if (columns == NULL) {
        return;
    }

    sql_free_string_array(columns->items, columns->count);
    columns->items = NULL;
    columns->count = 0;
}

static int append_required_column(RequiredColumnList *columns,
                                  const char *column_name,
                                  char *error_buf,
                                  size_t error_buf_size) {
    char **new_items;
    char *copy;

    if (sql_string_array_contains(columns->items, columns->count, column_name)) {
        return 1;
    }

    new_items = (char **)realloc(columns->items, (columns->count + 1) * sizeof(char *));
    if (new_items == NULL) {
        snprintf(error_buf, error_buf_size, "메모리 할당에 실패했습니다.");
        return 0;
    }
    columns->items = new_items;

    copy = sql_strdup(column_name);
    if (copy == NULL) {
        snprintf(error_buf, error_buf_size, "메모리 할당에 실패했습니다.");
        return 0;
    }

    columns->items[columns->count] = copy;
    columns->count++;
    return 1;
}

static int build_required_columns(const SelectStatement *select_stmt,
                                  RequiredColumnList *required_columns,
                                  char *error_buf,
                                  size_t error_buf_size) {
    size_t index;

    memset(required_columns, 0, sizeof(*required_columns));

    if (select_stmt->columns.is_star) {
        return 1;
    }

    for (index = 0; index < select_stmt->columns.count; ++index) {
        if (!append_required_column(required_columns,
                                    select_stmt->columns.items[index],
                                    error_buf,
                                    error_buf_size)) {
            free_required_columns(required_columns);
            return 0;
        }
    }

    if (select_stmt->has_where) {
        if (!append_required_column(required_columns,
                                    select_stmt->where.column_name,
                                    error_buf,
                                    error_buf_size)) {
            free_required_columns(required_columns);
            return 0;
        }
    }

    return 1;
}

static int row_matches_filter(const TableData *table, size_t row_index, const SelectStatement *select_stmt) {
    int column_index;

    if (!select_stmt->has_where) {
        return 1;
    }

    column_index = storage_find_column_index(table, select_stmt->where.column_name);
    if (column_index < 0) {
        return 0;
    }

    return strcmp(table->rows[row_index][column_index], select_stmt->where.value.text) == 0;
}

static void print_csv_line(char *const *values, size_t count) {
    size_t index;

    for (index = 0; index < count; ++index) {
        if (index > 0) {
            printf(",");
        }
        printf("%s", values[index]);
    }
    printf("\n");
}

static int execute_select(const SelectStatement *select_stmt,
                          const char *data_dir,
                          char *error_buf,
                          size_t error_buf_size) {
    TableData table;
    RequiredColumnList required_columns;
    size_t row_index;
    size_t output_count;
    size_t column_index;
    char **output_columns;

    memset(&table, 0, sizeof(table));
    memset(&required_columns, 0, sizeof(required_columns));

    if (!build_required_columns(select_stmt, &required_columns, error_buf, error_buf_size)) {
        return 0;
    }

    if (!storage_load_table(data_dir,
                            select_stmt->table_name,
                            required_columns.items,
                            required_columns.count,
                            &table,
                            error_buf,
                            error_buf_size)) {
        free_required_columns(&required_columns);
        return 0;
    }

    free_required_columns(&required_columns);

    if (select_stmt->columns.is_star) {
        print_csv_line(table.columns, table.column_count);
        for (row_index = 0; row_index < table.row_count; ++row_index) {
            if (row_matches_filter(&table, row_index, select_stmt)) {
                print_csv_line(table.rows[row_index], table.column_count);
            }
        }
        storage_free_table(&table);
        return 1;
    }

    output_columns = select_stmt->columns.items;
    output_count = select_stmt->columns.count;
    print_csv_line(output_columns, output_count);

    for (row_index = 0; row_index < table.row_count; ++row_index) {
        if (!row_matches_filter(&table, row_index, select_stmt)) {
            continue;
        }

        for (column_index = 0; column_index < output_count; ++column_index) {
            int selected_index = storage_find_column_index(&table, output_columns[column_index]);
            if (selected_index < 0) {
                storage_free_table(&table);
                snprintf(error_buf,
                         error_buf_size,
                         "실행 중 존재하지 않는 컬럼을 찾았습니다: %s",
                         output_columns[column_index]);
                return 0;
            }

            if (column_index > 0) {
                printf(",");
            }
            printf("%s", table.rows[row_index][selected_index]);
        }
        printf("\n");
    }

    storage_free_table(&table);
    return 1;
}

static int execute_insert(const InsertStatement *insert_stmt,
                          const char *data_dir,
                          char *error_buf,
                          size_t error_buf_size) {
    if (!storage_append_row(data_dir,
                            insert_stmt->table_name,
                            insert_stmt->values,
                            insert_stmt->value_count,
                            error_buf,
                            error_buf_size)) {
        return 0;
    }

    printf("Inserted 1 row into %s\n", insert_stmt->table_name);
    return 1;
}

int execute_statement(const Statement *statement, const char *data_dir, char *error_buf, size_t error_buf_size) {
    if (statement == NULL) {
        snprintf(error_buf, error_buf_size, "실행할 문장이 없습니다.");
        return 0;
    }

    if (statement->type == AST_INSERT_STATEMENT) {
        return execute_insert(&statement->as.insert_stmt, data_dir, error_buf, error_buf_size);
    }

    return execute_select(&statement->as.select_stmt, data_dir, error_buf, error_buf_size);
}
