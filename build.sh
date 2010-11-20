#!/bin/bash

gcc -g -O2 -Wall pp_test.c pp_parser.c pp_lexer.c ../scriptlib/List.c \
	-DPP_TEST \
	-I.. -I../scriptlib -I../tracelib -I../gamelib -I../..
	-opp_test


