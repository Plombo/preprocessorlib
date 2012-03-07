#ifndef INFIXPARSER_H
#define INFIXPARSER_H

#include <stdbool.h>
#include "pp_lexer.h"
#undef printf

typedef struct {
	pp_token theToken;
	enum {BINARY, UNARY, MULTI, LEAF} type; // MULTI refers to operators that can be unary or binary (+, -)
} nodedata;

typedef struct node {
	nodedata* info;
	struct node* left;
	struct node* right;
} tree;

tree* parsetree(pp_lexer* lexer, bool paren);
void fixtree(tree* self);
int tree_eval(tree* self);

#endif

