#include "tokenizer.h"

#include "util.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int append_token(TokenArray *tokens, TokenType type, char *lexeme, size_t position) {
    Token *new_items;

    if (tokens->count == tokens->capacity) {
        size_t new_capacity = tokens->capacity == 0 ? 16 : tokens->capacity * 2;
        new_items = (Token *)realloc(tokens->items, new_capacity * sizeof(Token));
        if (new_items == NULL) {
            free(lexeme);
            return 0;
        }
        tokens->items = new_items;
        tokens->capacity = new_capacity;
    }

    tokens->items[tokens->count].type = type;
    tokens->items[tokens->count].lexeme = lexeme;
    tokens->items[tokens->count].position = position;
    tokens->count++;
    return 1;
}

static TokenType identifier_type(const char *text) {
    if (sql_case_equal(text, "INSERT")) {
        return TOKEN_INSERT;
    }
    if (sql_case_equal(text, "INTO")) {
        return TOKEN_INTO;
    }
    if (sql_case_equal(text, "VALUES")) {
        return TOKEN_VALUES;
    }
    if (sql_case_equal(text, "SELECT")) {
        return TOKEN_SELECT;
    }
    if (sql_case_equal(text, "FROM")) {
        return TOKEN_FROM;
    }
    if (sql_case_equal(text, "WHERE")) {
        return TOKEN_WHERE;
    }
    return TOKEN_IDENTIFIER;
}

static int tokenize_identifier(const char *input,
                               size_t *index,
                               TokenArray *tokens,
                               char *error_buf,
                               size_t error_buf_size) {
    size_t start = *index;
    char *lexeme;
    TokenType type;

    while (isalnum((unsigned char)input[*index]) || input[*index] == '_') {
        (*index)++;
    }

    lexeme = sql_strndup(input + start, *index - start);
    if (lexeme == NULL) {
        snprintf(error_buf, error_buf_size, "메모리 할당에 실패했습니다.");
        return 0;
    }

    type = identifier_type(lexeme);
    return append_token(tokens, type, lexeme, start);
}

static int tokenize_number(const char *input,
                           size_t *index,
                           TokenArray *tokens,
                           char *error_buf,
                           size_t error_buf_size) {
    size_t start = *index;
    char *lexeme;

    if (input[*index] == '-') {
        (*index)++;
    }
    while (isdigit((unsigned char)input[*index])) {
        (*index)++;
    }

    lexeme = sql_strndup(input + start, *index - start);
    if (lexeme == NULL) {
        snprintf(error_buf, error_buf_size, "메모리 할당에 실패했습니다.");
        return 0;
    }

    return append_token(tokens, TOKEN_NUMBER, lexeme, start);
}

static int tokenize_string(const char *input,
                           size_t *index,
                           TokenArray *tokens,
                           char *error_buf,
                           size_t error_buf_size) {
    size_t start = *index;
    size_t content_start;
    char *lexeme;

    (*index)++;
    content_start = *index;
    while (input[*index] != '\0' && input[*index] != '\'') {
        (*index)++;
    }

    if (input[*index] != '\'') {
        snprintf(error_buf, error_buf_size, "문자열 리터럴이 닫히지 않았습니다. 위치=%zu", start);
        return 0;
    }

    lexeme = sql_strndup(input + content_start, *index - content_start);
    if (lexeme == NULL) {
        snprintf(error_buf, error_buf_size, "메모리 할당에 실패했습니다.");
        return 0;
    }

    (*index)++;
    return append_token(tokens, TOKEN_STRING, lexeme, start);
}

int tokenize_sql(const char *input, TokenArray *out_tokens, char *error_buf, size_t error_buf_size) {
    size_t index = 0;

    memset(out_tokens, 0, sizeof(*out_tokens));

    while (input[index] != '\0') {
        char ch = input[index];
        if (isspace((unsigned char)ch)) {
            index++;
            continue;
        }

        if (isalpha((unsigned char)ch) || ch == '_') {
            if (!tokenize_identifier(input, &index, out_tokens, error_buf, error_buf_size)) {
                free_tokens(out_tokens);
                return 0;
            }
            continue;
        }

        if (isdigit((unsigned char)ch) || (ch == '-' && isdigit((unsigned char)input[index + 1]))) {
            if (!tokenize_number(input, &index, out_tokens, error_buf, error_buf_size)) {
                free_tokens(out_tokens);
                return 0;
            }
            continue;
        }

        if (ch == '\'') {
            if (!tokenize_string(input, &index, out_tokens, error_buf, error_buf_size)) {
                free_tokens(out_tokens);
                return 0;
            }
            continue;
        }

        switch (ch) {
            case '*':
                if (!append_token(out_tokens, TOKEN_STAR, sql_strdup("*"), index)) {
                    snprintf(error_buf, error_buf_size, "메모리 할당에 실패했습니다.");
                    free_tokens(out_tokens);
                    return 0;
                }
                index++;
                break;
            case ',':
                if (!append_token(out_tokens, TOKEN_COMMA, sql_strdup(","), index)) {
                    snprintf(error_buf, error_buf_size, "메모리 할당에 실패했습니다.");
                    free_tokens(out_tokens);
                    return 0;
                }
                index++;
                break;
            case '(':
                if (!append_token(out_tokens, TOKEN_LPAREN, sql_strdup("("), index)) {
                    snprintf(error_buf, error_buf_size, "메모리 할당에 실패했습니다.");
                    free_tokens(out_tokens);
                    return 0;
                }
                index++;
                break;
            case ')':
                if (!append_token(out_tokens, TOKEN_RPAREN, sql_strdup(")"), index)) {
                    snprintf(error_buf, error_buf_size, "메모리 할당에 실패했습니다.");
                    free_tokens(out_tokens);
                    return 0;
                }
                index++;
                break;
            case ';':
                if (!append_token(out_tokens, TOKEN_SEMICOLON, sql_strdup(";"), index)) {
                    snprintf(error_buf, error_buf_size, "메모리 할당에 실패했습니다.");
                    free_tokens(out_tokens);
                    return 0;
                }
                index++;
                break;
            case '=':
                if (!append_token(out_tokens, TOKEN_EQUALS, sql_strdup("="), index)) {
                    snprintf(error_buf, error_buf_size, "메모리 할당에 실패했습니다.");
                    free_tokens(out_tokens);
                    return 0;
                }
                index++;
                break;
            default:
                snprintf(error_buf, error_buf_size, "지원하지 않는 문자입니다: '%c' (위치=%zu)", ch, index);
                free_tokens(out_tokens);
                return 0;
        }
    }

    if (!append_token(out_tokens, TOKEN_EOF, sql_strdup("<eof>"), index)) {
        snprintf(error_buf, error_buf_size, "메모리 할당에 실패했습니다.");
        free_tokens(out_tokens);
        return 0;
    }

    return 1;
}

void free_tokens(TokenArray *tokens) {
    size_t index;

    if (tokens == NULL) {
        return;
    }

    for (index = 0; index < tokens->count; ++index) {
        free(tokens->items[index].lexeme);
    }
    free(tokens->items);
    tokens->items = NULL;
    tokens->count = 0;
    tokens->capacity = 0;
}

const char *token_type_name(TokenType type) {
    switch (type) {
        case TOKEN_EOF:
            return "EOF";
        case TOKEN_IDENTIFIER:
            return "IDENTIFIER";
        case TOKEN_STRING:
            return "STRING";
        case TOKEN_NUMBER:
            return "NUMBER";
        case TOKEN_INSERT:
            return "INSERT";
        case TOKEN_INTO:
            return "INTO";
        case TOKEN_VALUES:
            return "VALUES";
        case TOKEN_SELECT:
            return "SELECT";
        case TOKEN_FROM:
            return "FROM";
        case TOKEN_WHERE:
            return "WHERE";
        case TOKEN_STAR:
            return "STAR";
        case TOKEN_COMMA:
            return "COMMA";
        case TOKEN_LPAREN:
            return "LPAREN";
        case TOKEN_RPAREN:
            return "RPAREN";
        case TOKEN_SEMICOLON:
            return "SEMICOLON";
        case TOKEN_EQUALS:
            return "EQUALS";
    }
    return "UNKNOWN";
}
