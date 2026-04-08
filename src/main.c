#include "executor.h"
#include "optimizer.h"
#include "parser.h"
#include "tokenizer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *program_name) {
    fprintf(stderr, "사용법:\n");
    fprintf(stderr, "  %s <sql-file>\n", program_name);
    fprintf(stderr, "  %s --data-dir <dir> <sql-file>\n", program_name);
}

static char *read_file_to_string(const char *path) {
    FILE *file;
    long length;
    size_t read_size;
    char *buffer;

    file = fopen(path, "r");
    if (file == NULL) {
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }

    length = ftell(file);
    if (length < 0) {
        fclose(file);
        return NULL;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    buffer = (char *)malloc((size_t)length + 1);
    if (buffer == NULL) {
        fclose(file);
        return NULL;
    }

    read_size = fread(buffer, 1, (size_t)length, file);
    buffer[read_size] = '\0';
    fclose(file);
    return buffer;
}

int main(int argc, char **argv) {
    const char *data_dir = "data";
    const char *sql_path = NULL;
    char error_buf[512];
    char *sql_text = NULL;
    TokenArray tokens;
    Statement statement;
    int ok = 0;

    memset(&tokens, 0, sizeof(tokens));
    memset(&statement, 0, sizeof(statement));
    memset(error_buf, 0, sizeof(error_buf));

    if (argc == 2) {
        sql_path = argv[1];
    } else if (argc == 4 && strcmp(argv[1], "--data-dir") == 0) {
        data_dir = argv[2];
        sql_path = argv[3];
    } else {
        print_usage(argv[0]);
        return 1;
    }

    sql_text = read_file_to_string(sql_path);
    if (sql_text == NULL) {
        fprintf(stderr, "SQL 파일을 읽을 수 없습니다: %s\n", sql_path);
        return 1;
    }

    if (!tokenize_sql(sql_text, &tokens, error_buf, sizeof(error_buf))) {
        fprintf(stderr, "Tokenizer 오류: %s\n", error_buf);
        goto cleanup;
    }

    if (!parse_statement(&tokens, &statement, error_buf, sizeof(error_buf))) {
        fprintf(stderr, "Parser 오류: %s\n", error_buf);
        goto cleanup;
    }

    if (!optimize_statement(&statement, error_buf, sizeof(error_buf))) {
        fprintf(stderr, "Optimizer 오류: %s\n", error_buf);
        goto cleanup;
    }

    if (!execute_statement(&statement, data_dir, error_buf, sizeof(error_buf))) {
        fprintf(stderr, "Executor 오류: %s\n", error_buf);
        goto cleanup;
    }

    ok = 1;

cleanup:
    free_statement(&statement);
    free_tokens(&tokens);
    free(sql_text);
    return ok ? 0 : 1;
}
