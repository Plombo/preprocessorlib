/*
 * OpenBOR - http://www.LavaLit.com
 * -----------------------------------------------------------------------
 * Licensed under the BSD license, see LICENSE in OpenBOR root for details.
 *
 * Copyright (c) 2004 - 2010 OpenBOR Team
 */

/**
 * This is the parser for the script preprocessor.  Its purpose is to emit the 
 * preprocessed source code for use by scriptlib.  It is not derived from the 
 * parser in scriptlib because it does something entirely different.
 * 
 * @author Plombo
 * @date 15 October 2010
 */

#ifndef PP_PARSER_H
#define PP_PARSER_H

#include "pp_lexer.h"
#include "types.h"
#include "openborscript.h"

#define MACRO_CONTENTS_SIZE		512

typedef struct pp_parser {
    Script* script;
    pp_lexer lexer;
    char* filename;
    char* sourceCode;
    bool slashComment;
    bool starComment;
    bool newline;
} pp_parser;

// FIXME: nothing outside of pp_parser has any business accessing the token buffer
extern char* tokens;

void pp_parser_init(pp_parser* self, Script* script, char* filename, char* sourceCode);
void pp_parser_reset();
void pp_error(pp_parser* self, char* format, ...);
void pp_parser_parse(pp_parser* self);
void pp_parser_parse_directive(pp_parser* self);
void pp_parser_include(pp_parser* self, char* filename);
void pp_parser_conditional(pp_parser* self, PP_TOKEN_TYPE directive);
bool pp_parser_eval_conditional(pp_parser* self, PP_TOKEN_TYPE directive);
void pp_parser_insert_macro(pp_parser* self, char* name);

#endif

