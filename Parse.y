%{
	#include <stdio.h>
  #include <string.h>
  #include <stdbool.h>
  #include <stdlib.h>
  extern FILE* cfgFin;
  void cfgFerror(char *);
  int cfgFlex(void);
  
  int cacheset=0;
  int cache1 = 1;
  int cache2 = 2; 
%}

%union
{
  float fvalue;
  int value;
  char *string;
}

%token HASH
%token ANY
%token ICACHE
%token DCACHE
%token CORE
%token DRAM
%token PERFECT
%token FALSE
%token TRUE
%token SIZE
%token ASSOC
%token REPLACE
%token WRITETHROUGH
%token BSIZE
%token<value> NUMBER
%token EQUAL
%token REPPolicy
%token<fvalue> DECIMAL
%token NEWLINE
%token FREQUENCY
%token LATENCY
%start Input
%% 

Input:
  /* empty */
  |LINE  NEWLINE Input
  ;
LINE:
  /* empty */
  |HASH  {;}
  |ICACHE   {cacheset=0;}
  |DCACHE   {cacheset=1;}
  |PERFECT EQUAL FALSE {if (cacheset==0) set_par_cache(cache2,1,0); else set_par_cache(cache1,1,0);}
  |PERFECT EQUAL TRUE {if (cacheset==0) set_par_cache(cache2,1,1); else set_par_cache(cache1,1,1);}
  |SIZE EQUAL NUMBER   {if (cacheset==0) set_par_cache(cache2,2,$3); else set_par_cache(cache1,2,$3);}
  |ASSOC EQUAL NUMBER  {if (cacheset==0) set_par_cache(cache2,3,$3); else set_par_cache(cache1,3,$3);}
  |REPLACE EQUAL REPPolicy {}
  |WRITETHROUGH EQUAL NUMBER {if (cacheset==0) set_par_cache(cache2,5,$3); else set_par_cache(cache1,5,$3);}
  |BSIZE EQUAL NUMBER  {if (cacheset==0) set_par_cache(cache2,6,$3); else set_par_cache(cache1,6,$3);}
  |CORE                 {}
  |DRAM                 {}
  |FREQUENCY EQUAL DECIMAL {printf("!%f\n",$3); set_frequency($3);}
  |LATENCY EQUAL NUMBER     {printf("#%d\n",$3);set_latency($3);}
  ;
%%

void cfgFerror(char* s){
    fprintf(stderr, "Invalid Input in CFG File./n");
    exit(-1);
}

void parseCfg(char *argv) {
  cfgFin=fopen(argv,"r");
  
  if (cfgFin==NULL){
    printf("CFG File Not Found.\n");
    exit(0);
  }
  cfgFparse();
  //printf("CFG file => Cache initialised.");
  fclose(cfgFin);
}