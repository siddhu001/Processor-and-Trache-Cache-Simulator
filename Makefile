all:
	bison -y -d parser.y 
	flex lexer.l
	bison -p cfgF -d Parse.y 
	flex -P cfgF lex.l
	gcc -g -o processor_simulator Parse.tab.c lex.cfgF.c y.tab.c lex.yy.c processor.h -lm -pthread

clean:
	rm -f y.tab.* lex.yy.c Parse.tab.* lex.lex.c processor.h.gch processor_simulator cache.h.gch
