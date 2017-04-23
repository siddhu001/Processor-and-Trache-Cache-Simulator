%{
	#include <stdio.h>
	#include <string.h>
    #include "processor.h"
	
	extern FILE* yyin;
    void yyerror(char *);
    int yylex(void);
	
	extern void parseCfg(char*);
%}

%union {
	char* hexStr;
}

%token <hexStr> HEXNUM
%token <int> NEWLINE
%token <int> END1
%type <int> number
%type <int> numbers
%start root

%%
root:
	numbers { numofinstructions = memidx; goProcessor(); exit(0);}
	;

numbers:
	number {;}
	|numbers number {;}        
	;

number:
	HEXNUM { storeInMem($1, memidx); memidx++;}
	;


%%

void yyerror(char* s){
	fprintf(stderr, "Invalid Input.\n");
}


int main(int argc, char *argv[]) {
	if (argc != 5){
		printf("Invalid Command. Enter Proper Arguments\n");
		exit(0);
	}
	initialiseCache();
//	printf("Cache Initialised.\n");
	parseCfg(strdup(argv[2]));
	
	yyin=fopen(argv[1],"r");
	if (yyin==NULL){
		printf("File Not Found.\n");
		exit(0);
	}
	image_file = (char*)malloc(sizeof(char)*1000);
	output_file = (char*)malloc(sizeof(char)*1000);
	
	strcpy(image_file, argv[3]);
	strcpy(output_file, argv[4]);
	
	yyparse();
	return 0;
}