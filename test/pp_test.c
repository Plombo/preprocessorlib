// Quick program to test the preprocessor lexer.
// Compile using build.sh.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "pp_lexer.h"
#include "pp_parser.h"
#undef printf

bool lexFile(char* filename)
{
	int length;
	char* buffer;
	pp_lexer lexer;
	pp_token token;
	
	// Open the file and read it into a memory buffer
	FILE* fp = fopen(filename, "rb");
	if(fp == NULL) return false;
	fseek(fp, 0, SEEK_END);
	length = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	fprintf(stderr, "Length is %i\n", length);
	
	buffer = malloc(length + 1);
	memset(buffer, 0, length + 1);
	if(fread(buffer, 1, length, fp) != length) return false;
	fclose(fp);
	
	TEXTPOS position = {0,0};
	pp_lexer_Init(&lexer, buffer, position);
	
	do {
		if(FAILED(pp_lexer_GetNextToken(&lexer, &token))) { fprintf(stderr, "Fail.\n"); return false; }
		printf("%s", token.theSource);
	} while(token.theType != PP_TOKEN_EOF);
	
	pp_lexer_Clear(&lexer);
	free(buffer);
	
	return true;
}

bool parseFile(char* filename)
{
	int length;
	char* buffer;
	bool success = true;
	FILE* fp;
	pp_parser parser;
	
	// Open the file and determine its size
	fp = fopen(filename, "rb");
	if(fp == NULL) return false;
	fseek(fp, 0, SEEK_END);
	length = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	
	// Read the file into a memory buffer; return false if this fails for some reason
	buffer = malloc(length + 1);
	memset(buffer, 0, length + 1);
	if(fread(buffer, 1, length, fp) != length)
		success = false;
	fclose(fp);
	if(!success) return false;
	
	pp_parser_reset();
	pp_parser_init(&parser, NULL, filename, buffer);
	pp_parser_parse(&parser);
	
	// Don't forget to free the buffer!
	free(buffer);
	
	printf("%s", tokens);
	pp_parser_reset();
	
	return success;
}

int main(int argc, char** argv)
{
	char* filename;
	if(argc != 2)
	{
		printf("Usage: %s filename\n", argv[0]);
		return 1;
	}
	
	filename = argv[1];
	//bool success = lexFile(filename);
	bool success = parseFile(filename);
	return !success;
}


