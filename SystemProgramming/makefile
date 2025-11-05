CC = gcc
CFLAGS = -Wall -g
FLEX = flex
BISON = bison

LEXER_SRC = lexer.l
PARSER_SRC = parser.y
TARGET = parser
AST_SRC = ast.c

LEXER_C = lex.yy.c
PARSER_C = parser.tab.c
PARSER_H = parser.tab.h
AST_O = ast.o

LEXER_O = lex.yy.o
PARSER_O = parser.tab.o

build: all

all: $(TARGET)

$(TARGET): $(LEXER_O) $(PARSER_O) $(AST_O)
	$(CC) $(CFLAGS) -o $@ $^ -lfl

$(LEXER_O): $(LEXER_C) $(PARSER_H)
	$(CC) $(CFLAGS) -c $<

$(PARSER_O): $(PARSER_C)
	$(CC) $(CFLAGS) -c $<

$(AST_O): $(AST_SRC) ast.h
	$(CC) $(CFLAGS) -c $(AST_SRC)

$(LEXER_C): $(LEXER_SRC)
	$(FLEX) $<

$(PARSER_C) $(PARSER_H): $(PARSER_SRC)
	$(BISON) -d $<

clean:
	rm -f $(LEXER_C) $(PARSER_C) $(PARSER_H) $(LEXER_O) $(PARSER_O) $(AST_O) $(TARGET) *.output ast.dot ast.png

distclean: clean
	rm -f *~ *.bak

debug: CFLAGS += -DYYDEBUG=1
debug: $(TARGET)

test: $(TARGET)
	@echo "Testing parser..."
	@echo 'method test() begin end;' | ./$(TARGET)

visualize: $(TARGET)
	@echo "Creating AST visualization..."
	./$(TARGET) < test_program.txt
	@if [ -f ast.dot ]; then \
		echo "Generating PNG..."; \
		dot -Tpng ast.dot -o ast.png; \
		echo "AST visualization saved as ast.png"; \
	else \
		echo "AST dot file not created"; \
	fi

help:
	@echo "Available targets:"
	@echo "  all         - Build parser (default)"
	@echo "  clean       - Remove generated files"
	@echo "  distclean   - Full cleanup"
	@echo "  debug       - Build with debug info"
	@echo "  test        - Test parser"
	@echo "  visualize   - Create AST visualization"
	@echo "  install-graphviz - Install Graphviz for visualization"
	@echo "  help        - Show this help"

.PHONY: all clean distclean debug test help visualize install-graphviz
