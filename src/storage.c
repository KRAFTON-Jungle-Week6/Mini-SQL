#include "storage.h"

#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char **items;
    size_t count;
} StringList;

static void free_string_list(StringList *list) {
    if (list == NULL) {
        return;
    }

    sql_free_string_array(list->items, list->count);
    list->items = NULL;
    list->count = 0;
}

static int make_table_path(const char *data_dir, const char *table_name, char *buffer, size_t buffer_size) {
    int written = snprintf(buffer, buffer_size, "%s/%s.tbl", data_dir, table_name);
    return written > 0 && (size_t)written < buffer_size;
}

static void trim_newline(char *line) {
    size_t length = strlen(line);
    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
        line[length - 1] = '\0';
        length--;
    }
}

static int split_csv_line(const char *line, StringList *out_list) {
    const char *start = line;
    const char *cursor = line;
    char **new_items;
    char *field;

    memset(out_list, 0, sizeof(*out_list));

    while (1) {
        if (*cursor == ',' || *cursor == '\0') {
            new_items = (char **)realloc(out_list->items, (out_list->count + 1) * sizeof(char *));
            if (new_items == NULL) {
                free_string_list(out_list);
                return 0;
            }
            out_list->items = new_items;

            field = sql_strndup(start, (size_t)(cursor - start));
            if (field == NULL) {
                free_string_list(out_list);
                return 0;
            }

            out_list->items[out_list->count] = field;
            out_list->count++;

            if (*cursor == '\0') {
                break;
            }

            cursor++;
            start = cursor;
            continue;
        }
        cursor++;
    }

    return 1;
}

static int copy_selected_columns(const StringList *header,
                                 char *const *required_columns,
                                 size_t required_count,
                                 size_t **out_indices,
                                 char ***out_columns,
                                 char *error_buf,
                                 size_t error_buf_size) {
    size_t *indices;
    char **columns;
    size_t i;
    size_t j;
    int found;

    if (required_columns == NULL || required_count == 0) {
        indices = (size_t *)calloc(header->count, sizeof(size_t));
        columns = (char **)calloc(header->count, sizeof(char *));
        if (indices == NULL || columns == NULL) {
            free(indices);
            free(columns);
            snprintf(error_buf, error_buf_size, "메모리 할당에 실패했습니다.");
            return 0;
        }

        for (i = 0; i < header->count; ++i) {
            indices[i] = i;
            columns[i] = sql_strdup(header->items[i]);
            if (columns[i] == NULL) {
                sql_free_string_array(columns, i);
                free(indices);
                snprintf(error_buf, error_buf_size, "메모리 할당에 실패했습니다.");
                return 0;
            }
        }

        *out_indices = indices;
        *out_columns = columns;
        return 1;
    }

    indices = (size_t *)calloc(required_count, sizeof(size_t));
    columns = (char **)calloc(required_count, sizeof(char *));
    if (indices == NULL || columns == NULL) {
        free(indices);
        free(columns);
        snprintf(error_buf, error_buf_size, "메모리 할당에 실패했습니다.");
        return 0;
    }

    for (i = 0; i < required_count; ++i) {
        found = 0;
        for (j = 0; j < header->count; ++j) {
            if (sql_case_equal(required_columns[i], header->items[j])) {
                indices[i] = j;
                columns[i] = sql_strdup(required_columns[i]);
                if (columns[i] == NULL) {
                    sql_free_string_array(columns, i);
                    free(indices);
                    snprintf(error_buf, error_buf_size, "메모리 할당에 실패했습니다.");
                    return 0;
                }
                found = 1;
                break;
            }
        }

        if (!found) {
            sql_free_string_array(columns, i);
            free(indices);
            snprintf(error_buf, error_buf_size, "테이블에 존재하지 않는 컬럼입니다: %s", required_columns[i]);
            return 0;
        }
    }

    *out_indices = indices;
    *out_columns = columns;
    return 1;
}

static int append_row(TableData *table, char **values) {
    char ***new_rows;

    new_rows = (char ***)realloc(table->rows, (table->row_count + 1) * sizeof(char **));
    if (new_rows == NULL) {
        return 0;
    }

    table->rows = new_rows;
    table->rows[table->row_count] = values;
    table->row_count++;
    return 1;
}

int storage_load_table(const char *data_dir,
                       const char *table_name,
                       char *const *required_columns,
                       size_t required_count,
                       TableData *out_table,
                       char *error_buf,
                       size_t error_buf_size) {
    char path[1024];
    FILE *file;
    char line[4096];
    StringList header = {0};
    StringList row = {0};
    size_t *selected_indices = NULL;
    size_t selected_count;
    size_t row_index;
    char **selected_row = NULL;

    memset(out_table, 0, sizeof(*out_table));

    if (!make_table_path(data_dir, table_name, path, sizeof(path))) {
        snprintf(error_buf, error_buf_size, "테이블 경로 생성에 실패했습니다.");
        return 0;
    }

    file = fopen(path, "r");
    if (file == NULL) {
        snprintf(error_buf, error_buf_size, "테이블 파일을 열 수 없습니다: %s", path);
        return 0;
    }

    if (fgets(line, sizeof(line), file) == NULL) {
        fclose(file);
        snprintf(error_buf, error_buf_size, "테이블 파일이 비어 있습니다: %s", path);
        return 0;
    }

    trim_newline(line);
    if (!split_csv_line(line, &header)) {
        fclose(file);
        snprintf(error_buf, error_buf_size, "헤더 파싱에 실패했습니다.");
        return 0;
    }

    if (!copy_selected_columns(&header,
                               required_columns,
                               required_count,
                               &selected_indices,
                               &out_table->columns,
                               error_buf,
                               error_buf_size)) {
        fclose(file);
        free_string_list(&header);
        return 0;
    }
    out_table->column_count = (required_columns == NULL || required_count == 0) ? header.count : required_count;
    selected_count = out_table->column_count;

    while (fgets(line, sizeof(line), file) != NULL) {
        trim_newline(line);
        if (line[0] == '\0') {
            continue;
        }

        if (!split_csv_line(line, &row)) {
            fclose(file);
            free(selected_indices);
            free_string_list(&header);
            storage_free_table(out_table);
            snprintf(error_buf, error_buf_size, "데이터 행 파싱에 실패했습니다.");
            return 0;
        }

        if (row.count != header.count) {
            fclose(file);
            free(selected_indices);
            free_string_list(&header);
            free_string_list(&row);
            storage_free_table(out_table);
            snprintf(error_buf, error_buf_size, "행의 컬럼 수가 스키마와 다릅니다.");
            return 0;
        }

        selected_row = (char **)calloc(selected_count, sizeof(char *));
        if (selected_row == NULL) {
            fclose(file);
            free(selected_indices);
            free_string_list(&header);
            free_string_list(&row);
            storage_free_table(out_table);
            snprintf(error_buf, error_buf_size, "메모리 할당에 실패했습니다.");
            return 0;
        }

        for (row_index = 0; row_index < selected_count; ++row_index) {
            selected_row[row_index] = sql_strdup(row.items[selected_indices[row_index]]);
            if (selected_row[row_index] == NULL) {
                fclose(file);
                free(selected_indices);
                free_string_list(&header);
                free_string_list(&row);
                sql_free_string_array(selected_row, row_index);
                storage_free_table(out_table);
                snprintf(error_buf, error_buf_size, "메모리 할당에 실패했습니다.");
                return 0;
            }
        }

        if (!append_row(out_table, selected_row)) {
            fclose(file);
            free(selected_indices);
            free_string_list(&header);
            free_string_list(&row);
            sql_free_string_array(selected_row, selected_count);
            storage_free_table(out_table);
            snprintf(error_buf, error_buf_size, "행 저장에 실패했습니다.");
            return 0;
        }

        selected_row = NULL;
        free_string_list(&row);
    }

    fclose(file);
    free(selected_indices);
    free_string_list(&header);
    return 1;
}

static int read_header(const char *path, StringList *header, char *error_buf, size_t error_buf_size) {
    FILE *file = fopen(path, "r");
    char line[4096];

    if (file == NULL) {
        snprintf(error_buf, error_buf_size, "테이블 파일을 열 수 없습니다: %s", path);
        return 0;
    }

    if (fgets(line, sizeof(line), file) == NULL) {
        fclose(file);
        snprintf(error_buf, error_buf_size, "테이블 파일이 비어 있습니다: %s", path);
        return 0;
    }

    fclose(file);
    trim_newline(line);
    if (!split_csv_line(line, header)) {
        snprintf(error_buf, error_buf_size, "헤더 파싱에 실패했습니다.");
        return 0;
    }

    return 1;
}

static int validate_literal_for_storage(const Literal *literal, char *error_buf, size_t error_buf_size) {
    if (strchr(literal->text, ',') != NULL || strchr(literal->text, '\n') != NULL || strchr(literal->text, '\r') != NULL) {
        snprintf(error_buf,
                 error_buf_size,
                 "현재 저장 포맷에서는 콤마/개행이 포함된 값은 지원하지 않습니다: %s",
                 literal->text);
        return 0;
    }
    return 1;
}

int storage_append_row(const char *data_dir,
                       const char *table_name,
                       const Literal *values,
                       size_t value_count,
                       char *error_buf,
                       size_t error_buf_size) {
    char path[1024];
    FILE *file;
    StringList header = {0};
    size_t index;

    if (!make_table_path(data_dir, table_name, path, sizeof(path))) {
        snprintf(error_buf, error_buf_size, "테이블 경로 생성에 실패했습니다.");
        return 0;
    }

    if (!read_header(path, &header, error_buf, error_buf_size)) {
        return 0;
    }

    if (header.count != value_count) {
        free_string_list(&header);
        snprintf(error_buf,
                 error_buf_size,
                 "INSERT 값 개수(%zu)와 테이블 컬럼 수(%zu)가 다릅니다.",
                 value_count,
                 header.count);
        return 0;
    }

    file = fopen(path, "a");
    if (file == NULL) {
        free_string_list(&header);
        snprintf(error_buf, error_buf_size, "테이블 파일을 append 모드로 열 수 없습니다: %s", path);
        return 0;
    }

    for (index = 0; index < value_count; ++index) {
        if (!validate_literal_for_storage(&values[index], error_buf, error_buf_size)) {
            fclose(file);
            free_string_list(&header);
            return 0;
        }

        if (index > 0) {
            fputc(',', file);
        }
        fputs(values[index].text, file);
    }
    fputc('\n', file);

    fclose(file);
    free_string_list(&header);
    return 1;
}

int storage_find_column_index(const TableData *table, const char *column_name) {
    size_t index;

    for (index = 0; index < table->column_count; ++index) {
        if (sql_case_equal(table->columns[index], column_name)) {
            return (int)index;
        }
    }
    return -1;
}

void storage_free_table(TableData *table) {
    size_t row_index;
    size_t column_count;

    if (table == NULL) {
        return;
    }

    column_count = table->column_count;
    sql_free_string_array(table->columns, table->column_count);
    table->columns = NULL;
    table->column_count = 0;

    for (row_index = 0; row_index < table->row_count; ++row_index) {
        sql_free_string_array(table->rows[row_index], column_count);
    }
    free(table->rows);
    table->rows = NULL;
    table->row_count = 0;
}
