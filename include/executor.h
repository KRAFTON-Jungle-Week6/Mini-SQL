#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "ast.h"

#include <stddef.h>

int execute_statement(const Statement *statement, const char *data_dir, char *error_buf, size_t error_buf_size);

#endif
