#!/bin/bash

gcc -g -O2 -Wall pp_test.c pp_parser.c pp_lexer.c List.c \
	-DPP_TEST \
	-I.. -I../scriptlib -I../tracelib -I../gamelib -I../.. -I../ramlib \
	-opp_test


