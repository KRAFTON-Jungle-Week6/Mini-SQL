#ifndef STORAGE_H
#define STORAGE_H

#include "ast.h"

#include <stddef.h>

typedef struct {
    char **columns;
    size_t column_count;
    char ***rows;
    size_t row_count;
} TableData;

int storage_load_table(const char *data_dir,
                       const char *table_name,
                       char *const *required_columns,
                       size_t required_count,
                       TableData *out_table,
                       char *error_buf,
                       size_t error_buf_size);

int storage_append_row(const char *data_dir,
                       const char *table_name,
                       const Literal *values,
                       size_t value_count,
                       char *error_buf,
                       size_t error_buf_size);

int storage_find_column_index(const TableData *table, const char *column_name);
void storage_free_table(TableData *table);

#endif
