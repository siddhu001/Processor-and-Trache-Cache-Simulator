all:
	gcc processor.h -pthread
	bison -y -d parser.y 
	flex lexer.l
	gcc -o processor_simulator y.tab.c lex.yy.c processor.h -lm -pthread

clean:
	rm -f y.tab.* lex.yy.c processor_simulator