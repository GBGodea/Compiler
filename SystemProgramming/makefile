CC = gcc
CFLAGS = -Wall -g -Wextra
FLEX = flex
BISON = bison

LEXER_SRC = lexer.l
PARSER_SRC = parser.y
AST_SRC = ast.c
CFG_SRC = cfg_builder.c
SEMANTIC_SRC = semantic.c
CALLTREE_SRC = calltree.c
MAIN_SRC = main.c

TARGET = cfg_builder

LEXER_C = lex.yy.c
PARSER_C = parser.tab.c
PARSER_H = parser.tab.h

LEXER_O = lex.yy.o
PARSER_O = parser.tab.o
AST_O = ast.o
CFG_O = cfg_builder.o
SEMANTIC_O = semantic.o
CALLTREE_O = calltree.o
MAIN_O = main.o

OBJECTS = $(PARSER_O) $(LEXER_O) $(AST_O) $(CFG_O) $(SEMANTIC_O) $(CALLTREE_O) $(MAIN_O)

all: $(TARGET)

$(PARSER_C) $(PARSER_H): $(PARSER_SRC)
	@echo "[*] Generating parser with bison..."
	$(BISON) -d $(PARSER_SRC)
	@echo "[+] Parser generated"

$(LEXER_C): $(LEXER_SRC) $(PARSER_H)
	@echo "[*] Generating lexer with flex..."
	$(FLEX) $(LEXER_SRC)
	@echo "[+] Lexer generated"

$(LEXER_O): $(LEXER_C)
	@echo "[*] Compiling lexer..."
	$(CC) $(CFLAGS) -c $< -o $@

$(PARSER_O): $(PARSER_C)
	@echo "[*] Compiling parser..."
	$(CC) $(CFLAGS) -c $< -o $@

$(AST_O): $(AST_SRC) ast.h
	@echo "[*] Compiling AST..."
	$(CC) $(CFLAGS) -c $< -o $@

$(CFG_O): $(CFG_SRC) cfg.h ast.h semantic.h
	@echo "[*] Compiling CFG builder (extended)..."
	$(CC) $(CFLAGS) -c $< -o $@

$(SEMANTIC_O): $(SEMANTIC_SRC) semantic.h ast.h
	@echo "[*] Compiling semantic analyzer..."
	$(CC) $(CFLAGS) -c $< -o $@

$(CALLTREE_O): $(CALLTREE_SRC) calltree.h ast.h
	@echo "[*] Compiling call tree..."
	$(CC) $(CFLAGS) -c $< -o $@

$(MAIN_O): $(MAIN_SRC) ast.h cfg.h semantic.h calltree.h
	@echo "[*] Compiling main..."
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	@echo "[*] Linking..."
	$(CC) $(CFLAGS) -o $@ $^ -lfl
	@echo "[+] Build complete: $(TARGET)"

test: $(TARGET)
	@echo "[*] Running test..."
	@if [ -f test.txt ]; then \
		mkdir -p output; \
		./$(TARGET) test.txt -o output; \
		echo "[+] Test complete"; \
	else \
		echo "[!] test.txt not found"; \
	fi

visualize: test
	@echo "[*] Generating visualizations..."
	@if [ -f output/ast_output.dot ]; then \
		dot -Tpng output/ast_output.dot -o output/ast_output.png; \
		echo "[+] AST: output/ast_output.png"; \
	fi
	@if [ -f output/cfg_output.dot ]; then \
		dot -Tpng output/cfg_output.dot -o output/cfg_output.png; \
		echo "[+] CFG with expression trees: output/cfg_output.png"; \
	fi
	@if [ -f output/calltree_output.dot ]; then \
		dot -Tpng output/calltree_output.dot -o output/calltree_output.png; \
		echo "[+] Call Tree: output/calltree_output.png"; \
	fi

clean:
	@echo "[*] Cleaning..."
	rm -f $(LEXER_C) $(PARSER_C) $(PARSER_H)
	rm -f $(LEXER_O) $(PARSER_O) $(AST_O) $(CFG_O) $(SEMANTIC_O) $(CALLTREE_O) $(MAIN_O)
	rm -f $(TARGET) *.output
	@echo "[+] Clean complete"

distclean: clean
	rm -rf output/ *.dot *.png
	@echo "[+] Distclean complete"

help:
	@echo ""
	@echo "╔════════════════════════════════════════════════════════════╗"
	@echo "║      CFG Builder v3.0 - With Expression Trees            ║"
	@echo "╚════════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "Build targets:"
	@echo "  make              - Build cfg_builder"
	@echo "  make clean        - Remove build artifacts"
	@echo "  make distclean    - Full cleanup"
	@echo ""
	@echo "Testing targets:"
	@echo "  make test         - Test with test.txt"
	@echo "  make visualize    - Test and generate PNG images"
	@echo ""
	@echo "Output files:"
	@echo "  ast_output.dot    - Abstract Syntax Tree"
	@echo "  cfg_output.dot    - Control Flow Graph WITH Expression Trees"
	@echo "  calltree_output.dot - Call Tree (function calls)"
	@echo ""
	@echo "Usage:"
	@echo "  ./cfg_builder test.txt -o output"
	@echo ""

.PHONY: all clean distclean test visualize help
