#ifndef AST_H
#define AST_H

#include <stddef.h>

typedef enum {
    LITERAL_STRING,
    LITERAL_NUMBER
} LiteralType;

typedef struct {
    LiteralType type;
    char *text;
} Literal;

typedef struct {
    char **items;
    size_t count;
    int is_star;
} ColumnList;

typedef struct {
    char *column_name;
    Literal value;
} WhereClause;

typedef struct {
    char *table_name;
    ColumnList columns;
    int has_where;
    WhereClause where;
} SelectStatement;

typedef struct {
    char *table_name;
    Literal *values;
    size_t value_count;
} InsertStatement;

typedef enum {
    AST_SELECT_STATEMENT,
    AST_INSERT_STATEMENT
} StatementType;

typedef struct {
    StatementType type;
    union {
        SelectStatement select_stmt;
        InsertStatement insert_stmt;
    } as;
} Statement;

void free_literal(Literal *literal);
Literal clone_literal(const Literal *literal);
void free_statement(Statement *statement);

#endif
