/*
 * OpenBOR - http://www.LavaLit.com
 * -----------------------------------------------------------------------
 * Licensed under the BSD license, see LICENSE in OpenBOR root for details.
 *
 * Copyright (c) 2004 - 2010 OpenBOR Team
 */

/**
 * This is the parser for the script preprocessor.  Its purpose is to emit(token) the 
 * preprocessed source code for use by scriptlib.  It is not related to the 
 * parser in scriptlib because it does something entirely different.
 * 
 * TODO: add a universal "preprocessor error" function
 * TODO/FIXME: lots of stuff with #define support
 * TODO: support conditional directives (#if, #ifdef, #ifndef, #elif, #else, #endif)
 * 
 * @author Plombo
 * @date 15 October 2010
 */

#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <errno.h>
#include "List.h"
#include "pp_parser.h"

#define skip_whitespace()	do { pp_lexer_GetNextToken(&self->lexer, &token); } while(token.theType == PP_TOKEN_WHITESPACE)

#if PP_TEST // using pp_test.c to test the preprocessor functionality; OpenBOR functionality is not available
#undef printf
#define tracemalloc(name, size)		malloc(size)
#define tracefree(ptr)				free(ptr)
#define emit(tkn)					fprintf(stdout, "%s", tkn.theSource)
#else // otherwise, we can use OpenBOR functionality like tracemalloc and writeToLogFile
#include "tracemalloc.h"
#include "globals.h"
#include "packfile.h"
#define emit(tkn)					Script_AppendText(self->script, tkn.theSource, self->filename)
#endif

/**
 * List of currently defined macros.  Macros don't die when parsers do (there's 
 * a separate parser for each #include and #define) so this list is defined globally.
 */
List macros = {NULL, NULL, NULL, NULL, 0, 0};

/**
 * TODO: Do this in a way that doesn't cause insane memory fragmentation, e.g. use a 
 * single buffer for all tokens and realloc() it if we need more space.  Even 
 * better, we could just use the buffer to store the resulting string, and not 
 * have to deal with concatenating all of the tokens when we're finished.
 */
//List tokens = {NULL, NULL, NULL, NULL, 0, 0};
char* tokens = NULL;

/**
 * Initializes a preprocessor parser (pp_parser) object.
 * @param self the object
 * @param script the script to write the processed script file to
 */
void pp_parser_init(pp_parser* self, Script* script, char* filename, char* sourceCode)
{
	TEXTPOS initialPos = {0, 0};
	pp_lexer_Init(&self->lexer, sourceCode, initialPos);
	self->script = script;
	self->filename = filename;
	self->sourceCode = sourceCode;
	
	if(tokens == NULL)
	{
		tokens = tracemalloc("pp_parser tokens", 4096); // default buffer size of 4 KB, expand it later if needed
		memset(tokens, 0, 4096);
	}
}

/**
 * Undefines and frees all currently defined macros.  This should be called 
 * before and after preprocessing a script.
 * TODO: if we switch to a buffer-based solution for token storage in the future, 
 * this function should also free the token buffer.  It should also be renamed 
 * to pp_parser_reset() in that case.
 */
void pp_parser_reset_macros()
{
	List_Reset(&macros); // start at first element in list
	while(macros.size > 0) {
		tracefree(List_Retrieve(&macros));
		List_Remove(&macros);
	}
	
	if(tokens != NULL)
		tracefree(tokens);
}

/**
 * Preprocesses the entire source file.
 * @return S_OK on success, E_FAIL on failure
 */
HRESULT pp_parser_parse(pp_parser* self)
{
	pp_token token;
	
	self->newline = 1;
	self->slashComment = 0;
	self->starComment = 0;
	
	while(SUCCEEDED(pp_lexer_GetNextToken(&self->lexer, &token))) {
		switch(token.theType) {
			case PP_TOKEN_DIRECTIVE:
				if(self->newline && !self->slashComment && !self->starComment) {
					/* only parse the "#" symbol when it's at the beginning of a 
					 * line (ignoring whitespace) and not in a comment */
					if(FAILED(pp_parser_parse_directive(self))) return E_FAIL;
				} else emit(token);
				break;
			case PP_TOKEN_COMMENT_SLASH:
				if(!self->starComment) self->slashComment = 1;
				self->newline = 0;
				emit(token);
				break;
			case PP_TOKEN_COMMENT_STAR_BEGIN:
				if(!self->slashComment) self->starComment = 1;
				self->newline = 0;
				emit(token);
				break;
			case PP_TOKEN_COMMENT_STAR_END:
				self->starComment = 0;
				self->newline = 0;
				emit(token);
				break;
			case PP_TOKEN_NEWLINE:
				self->slashComment = 0;
				self->newline = 1;
				emit(token);
				break;
			case PP_TOKEN_WHITESPACE:
				emit(token);
				// whitespace doesn't affect the newline property
				break;
			case PP_TOKEN_IDENTIFIER:
				if(List_FindByName(&macros, token.theSource)) pp_parser_insert_macro(self, token.theSource);
				else emit(token);
				break;
			case PP_TOKEN_EOF:
				emit(token);
				return S_OK;
			default:
				self->newline = 0;
				emit(token);
		}
	}
	printf("Preprocessor error: end of source code reached without EOF token\n");
	return E_FAIL;
}

/**
 * Parses a C preprocessor directive.  When this function is called, the token
 * '#' has just been detected by the compiler.
 * 
 * Currently supported directives are #include and #define. Support for #define 
 * is still limited, as macros can only be 512 characters long and "function-like" 
 * macros are not supported.
 */
HRESULT pp_parser_parse_directive(pp_parser* self) {
	pp_token token;
	
	skip_whitespace();
	switch(token.theType) {
		case PP_TOKEN_INCLUDE:
		{
			char* filename;
			skip_whitespace();
			
			if(token.theType != PP_TOKEN_STRING_LITERAL) {
				printf("Preprocessor error: %s: %i: couldn't interpret #include path '%s'\n", self->filename, token.theTextPosition.row, token.theSource);
				return E_FAIL;
			}
			
			filename = token.theSource + 1; // trim first " mark
			filename[strlen(filename)-1] = '\0'; // trim last " mark
			
			return pp_parser_include(self, filename);
		}
		case PP_TOKEN_DEFINE:
		{
			// FIXME: this will only work if the macro name is on the same line as the "#define"
			// FIXME: does not properly support comments after the macro definition on the same line
			// FIXME: length of contents is limited to MACRO_CONTENTS_SIZE (512) characters
			char name[128];
			char* contents = tracemalloc("pp_parser_define", MACRO_CONTENTS_SIZE);
			
			skip_whitespace();
			if(token.theType != PP_TOKEN_IDENTIFIER) { // Macro must have at least a name before the newline
				printf("Preprocessor error: no macro name given in #define directive\n");
				return E_FAIL;
			}
			
			// Parse macro name and contents
			strcpy(name, token.theSource);
			contents[0] = '\0';
			while(1) {
				pp_lexer_GetNextToken(&self->lexer, &token);
				if((token.theType == PP_TOKEN_NEWLINE) || (token.theType == PP_TOKEN_EOF)) { emit(token); break; }
				else if(strcmp(token.theSource, "\\") == 0) pp_lexer_GetNextToken(&self->lexer, &token); // allows escaping line breaks with "\"
				
				if((strlen(contents) + strlen(token.theSource) + 1) > MACRO_CONTENTS_SIZE) {
					// prevent buffer overflow
					printf("Preprocessor error: length of macro contents is too long; must be <= %i characters\n", MACRO_CONTENTS_SIZE);
					return E_FAIL;
				}
				else strcat(contents, token.theSource);
			}
			
			// Add macro to list
			List_InsertAfter(&macros, contents, name);
			//printf("Defining macro '%s' as '%s'\n", name, contents);
			
			break;
		}
		default:
			printf("Preprocessor error: unknown directive '%s'\n", token.theSource);
			return E_FAIL;
	}
	
	return S_OK;
}

/**
 * Includes a source file specified with the #include directive.
 * @param filename the path to include
 */
HRESULT pp_parser_include(pp_parser* self, char* filename)
{
	pp_parser incparser;
	char* buffer;
	int length;
	int bytes_read;
	
	// Open the file and determine its size
#if PP_TEST // use stdio functions for file I/O
	FILE* fp = fopen(filename, "rb");
	if(fp == NULL) return E_FAIL;
	fseek(fp, 0, SEEK_END);
	length = ftell(fp);
	fseek(fp, 0, SEEK_SET);
#else // use packfile functions for file I/O
	int handle = openpackfile(filename, packfile);
	if(handle < 0) return E_FAIL;
	length = seekpackfile(handle, 0, SEEK_END);
	seekpackfile(handle, 0, SEEK_SET);
#endif	
	
	// Allocate a buffer for the file's contents
	buffer = tracemalloc("pp_parser_include", length + 1);
	memset(buffer, 0, length + 1);
	
	// Read the file into the buffer
#if PP_TEST
	bytes_read = fread(buffer, 1, length, fp);
	fclose(fp);
#else
	bytes_read = readpackfile(handle, buffer, length);
	closepackfile(handle);
#endif
	
	if(bytes_read != length) { printf("Preprocessor I/O error: %s: %s\n", filename, strerror(errno)); }
	
	// Parse the source code in the buffer
	pp_parser_init(&incparser, self->script, filename, buffer);
	pp_parser_parse(&incparser);
	
	// Free the buffer to prevent memory leaks
	tracefree(buffer);
	
	return S_OK;
}

/**
 * Expands a macro.
 * Pre: the macro is defined
 */
void pp_parser_insert_macro(pp_parser* self, char* name)
{
	pp_parser macroParser;
	
	List_FindByName(&macros, name);
	pp_parser_init(&macroParser, self->script, self->filename, List_Retrieve(&macros));
	pp_parser_parse(&macroParser);
}

