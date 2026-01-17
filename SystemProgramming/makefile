CC = gcc

CFLAGS = -Wall -g -Wextra

FLEX = flex

BISON = bison

# ================================================================
# ИСХОДНЫЕ ФАЙЛЫ
# ================================================================

LEXER_SRC = lexer.l

PARSER_SRC = parser.y

AST_SRC = ast.c

CFG_SRC = cfg_builder.c

SEMANTIC_SRC = semantic.c

CALLTREE_SRC = calltree.c

CODEGEN_SRC = codegen.c              # ← ИСПРАВЛЕНО

MAIN_SRC = main.c

# ================================================================
# ЦЕЛЬ
# ================================================================

TARGET = cfg_builder

# ================================================================
# ГЕНЕРИРУЕМЫЕ ФАЙЛЫ (flex/bison)
# ================================================================

LEXER_C = lex.yy.c

PARSER_C = parser.tab.c

PARSER_H = parser.tab.h

# ================================================================
# ОБЪЕКТНЫЕ ФАЙЛЫ
# ================================================================

LEXER_O = lex.yy.o

PARSER_O = parser.tab.o

AST_O = ast.o

CFG_O = cfg_builder.o

SEMANTIC_O = semantic.o

CALLTREE_O = calltree.o

CODEGEN_O = codegen.o                # ← ИСПРАВЛЕНО

MAIN_O = main.o

# ВСЕ объектные файлы (ДЛЯ ЛИНКОВКИ)

OBJECTS = $(PARSER_O) $(LEXER_O) $(AST_O) $(CFG_O) $(SEMANTIC_O) \
	$(CALLTREE_O) $(CODEGEN_O) $(MAIN_O)   # ← ИСПРАВЛЕНО

# ================================================================
# ОСНОВНАЯ ЦЕЛЬ
# ================================================================

all: $(TARGET)

# ================================================================
# ГЕНЕРАЦИЯ ПАРСЕРА И ЛЕКСЕРА (flex/bison)
# ================================================================

$(PARSER_C) $(PARSER_H): $(PARSER_SRC)
	@echo "[*] Generating parser with bison..."
	$(BISON) -d $(PARSER_SRC)
	@echo "[+] Parser generated"

$(LEXER_C): $(LEXER_SRC) $(PARSER_H)
	@echo "[*] Generating lexer with flex..."
	$(FLEX) $(LEXER_SRC)
	@echo "[+] Lexer generated"

# ================================================================
# ПРАВИЛА КОМПИЛЯЦИИ
# ================================================================

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
	@echo "[*] Compiling CFG builder..."
	$(CC) $(CFLAGS) -c $< -o $@

$(SEMANTIC_O): $(SEMANTIC_SRC) semantic.h ast.h
	@echo "[*] Compiling semantic analyzer..."
	$(CC) $(CFLAGS) -c $< -o $@

$(CALLTREE_O): $(CALLTREE_SRC) calltree.h ast.h
	@echo "[*] Compiling call tree..."
	$(CC) $(CFLAGS) -c $< -o $@

# ИСПРАВЛЕНО: Правило для кодогенератора
$(CODEGEN_O): $(CODEGEN_SRC) codegen.h cfg.h
	@echo "[*] Compiling code generator..."
	$(CC) $(CFLAGS) -c $< -o $@

$(MAIN_O): $(MAIN_SRC) ast.h cfg.h semantic.h calltree.h codegen.h  # ← ИСПРАВЛЕНО
	@echo "[*] Compiling main..."
	$(CC) $(CFLAGS) -c $< -o $@

# ================================================================
# ЛИНКОВКА
# ================================================================

$(TARGET): $(OBJECTS)
	@echo "[*] Linking..."
	$(CC) $(CFLAGS) -o $@ $^ -lfl
	@echo "[+] Build complete: $(TARGET)"

# ================================================================
# ТЕСТОВЫЕ ЦЕЛИ
# ================================================================

test: $(TARGET)
	@echo "[*] Running test..."
	@if [ -f test.txt ]; then \
		mkdir -p output; \
		./$(TARGET) test.txt -o output; \
		echo "[+] Test complete"; \
	else \
		echo "[!] test.txt not found"; \
	fi

test_asm: $(TARGET)
	@echo "[*] Testing with assembly generation..."
	@if [ -f test.txt ]; then \
		mkdir -p output; \
		./$(TARGET) test.txt -o output -asm output/test.asm; \
		echo "[+] Assembly generated: output/test.asm"; \
	else \
		echo "[!] test.txt not found"; \
	fi

# ================================================================
# ВИЗУАЛИЗАЦИЯ (AST, CFG, Call Tree PNG)
# ================================================================

visualize: test
	@echo "[*] Generating visualizations..."
	@if [ -f output/ast_output.dot ]; then \
		dot -Tpng output/ast_output.dot -o output/ast_output.png; \
		echo "[+] AST: output/ast_output.png"; \
	fi
	@if [ -f output/cfg_output.dot ]; then \
		dot -Tpng output/cfg_output.dot -o output/cfg_output.png; \
		echo "[+] CFG: output/cfg_output.png"; \
	fi
	@if [ -f output/calltree_output.dot ]; then \
		dot -Tpng output/calltree_output.dot -o output/calltree_output.png; \
		echo "[+] Call Tree: output/calltree_output.png"; \
	fi

# ================================================================
# ОЧИСТКА
# ================================================================

clean:
	@echo "[*] Cleaning..."
	rm -f $(LEXER_C) $(PARSER_C) $(PARSER_H)
	rm -f $(LEXER_O) $(PARSER_O) $(AST_O) $(CFG_O) $(SEMANTIC_O)
	rm -f $(CALLTREE_O) $(CODEGEN_O) $(MAIN_O)
	rm -f $(TARGET) *.output
	@echo "[+] Clean complete"

distclean: clean
	rm -rf output/ *.dot *.png *.asm *.bin
	@echo "[+] Distclean complete"

# ================================================================
# СПРАВКА И ИНФОРМАЦИЯ
# ================================================================

info:
	@echo ""
	@echo "╔════════════════════════════════════════════════════════════╗"
	@echo "║ CFG Builder v5.0 - With Code Generator                    ║"
	@echo "╚════════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "📦 Modules:"
	@echo " ✓ Lexer (lexer.l)"
	@echo " ✓ Parser (parser.y)"
	@echo " ✓ AST Builder (ast.c)"
	@echo " ✓ CFG Builder (cfg_builder.c)"
	@echo " ✓ Semantic Analysis (semantic.c)"
	@echo " ✓ Call Tree Analysis (calltree.c)"
	@echo " ✓ Code Generator (codegen.c)"
	@echo ""
	@echo "🔗 Dependencies:"
	@echo " codegen.c requires:"
	@echo " - codegen.h"
	@echo " - cfg.h"
	@echo ""

help:
	@echo ""
	@echo "╔════════════════════════════════════════════════════════════╗"
	@echo "║ CFG Builder v5.0 - With Code Generator                    ║"
	@echo "╚════════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "📋 Build targets:"
	@echo " make            - Build cfg_builder (default)"
	@echo " make clean      - Remove build artifacts"
	@echo " make distclean  - Full cleanup (including output/)"
	@echo ""
	@echo "🧪 Testing targets:"
	@echo " make test       - Test with test.txt (outputs AST, CFG, etc.)"
	@echo " make test_asm   - Test with assembly generation"
	@echo " make visualize  - Test and generate PNG graphs"
	@echo ""
	@echo "ℹ️ Info targets:"
	@echo " make info       - Show module information"
	@echo " make help       - Show this help"
	@echo ""
	@echo "📁 Output files (in output/ directory):"
	@echo " ast_output.dot          - Abstract Syntax Tree"
	@echo " cfg_output.dot          - Control Flow Graph"
	@echo " calltree_output.dot     - Call Tree"
	@echo " test.asm                - Generated assembly code"
	@echo ""
	@echo "💻 Usage examples:"
	@echo " ./cfg_builder test.txt -o output"
	@echo " ./cfg_builder test.txt -o output -asm output/test.asm"
	@echo ""
	@echo "🆕 Code Generator:"
	@echo " - Works directly with CFG graph"
	@echo " - Generates NOOBIK assembly code"
	@echo " - Supports function calls and recursion"
	@echo " - Stack-based variable management"
	@echo " - Register allocation and management"
	@echo ""

.PHONY: all clean distclean test test_asm visualize help info
