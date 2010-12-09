/*
 * OpenBOR - http://www.LavaLit.com
 * -----------------------------------------------------------------------
 * Licensed under the BSD license, see LICENSE in OpenBOR root for details.
 *
 * Copyright (c) 2004 - 2010 OpenBOR Team
 */

/**
 * This is the parser for the script preprocessor.  Its purpose is to emit the 
 * preprocessed source code for use by scriptlib.  It is not related to the 
 * parser in scriptlib because it does something entirely different.
 * 
 * TODO/FIXME: lots of stuff with #define support
 * TODO: support conditional directives that require expression parsing (#if, #elif)
 * TODO: move the resizable buffer functionality into a separate class
 * 
 * @author Plombo
 * @date 15 October 2010
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <malloc.h>
#include <errno.h>
#include "List.h"
#include "pp_parser.h"
#include "borendian.h"

#define DEFAULT_TOKEN_BUFFER_SIZE	(16 * 1024)
#define TOKEN_BUFFER_SIZE_INCREMENT	(16 * 1024)
#define skip_whitespace()			do { pp_lexer_GetNextToken(&self->lexer, &token); } while(token.theType == PP_TOKEN_WHITESPACE)

#if PP_TEST // using pp_test.c to test the preprocessor functionality; OpenBOR functionality is not available
#undef printf
#define tracemalloc(name, size)		malloc(size)
#define tracecalloc(name, size)		calloc(1, size)
#define tracerealloc(ptr, size, os)	realloc(ptr, size)
#define tracefree(ptr)				free(ptr)
#define openpackfile(fname, pname)	((int)fopen(fname, "rb"))
#define readpackfile(hnd, buf, len)	fread(buf, 1, len, (FILE*)hnd)
#define seekpackfile(hnd, loc, md)	fseek((FILE*)hnd, loc, md)
#define tellpackfile(hnd)			ftell((FILE*)hnd)
#define closepackfile(hnd)			fclose((FILE*)hnd)
#define shutdown(ret, msg, args...) { fprintf(stderr, msg, ##args); exit(ret); }
#else // otherwise, we can use OpenBOR functionality like tracemalloc and writeToLogFile
#include "openbor.h"
#include "globals.h"
#include "tracemalloc.h"
#include "packfile.h"
#define tellpackfile(hnd)			seekpackfile(hnd, 0, SEEK_CUR)
#endif

/**
 * List of currently defined macros.  Macros don't die when parsers do (there's 
 * a separate parser for each #include and #define) so this list is defined globally.
 */
List macros = {NULL, NULL, NULL, NULL, 0, 0};

/**
 * The token buffer.  Like the macro list, it is defined globally because it 
 * doesn't die when parsers do.  It is realloc()ed if it needs to be enlarged.
 */
char* tokens = NULL;
static int token_bufsize = 0;
static int tokens_length = 0;

/**
 * Stack of conditional directives.  The preprocessor can handle up to 16 nested 
 * conditionals.  The stack is implemented as a 32-bit integer.
 */
union {
	int all;
	struct {
		unsigned top:2;
		unsigned others:30;
	};
} conditionals;

int num_conditionals = 0;

enum conditional_state {
	cs_none = 0,
	cs_true = 1,
	cs_false = 2,
	cs_done = 3
};

/**
 * Emits a token to the token buffer, enlarging the token buffer if necessary. 
 * (Too bad strlcat() isn't part of the C standard library, or even in glibc.)
 * \pre token buffer is non-NULL
 * @param token the pp_token to emit
 */
static __inline__ void emit(pp_token token)
{
	int toklen = strlen(token.theSource);
	
	// don't emit anything if the current conditional block evaluates to false
	if(conditionals.top == cs_false || conditionals.top == cs_done)
		return;
	
	if(toklen + tokens_length >= token_bufsize)
	{
		int new_bufsize = token_bufsize + TOKEN_BUFFER_SIZE_INCREMENT;
		char* tokens2;
		tokens2 = tracerealloc(tokens, new_bufsize, token_bufsize);
		if(tokens2)
		{
			tokens = tokens2;
			memset(tokens + token_bufsize, 0, new_bufsize - token_bufsize);
			token_bufsize = new_bufsize;
		}
		else
		{
			// tracerealloc() failed...
			shutdown(1, "Fatal error: tracerealloc() failed. The system might "
				   "be out of memory, or it may have a shoddy realloc() "
				   "implementation.\n");
		}
	}
	
	strncat(tokens, token.theSource, toklen);
	tokens_length += toklen;
}

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
	
	// allocate token buffer with default size of 4 KB; expand it later if needed
	if(tokens == NULL)
	{
		tokens = tracecalloc("pp_parser tokens", DEFAULT_TOKEN_BUFFER_SIZE);
		token_bufsize = DEFAULT_TOKEN_BUFFER_SIZE;
		tokens_length = 0;
	}
}

/**
 * Frees the entire global parser state.  This should be called before and after
 * preprocessing a script.
 */
void pp_parser_reset()
{
	// undefine and free all macros
	List_Reset(&macros);
	while(macros.size > 0)
	{
		tracefree(List_Retrieve(&macros));
		List_Remove(&macros);
	}
	
	// free the token buffer
	if(tokens != NULL)
	{
		tracefree(tokens);
		tokens = NULL;
		token_bufsize = tokens_length = 0;
	}
	
	// reset the conditional state
	conditionals.all = 0;
}

/**
 * Exits the preprocessor with an error message.
 */
void pp_error(pp_parser* self, char* format, ...)
{
	char buf[1024] = {""};
	va_list arglist;
	
	va_start(arglist, format);
	vsprintf(buf, format, arglist);
	va_end(arglist);
	shutdown(1, "Preprocessor error: %s: line %d: %s\n", self->filename, self->lexer.theTokenPosition.row + 1, buf);
}

/**
 * Writes a warning message to the log.
 */
void pp_warning(pp_parser* self, char* format, ...)
{
	char buf[1024] = {""};
	va_list arglist;
	
	va_start(arglist, format);
	vsprintf(buf, format, arglist);
	va_end(arglist);
	printf("Preprocessor warning: %s: line %d: %s\n", self->filename, self->lexer.theTokenPosition.row + 1, buf);
}

/**
 * Preprocesses the entire source file.  Will shut down the engine if it fails 
 * (no real way to recover), so no need for a return value.
 */
void pp_parser_parse(pp_parser* self)
{
	pp_token token;
	
	self->newline = 1;
	self->slashComment = 0;
	self->starComment = 0;
	
	while(SUCCEEDED(pp_lexer_GetNextToken(&self->lexer, &token)))
	{
		switch(token.theType)
		{
			case PP_TOKEN_DIRECTIVE:
				if(self->newline && !self->slashComment && !self->starComment)
				{ /* only parse the "#" symbol when it's at the beginning of a 
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
	pp_error(self, "end of source code reached without EOF token");
	return E_FAIL;
}

// TODO: use resizable buffers to preclude these stupid overflow errors
// FIXME: does not properly support comments on the same line after the message or macro definition
void pp_parser_readline(pp_parser* self, char* buf, int bufsize)
{
	pp_token token;
	int total_length = 1;
	
	buf[0] = '\0';
	skip_whitespace();
	while(1)
	{
		if((token.theType == PP_TOKEN_NEWLINE) || (token.theType == PP_TOKEN_EOF)) { emit(token); break; }
		else if(strcmp(token.theSource, "\\") == 0) pp_lexer_GetNextToken(&self->lexer, &token); // allows escaping line breaks with "\"
		
		if((total_length + strlen(token.theSource)) > bufsize)
		{
			// Prevent buffer overflow
			// FIXME: this is used for more than just macros now; change the message!
			pp_error(self, "length of macro contents is too long; must be <= %i characters", bufsize);
		}
		
		strcat(buf, token.theSource);
		total_length += strlen(token.theSource);
		pp_lexer_GetNextToken(&self->lexer, &token);
	}
}

/**
 * Parses a C preprocessor directive.  When this function is called, the token
 * '#' has just been detected by the compiler.
 * 
 * Currently supported directives are #include and #define. Support for #define 
 * is still limited, as macros can only be 512 characters long and "function-like" 
 * macros are not supported.
 */
void pp_parser_parse_directive(pp_parser* self) {
	pp_token token;
	
	skip_whitespace();
	
	// most directives shouldn't be parsed if we're in the middle of a conditional false
	if(conditionals.top == cs_false || conditionals.top == cs_done)
	{
		if(token.theType != PP_TOKEN_ELIF &&
		   token.theType != PP_TOKEN_ELSE &&
		   token.theType != PP_TOKEN_ENDIF)
		{
			return;
		}
	}
	
	switch(token.theType)
	{
		case PP_TOKEN_INCLUDE:
		{
			char* filename;
			skip_whitespace();
			
			if(token.theType != PP_TOKEN_STRING_LITERAL)
			{
				pp_error(self, "couldn't interpret #include path '%s'", token.theSource);
			}
			
			filename = token.theSource + 1; // trim first " mark
			filename[strlen(filename)-1] = '\0'; // trim last " mark
			
			pp_parser_include(self, filename);
			break;
		}
		case PP_TOKEN_DEFINE:
		{
			// FIXME: this will only work if the macro name is on the same line as the "#define"
			// FIXME: length of contents is limited to MACRO_CONTENTS_SIZE (512) characters
			char name[128];
			char* contents = tracemalloc("pp_parser_define", MACRO_CONTENTS_SIZE);
			
			skip_whitespace();
			if(token.theType != PP_TOKEN_IDENTIFIER)
			{
				// Macro must have at least a name before the newline
				pp_error(self, "no macro name given in #define directive");
			}
			
			// Parse macro name and contents
			strcpy(name, token.theSource);
			pp_parser_readline(self, contents, MACRO_CONTENTS_SIZE);
			
			// Add macro to list
			List_InsertAfter(&macros, contents, name);
			break;
		}
		case PP_TOKEN_UNDEF:
			skip_whitespace();
			if(List_FindByName(&macros, token.theSource))
				List_Remove(&macros);
			break;
		case PP_TOKEN_IF:
		case PP_TOKEN_IFDEF:
		case PP_TOKEN_IFNDEF:
		case PP_TOKEN_ELIF:
		case PP_TOKEN_ELSE:
		case PP_TOKEN_ENDIF:
			pp_parser_conditional(self, token.theType);
			break;
		case PP_TOKEN_WARNING:
		case PP_TOKEN_ERROR_TEXT:
		{
			char text[256] = {""};
			PP_TOKEN_TYPE msgType = token.theType; // "token" is about to be clobbered, so save whether this is a warning or error
			
			pp_parser_readline(self, text, sizeof(text));
			
			if(msgType == PP_TOKEN_WARNING)
				pp_warning(self, "#warning %s", text);
			else
				pp_error(self, "#error %s", text);
			break;
		}
		default:
			pp_error(self, "unknown directive '%s'", token.theSource);
	}
}

/**
 * Includes a source file specified with the #include directive.
 * @param filename the path to include
 */
void pp_parser_include(pp_parser* self, char* filename)
{
	pp_parser incparser;
	char* buffer;
	int length;
	int bytes_read;
	int handle;
	
	// Open the file
	handle = openpackfile(filename, packfile);
#ifdef PP_TEST
	if(!handle)
#else
	if(handle < 0)
#endif
	{
		pp_error(self, "unable to open file '%s'", filename);
	}
	
	// Determine the file's size
	seekpackfile(handle, 0, SEEK_END);
	length = tellpackfile(handle);
	seekpackfile(handle, 0, SEEK_SET);
	
	// Allocate a buffer for the file's contents
	buffer = tracemalloc("pp_parser_include", length + 1);
	memset(buffer, 0, length + 1);
	
	// Read the file into the buffer
	bytes_read = readpackfile(handle, buffer, length);
	closepackfile(handle);
	
	if(bytes_read != length)
	{
		pp_error(self, "I/O error: %s", strerror(errno));
	}
	
	// Parse the source code in the buffer
	pp_parser_init(&incparser, self->script, filename, buffer);
	pp_parser_parse(&incparser);
	
	// Free the buffer to prevent memory leaks
	tracefree(buffer);
}

/**
 * Handles conditional directives.
 * @param directive the type of conditional directive
 */
void pp_parser_conditional(pp_parser* self, PP_TOKEN_TYPE directive)
{
	switch(directive)
	{
		case PP_TOKEN_IF:
		case PP_TOKEN_IFDEF:
		case PP_TOKEN_IFNDEF:
			if(num_conditionals++ > 16) pp_error(self, "too many levels of nested conditional directives");
			conditionals.all <<= 2; // push a new conditional state onto the stack
			conditionals.top = pp_parser_eval_conditional(self, directive) ? cs_true : cs_false;
			break;
		case PP_TOKEN_ELIF:
			if(conditionals.top == cs_done || conditionals.top == cs_true)
				conditionals.top = cs_done;
			else
				conditionals.top = pp_parser_eval_conditional(self, directive) ? cs_true : cs_false;
			break;
		case PP_TOKEN_ELSE:
			if(conditionals.top == cs_none) pp_error(self, "stray #else");
			conditionals.top = (conditionals.top == cs_false) ? cs_true : cs_false;
			break;
		case PP_TOKEN_ENDIF:
			if(conditionals.top == cs_none || num_conditionals-- < 0) pp_error(self, "stray #endif");
			conditionals.all >>= 2; // pop a conditional state from the stack
			break;
		default:
			pp_error(self, "unknown conditional directive type (ID=%d)", directive);
	}
}

bool pp_parser_eval_conditional(pp_parser* self, PP_TOKEN_TYPE directive)
{
	pp_token token;
	
	// all directives have whitespace between the directive and the contents
	skip_whitespace();
	
	switch(directive)
	{
		case PP_TOKEN_IFDEF:
			return List_FindByName(&macros, token.theSource);
		case PP_TOKEN_IFNDEF:
			return !List_FindByName(&macros, token.theSource);
		case PP_TOKEN_IF:
			pp_error(self, "#if directive not yet supported");
			break;
		case PP_TOKEN_ELIF:
			pp_error(self, "#elif directive not yet supported");
			break;
		default:
			pp_error(self, "internal error: evaluating an unknown conditional type");
	}
	
	return false;
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

