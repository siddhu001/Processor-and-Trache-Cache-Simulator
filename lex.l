%option noyywrap
%{
	#include "Parse.tab.h"
	#include <stdio.h>
	#include <stdlib.h>
	#include <string.h>
%}

%%
#[^\n]* {return HASH;}
"[perf_model/l1_icache]" {return ICACHE;}
"[perf_model/l1_dcache]" {return DCACHE;}
"[perf_model/core]" {printf("core\n");return CORE;}
"[perf_model/dram]" {printf("dram\n");return DRAM;}
"perfect" {return PERFECT;}
"false" {return FALSE;}
"true" {return TRUE;}
"cache_size" {return SIZE;}
"associativity" {return ASSOC;}
"replacement_policy" {return REPLACE;}
"writethrough" {return WRITETHROUGH;}
"block_size"   {return BSIZE;}
"frequency"    {return FREQUENCY;}
"latency"      {return LATENCY;}
[0-9]+       {cfgFlval.value = atoi(cfgFtext);return NUMBER;}
"="	     {return EQUAL;}
[a-z]+       {cfgFlval.string=strdup(cfgFtext);return REPPolicy;}
([0-9]+)(\.[0-9]+) {cfgFlval.fvalue = atof(cfgFtext);return DECIMAL;}
"\n"         {return NEWLINE;}
[ \t]	{}
.	{cfgFerror("Unknown Character"); exit(0);}
%%
