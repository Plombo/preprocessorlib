/**
 * Calculates the value of simple constant integer expressions in C.
 *
 * @author Bryan Cain (Plombo)
 * @date 6 March 2012
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include "infixparser.h"

int tree_eval(tree* self)
{
	int left, right;
	
	switch(self->info->type)
	{
	case LEAF:
		switch(self->info->theToken.theType)
		{
			case PP_TOKEN_INTCONSTANT:
				if(self->info->theToken.theSource[0] == '0') // octal
					return strtol(self->info->theToken.theSource, NULL, 8);
				else
					return strtol(self->info->theToken.theSource, NULL, 10);
			case PP_TOKEN_HEXCONSTANT:
				assert(!strncmp(self->info->theToken.theSource, "0x", 2));
				return strtol(self->info->theToken.theSource + 2, NULL, 16);
			default:
				printf("Error: '%s' is not an integer constant", self->info->theToken.theSource);
				assert(0);
		}
		break;
	case UNARY:
		assert(self->left);
		left = tree_eval(self->left);
		switch(self->info->theToken.theType)
		{
			case PP_TOKEN_EOF:
			case PP_TOKEN_LPAREN:
			case PP_TOKEN_RPAREN:
			case PP_TOKEN_ADD:
				return left;
			case PP_TOKEN_SUB:
				return -left;
			case PP_TOKEN_BOOLEAN_NOT:
				return !left;
			case PP_TOKEN_BITWISE_NOT:
				return ~left;
			default:
				assert(!"Unknown unary operator");
				break;
		}
		break;
	case BINARY:
		assert(self->left && self->right);
		left = tree_eval(self->left);
		right = tree_eval(self->right);
		switch(self->info->theToken.theType)
		{
			case PP_TOKEN_MUL:
				return left * right;
			case PP_TOKEN_DIV:
				return left / right;
			case PP_TOKEN_MOD:
				return left % right;
			case PP_TOKEN_ADD:
				return left + right;
			case PP_TOKEN_SUB:
				return left - right;
			case PP_TOKEN_LEFT_OP:
				return left << right;
			case PP_TOKEN_RIGHT_OP:
				return left >> right;
			case PP_TOKEN_LT:
				return left < right;
			case PP_TOKEN_GT:
				return left > right;
			case PP_TOKEN_LE_OP:
				return left <= right;
			case PP_TOKEN_GE_OP:
				return left >= right;
			case PP_TOKEN_EQ_OP:
				return left == right;
			case PP_TOKEN_NE_OP:
				return left != right;
			case PP_TOKEN_BITWISE_AND:
				return left & right;
			case PP_TOKEN_XOR:
				return left ^ right;
			case PP_TOKEN_BITWISE_OR:
				return left | right;
			case PP_TOKEN_AND_OP:
				return left && right;
			case PP_TOKEN_OR_OP:
				return left || right;
			default:
				assert(!"Unknown unary operator");
				break;
		}
	case MULTI:
	default:
		assert(!"Invalid node type");
	}
	
	return 0;
}

#ifdef CALCULATOR
int main()
{
	char buffer[1024];
	tree* expression;
	pp_lexer lexer;
	TEXTPOS startpos = {0,0};
	
	while(1)
	{
		// get the user's input (i.e. the expression to evaluate)
		printf(">> ");
		fgets(buffer, sizeof(buffer), stdin);
		
		// end the program if the user enters a blank line, a null char, or Ctrl+D
		if(buffer[0] == '\r' || buffer[0] == '\n' || buffer[0] == '\0')
			break;
		else if(feof(stdin))
		{
			printf("\n");
			break;
		}
		
		// parse the input and correct the parsed tree for operator precedence
		pp_lexer_Init(&lexer, buffer, startpos);
		expression = parsetree(&lexer, false);
		fixtree(expression);
		
		// evaluate the expression and display the result
		printf("%i\n", tree_eval(expression));
	}
	
	return 0;
}
#endif

