#!/bin/bash

cd test
gcc -g -O0 -Wall ../infixparser.c ../calculator.c ../pp_lexer.c \
	../../scriptlib/List.c \
	-DPP_TEST \
	-I.. -I../.. -I../../scriptlib -I../../tracelib -I../../gamelib -I../../.. -I../../ramlib \
	-o../infixparser


