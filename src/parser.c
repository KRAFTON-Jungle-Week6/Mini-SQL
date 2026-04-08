#include "parser.h"

#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const TokenArray *tokens;
    size_t current;
    char *error_buf;
    size_t error_buf_size;
} Parser;

static const Token *peek(Parser *parser) {
    return &parser->tokens->items[parser->current];
}

static const Token *previous(Parser *parser) {
    return &parser->tokens->items[parser->current - 1];
}

static int is_at_end(Parser *parser) {
    return peek(parser)->type == TOKEN_EOF;
}

static const Token *advance_token(Parser *parser) {
    if (!is_at_end(parser)) {
        parser->current++;
    }
    return previous(parser);
}

static int match_token(Parser *parser, TokenType type) {
    if (peek(parser)->type != type) {
        return 0;
    }
    advance_token(parser);
    return 1;
}

static int consume_token(Parser *parser, TokenType type, const char *message) {
    if (match_token(parser, type)) {
        return 1;
    }

    snprintf(parser->error_buf,
             parser->error_buf_size,
             "%s 현재 토큰=%s('%s')",
             message,
             token_type_name(peek(parser)->type),
             peek(parser)->lexeme);
    return 0;
}

static Literal parse_literal(Parser *parser, int *ok) {
    Literal literal;
    const Token *token;

    literal.type = LITERAL_STRING;
    literal.text = NULL;

    if (match_token(parser, TOKEN_STRING)) {
        token = previous(parser);
        literal.type = LITERAL_STRING;
        literal.text = sql_strdup(token->lexeme);
        *ok = literal.text != NULL;
        return literal;
    }

    if (match_token(parser, TOKEN_NUMBER)) {
        token = previous(parser);
        literal.type = LITERAL_NUMBER;
        literal.text = sql_strdup(token->lexeme);
        *ok = literal.text != NULL;
        return literal;
    }

    snprintf(parser->error_buf,
             parser->error_buf_size,
             "리터럴이 필요합니다. 현재 토큰=%s('%s')",
             token_type_name(peek(parser)->type),
             peek(parser)->lexeme);
    *ok = 0;
    return literal;
}

static int append_column(ColumnList *columns, const char *name) {
    char **new_items;
    char *copy;

    new_items = (char **)realloc(columns->items, (columns->count + 1) * sizeof(char *));
    if (new_items == NULL) {
        return 0;
    }
    columns->items = new_items;

    copy = sql_strdup(name);
    if (copy == NULL) {
        return 0;
    }

    columns->items[columns->count] = copy;
    columns->count++;
    return 1;
}

static int parse_column_list(Parser *parser, ColumnList *columns) {
    const Token *identifier;

    memset(columns, 0, sizeof(*columns));

    if (match_token(parser, TOKEN_STAR)) {
        columns->is_star = 1;
        return 1;
    }

    if (!consume_token(parser, TOKEN_IDENTIFIER, "SELECT 뒤에는 '*' 또는 컬럼 이름이 와야 합니다.")) {
        return 0;
    }

    identifier = previous(parser);
    if (!append_column(columns, identifier->lexeme)) {
        snprintf(parser->error_buf, parser->error_buf_size, "메모리 할당에 실패했습니다.");
        return 0;
    }

    while (match_token(parser, TOKEN_COMMA)) {
        if (!consume_token(parser, TOKEN_IDENTIFIER, "콤마 뒤에는 컬럼 이름이 와야 합니다.")) {
            return 0;
        }
        identifier = previous(parser);
        if (!append_column(columns, identifier->lexeme)) {
            snprintf(parser->error_buf, parser->error_buf_size, "메모리 할당에 실패했습니다.");
            return 0;
        }
    }

    return 1;
}

static int parse_where_clause(Parser *parser, WhereClause *where) {
    int ok;

    memset(where, 0, sizeof(*where));

    if (!consume_token(parser, TOKEN_IDENTIFIER, "WHERE 뒤에는 컬럼 이름이 와야 합니다.")) {
        return 0;
    }

    where->column_name = sql_strdup(previous(parser)->lexeme);
    if (where->column_name == NULL) {
        snprintf(parser->error_buf, parser->error_buf_size, "메모리 할당에 실패했습니다.");
        return 0;
    }

    if (!consume_token(parser, TOKEN_EQUALS, "현재 MVP에서는 WHERE column = literal 형태만 지원합니다.")) {
        return 0;
    }

    where->value = parse_literal(parser, &ok);
    if (!ok) {
        return 0;
    }

    return 1;
}

static int parse_select(Parser *parser, Statement *statement) {
    SelectStatement *select_stmt = &statement->as.select_stmt;

    memset(select_stmt, 0, sizeof(*select_stmt));
    statement->type = AST_SELECT_STATEMENT;

    if (!parse_column_list(parser, &select_stmt->columns)) {
        return 0;
    }

    if (!consume_token(parser, TOKEN_FROM, "SELECT 목록 뒤에는 FROM이 와야 합니다.")) {
        return 0;
    }

    if (!consume_token(parser, TOKEN_IDENTIFIER, "FROM 뒤에는 테이블 이름이 와야 합니다.")) {
        return 0;
    }

    select_stmt->table_name = sql_strdup(previous(parser)->lexeme);
    if (select_stmt->table_name == NULL) {
        snprintf(parser->error_buf, parser->error_buf_size, "메모리 할당에 실패했습니다.");
        return 0;
    }

    if (match_token(parser, TOKEN_WHERE)) {
        select_stmt->has_where = 1;
        if (!parse_where_clause(parser, &select_stmt->where)) {
            return 0;
        }
    }

    return 1;
}

static int append_value(InsertStatement *insert_stmt, Literal literal) {
    Literal *new_items;

    new_items = (Literal *)realloc(insert_stmt->values, (insert_stmt->value_count + 1) * sizeof(Literal));
    if (new_items == NULL) {
        return 0;
    }

    insert_stmt->values = new_items;
    insert_stmt->values[insert_stmt->value_count] = literal;
    insert_stmt->value_count++;
    return 1;
}

static int parse_insert(Parser *parser, Statement *statement) {
    InsertStatement *insert_stmt = &statement->as.insert_stmt;
    Literal literal;
    int ok;

    memset(insert_stmt, 0, sizeof(*insert_stmt));
    statement->type = AST_INSERT_STATEMENT;

    if (!consume_token(parser, TOKEN_INTO, "INSERT 뒤에는 INTO가 와야 합니다.")) {
        return 0;
    }

    if (!consume_token(parser, TOKEN_IDENTIFIER, "INTO 뒤에는 테이블 이름이 와야 합니다.")) {
        return 0;
    }

    insert_stmt->table_name = sql_strdup(previous(parser)->lexeme);
    if (insert_stmt->table_name == NULL) {
        snprintf(parser->error_buf, parser->error_buf_size, "메모리 할당에 실패했습니다.");
        return 0;
    }

    if (!consume_token(parser, TOKEN_VALUES, "현재 MVP에서는 INSERT INTO table VALUES (...) 형태만 지원합니다.")) {
        return 0;
    }

    if (!consume_token(parser, TOKEN_LPAREN, "VALUES 뒤에는 '(' 가 와야 합니다.")) {
        return 0;
    }

    literal = parse_literal(parser, &ok);
    if (!ok || !append_value(insert_stmt, literal)) {
        snprintf(parser->error_buf, parser->error_buf_size, "INSERT 값 저장에 실패했습니다.");
        if (ok) {
            free_literal(&literal);
        }
        return 0;
    }

    while (match_token(parser, TOKEN_COMMA)) {
        literal = parse_literal(parser, &ok);
        if (!ok || !append_value(insert_stmt, literal)) {
            snprintf(parser->error_buf, parser->error_buf_size, "INSERT 값 저장에 실패했습니다.");
            if (ok) {
                free_literal(&literal);
            }
            return 0;
        }
    }

    if (!consume_token(parser, TOKEN_RPAREN, "VALUES 목록 뒤에는 ')' 가 와야 합니다.")) {
        return 0;
    }

    return 1;
}

int parse_statement(const TokenArray *tokens, Statement *out_statement, char *error_buf, size_t error_buf_size) {
    Parser parser;

    memset(out_statement, 0, sizeof(*out_statement));
    memset(&parser, 0, sizeof(parser));
    parser.tokens = tokens;
    parser.error_buf = error_buf;
    parser.error_buf_size = error_buf_size;

    if (match_token(&parser, TOKEN_SELECT)) {
        if (!parse_select(&parser, out_statement)) {
            free_statement(out_statement);
            return 0;
        }
    } else if (match_token(&parser, TOKEN_INSERT)) {
        if (!parse_insert(&parser, out_statement)) {
            free_statement(out_statement);
            return 0;
        }
    } else {
        snprintf(error_buf,
                 error_buf_size,
                 "지원하지 않는 문장입니다. 첫 토큰=%s('%s')",
                 token_type_name(peek(&parser)->type),
                 peek(&parser)->lexeme);
        return 0;
    }

    match_token(&parser, TOKEN_SEMICOLON);
    if (!consume_token(&parser, TOKEN_EOF, "SQL 파일에는 현재 한 개의 문장만 넣을 수 있습니다.")) {
        free_statement(out_statement);
        return 0;
    }

    return 1;
}
