#ifndef TOKENIZER_H
#define TOKENIZER_H

#include <stddef.h>

typedef enum {
    TOKEN_EOF = 0,
    TOKEN_IDENTIFIER,
    TOKEN_STRING,
    TOKEN_NUMBER,
    TOKEN_INSERT,
    TOKEN_INTO,
    TOKEN_VALUES,
    TOKEN_SELECT,
    TOKEN_FROM,
    TOKEN_WHERE,
    TOKEN_STAR,
    TOKEN_COMMA,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_SEMICOLON,
    TOKEN_EQUALS
} TokenType;

typedef struct {
    TokenType type;
    char *lexeme;
    size_t position;
} Token;

typedef struct {
    Token *items;
    size_t count;
    size_t capacity;
} TokenArray;

int tokenize_sql(const char *input, TokenArray *out_tokens, char *error_buf, size_t error_buf_size);
void free_tokens(TokenArray *tokens);
const char *token_type_name(TokenType type);

#endif
