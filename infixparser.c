/**
 * This is a parser for simple infix expressions that takes precedence into
 * account.
 *
 * @author Bryan Cain (Plombo)
 * @date 5 March 2012
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <assert.h>
#include "pp_lexer.h"
#include "infixparser.h"

#undef printf
#define tokendisplay(tok) ((tok).theType == PP_TOKEN_EOF ? "E" : (tok).theSource)
#define FIXTREE_DEBUG 0
#define LEXTREE_DEBUG 0

HRESULT error(char* format, ...)
{
	char buf[1024] = {""};
	va_list arglist;

	va_start(arglist, format);
	vsprintf(buf, format, arglist);
	va_end(arglist);
	printf("Error: %s\n", buf);

	return E_FAIL;
}

/**
 * @return the type of a token - binary operator, unary operator, multipurpose
 *         operator (+ and - can be binary or unary), or operand (leaf).
 */
int tokentype(PP_TOKEN_TYPE id)
{
	switch(id)
	{
		// BITWISE_AND and MUL would be MULTI in full C, but we don't need pointer operations for preprocessing
		case PP_TOKEN_LEFT_OP:
		case PP_TOKEN_RIGHT_OP:
		case PP_TOKEN_LT:
		case PP_TOKEN_GT:
		case PP_TOKEN_LE_OP:
		case PP_TOKEN_GE_OP:
		case PP_TOKEN_EQ_OP:
		case PP_TOKEN_NE_OP:
		case PP_TOKEN_AND_OP:
		case PP_TOKEN_OR_OP:
		case PP_TOKEN_BITWISE_AND:
		case PP_TOKEN_BITWISE_OR:
		case PP_TOKEN_XOR:
		case PP_TOKEN_MUL:
		case PP_TOKEN_DIV:
		case PP_TOKEN_MOD:
			return BINARY;
		case PP_TOKEN_BOOLEAN_NOT:
		case PP_TOKEN_BITWISE_NOT:
		case PP_TOKEN_LPAREN:
		case PP_TOKEN_RPAREN:
		case PP_TOKEN_EOF:
		case PP_TOKEN_NEWLINE:
			return UNARY;
		case PP_TOKEN_ADD:
		case PP_TOKEN_SUB:
			return MULTI;
		default:
			return LEAF;
	} 
}

/**
 * @return the precedence from 1 (lowest) to 10 (highest) of a binary operator,
 *         or 0 if the operation is not binary
 */
int precedence(PP_TOKEN_TYPE op)
{
	switch(op)
	{
		case PP_TOKEN_MUL:
		case PP_TOKEN_DIV:
		case PP_TOKEN_MOD:
			return 10;
		case PP_TOKEN_ADD:
		case PP_TOKEN_SUB:
			return 9;
		case PP_TOKEN_LEFT_OP:
		case PP_TOKEN_RIGHT_OP:
			return 8;
		case PP_TOKEN_LT:
		case PP_TOKEN_GT:
		case PP_TOKEN_LE_OP:
		case PP_TOKEN_GE_OP:
			return 7;
		case PP_TOKEN_EQ_OP:
		case PP_TOKEN_NE_OP:
			return 6;
		case PP_TOKEN_BITWISE_AND:
			return 5;
		case PP_TOKEN_XOR:
			return 4;
		case PP_TOKEN_BITWISE_OR:
			return 3;
		case PP_TOKEN_AND_OP:
			return 2;
		case PP_TOKEN_OR_OP:
			return 1;
		default:
			return 0;
	}
}

/**
 * Initializes the fields of a tree.
 * @param self the tree
 * @param info the node
 * @param left the left subtree
 * @param right the right subtree
 */
void tree_init(tree* self, nodedata* info, tree* left, tree* right)
{
	self->info = info;
	self->left = left;
	self->right = right;
}

void tree_display(tree* self)
{
	if(self->info->type == UNARY && self->info->theToken.theType != PP_TOKEN_RPAREN)
		printf("%s", self->info->theToken.theSource);
	if(self->left) tree_display(self->left);
	if(self->info->type != UNARY || self->info->theToken.theType == PP_TOKEN_RPAREN)
		printf("%s", self->info->theToken.theSource);
	if(self->right) tree_display(self->right);
}

void tree_display2(tree* self)
{
	if(self->info->type == BINARY)
	{
		printf("%s: %s %s\n", tokendisplay(self->info->theToken), tokendisplay(self->left->info->theToken), tokendisplay(self->right->info->theToken));
		tree_display2(self->left);
		tree_display2(self->right);
	}
	else if(self->info->type == UNARY)
	{
		printf("%s: %s\n", tokendisplay(self->info->theToken), tokendisplay(self->left->info->theToken));
		tree_display2(self->left);
	}
	else
		printf("%s\n", tokendisplay(self->info->theToken));
}

/**
 * Rearranges a parsed tree so that the operations will be done in order of
 * precedence.
 */
void fixtree(tree* self)
{
#if FIXTREE_DEBUG
	static int iteration = 0;
	++iteration;
	
	printf("Iteration %i\n", iteration);
	tree_display2(self);
	printf("\n");
#endif
	
	if(self->info->type == LEAF)
		assert(!self->left && !self->right);
	else if(self->info->type == UNARY)
	{
		assert(self->left && !self->right);
		fixtree(self->left);
	}
	else
	{
		assert(self->info->type == BINARY);
		assert(self->left && self->right);
		fixtree(self->right);
		if(self->right->info->type == BINARY &&
		   precedence(self->info->theToken.theType) >= precedence(self->right->info->theToken.theType))
		{
			tree* ll = self->left->left;
			tree* r = self->right;
#if FIXTREE_DEBUG
			printf("Swap %s with %s (iteration %i)\n", tokendisplay(self->info->theToken), tokendisplay(self->right->info->theToken), iteration);
#endif
			self->left->left = malloc(sizeof(tree));
			tree_init(self->left->left, self->left->info, ll ? ll : NULL, self->left->right);
			tree_init(self->left, self->info, self->left->left, self->right->left);
			tree_init(self, self->right->info, self->left, self->right->right);
			free(r);
		}
		fixtree(self->left);
	}
	
#if FIXTREE_DEBUG
	printf("return from %i\n", iteration);
	--iteration;
#endif
}

/**
 * @return the tree structure constructed from the input, or NULL on error
 */
tree* parsetree(pp_lexer* lexer, bool paren)
{
	tree* root = malloc(sizeof(tree));
	tree* current = root;
	tree* previous = NULL;
	tree* leftleaf = NULL;
	nodedata* currentdata;
	pp_token token;
	
	memset(&token, 0, sizeof(token));
	
	while(token.theType != PP_TOKEN_EOF && token.theType != PP_TOKEN_RPAREN)
	{
		int type;
		pp_lexer_GetNextToken(lexer, &token);
		type = tokentype(token.theType);
		
		// determine whether a MULTI operator is being used as unary or binary
		if(type == MULTI)
		{
			if(!leftleaf)
				type = UNARY;
			else
			{
				tree* bottomnode = leftleaf;
				
				while(bottomnode && bottomnode->left)
					bottomnode = bottomnode->left;
				type = (bottomnode->info->type == LEAF) ? BINARY : UNARY;
			}
		}
		
		// treat newlines as the end of the expression, same as EOF
		if(token.theType == PP_TOKEN_NEWLINE)
			token.theType = PP_TOKEN_EOF;
		
		currentdata = malloc(sizeof(nodedata));
		memcpy(&currentdata->theToken, &token, sizeof(pp_token));
		currentdata->type = type;
		
		if(token.theType == PP_TOKEN_EOF || token.theType == PP_TOKEN_RPAREN)
		{
			if(!leftleaf || 
			   (token.theType == PP_TOKEN_RPAREN && !paren) ||
			   (token.theType != PP_TOKEN_RPAREN && paren))
			{
				error("unexpected ')' or end of file");
				return NULL;
			}
			
			tree_init(current, currentdata, leftleaf, NULL);
			if(previous)
				previous->right = current;
		}
		else if(token.theType == PP_TOKEN_WHITESPACE) // skip over whitespace
		{
		}
		else if(type == LEAF || type == UNARY)
		{
			tree* treedata;
			tree* bottomnode = leftleaf;
			tree* subtree = NULL;
			
			// find the bottom of the left tree (there can be any number of unary operators stacked there)
			if(leftleaf)
				while(bottomnode->left) bottomnode = bottomnode->left;
			
			if(!(!leftleaf || bottomnode->info->type == UNARY))
			{
				error("expected an operator, got '%s'", token.theSource);
				return NULL;
			}
			
			if(token.theType == PP_TOKEN_LPAREN)
				subtree = parsetree(lexer, true);
			
			treedata = malloc(sizeof(tree));
			tree_init(treedata, currentdata, subtree, NULL);
			
			// insert at the bottom of the left tree
			if(leftleaf)
				bottomnode->left = treedata;
			else
				leftleaf = treedata;
		}
		else if(type == BINARY)
		{
			if(!leftleaf)
			{
				error("expected an operand, got '%s'", token.theSource);
				return NULL;
			}
			
			tree_init(current, currentdata, leftleaf, NULL);
			leftleaf = NULL;
			
			if(previous)
				previous->right = current;
			
			previous = current;
			current = malloc(sizeof(tree));

#if LEXTREE_DEBUG
			printf("%s %s\n", previous->left->info->theToken.theSource, token.theSource);
#endif
		}
	}
	
	return root;
}

#ifndef CALCULATOR
int main()
{
	pp_lexer lexer;
	TEXTPOS startPos = {0,0};
	//pp_lexer_Init(&lexer, "a*(b+c)/2 + 2", startPos);
	//pp_lexer_Init(&lexer, "a*b+c", startPos);
	//pp_lexer_Init(&lexer, "a*(b+c)", startPos);
	//pp_lexer_Init(&lexer, "~a*+(b+!-c)/2", startPos);
	//pp_lexer_Init(&lexer, "48/4/3/2", startPos);
	//pp_lexer_Init(&lexer, "48/4/3/2/1", startPos);
	pp_lexer_Init(&lexer, "~10 + 17", startPos);
	
	tree* expression = parsetree(&lexer, false);
	if(expression == NULL) return 1;
	
	tree_display(expression);
	printf("\n\n");
	tree_display2(expression);
	printf("\n");
	
	fixtree(expression);
	tree_display(expression);
	printf("\n\n");
	tree_display2(expression);
	printf("\n");
	
	printf("Expression evaluates to %i\n", tree_eval(expression));
	
	return 0;
}
#endif

