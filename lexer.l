%option noyywrap
%{
    #include "y.tab.h"
	#include <stdio.h>
	#include <stdlib.h>
	#include <string.h>
%}

%%
[0-9a-fA-F]+ {yylval.hexStr = (char*) malloc(sizeof(char)*9); strcpy(yylval.hexStr, yytext);return HEXNUM;}
"\n"	{;}
.	{printf("Unknown Character.\n"); exit(0);}
%%
