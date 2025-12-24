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
CODEGEN_CFG_SRC = codegen_cfg.c           # ← НОВОЕ: новый кодогенератор
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
CODEGEN_CFG_O = codegen_cfg.o            # ← НОВОЕ: объектный файл нового кодогена
MAIN_O = main.o

# ВСЕ объектные файлы (ДЛЯ ЛИНКОВКИ)
OBJECTS = $(PARSER_O) $(LEXER_O) $(AST_O) $(CFG_O) $(SEMANTIC_O) \
          $(CALLTREE_O) $(CODEGEN_CFG_O) $(MAIN_O)

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

# НОВОЕ: Правило для кодогенератора
$(CODEGEN_CFG_O): $(CODEGEN_CFG_SRC) codegen_cfg.h cfg.h semantic.h ast.h
	@echo "[*] Compiling new code generator (codegen_cfg)..."
	$(CC) $(CFLAGS) -c $< -o $@

$(MAIN_O): $(MAIN_SRC) ast.h cfg.h semantic.h calltree.h codegen_cfg.h
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
# АССЕМБЛИРОВАНИЕ ДЛЯ ВИРТУАЛЬНОЙ МАШИНЫ
# ================================================================

assemble: test_asm
	@echo "[*] Assembling for Virtual Machine..."
	@if [ -f output/test.asm ]; then \
		python3 vm_assembler.py output/test.asm output/test.bin 2>/dev/null || \
		echo "    (VM assembler not implemented yet)"; \
		echo "[!] To test assembly, run: python3 vm_assembler.py output/test.asm"; \
	else \
		echo "[!] No assembly file found"; \
	fi

# ================================================================
# ОЧИСТКА
# ================================================================

clean:
	@echo "[*] Cleaning..."
	rm -f $(LEXER_C) $(PARSER_C) $(PARSER_H)
	rm -f $(LEXER_O) $(PARSER_O) $(AST_O) $(CFG_O) $(SEMANTIC_O)
	rm -f $(CALLTREE_O) $(CODEGEN_CFG_O) $(MAIN_O)
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
	@echo "║         CFG Builder v5.0 - With New Code Generator        ║"
	@echo "║                    (codegen_cfg)                           ║"
	@echo "╚════════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "📦 Modules:"
	@echo "   ✓ Lexer (lexer.l)"
	@echo "   ✓ Parser (parser.y)"
	@echo "   ✓ AST Builder (ast.c)"
	@echo "   ✓ CFG Builder (cfg_builder.c)"
	@echo "   ✓ Semantic Analysis (semantic.c)"
	@echo "   ✓ Call Tree Analysis (calltree.c)"
	@echo "   ✓ Code Generator (codegen_cfg.c) ← NEW!"
	@echo ""
	@echo "🔗 Dependencies:"
	@echo "   codegen_cfg.c requires:"
	@echo "      - codegen_cfg.h"
	@echo "      - cfg.h"
	@echo "      - semantic.h"
	@echo "      - ast.h"
	@echo ""

help:
	@echo ""
	@echo "╔════════════════════════════════════════════════════════════╗"
	@echo "║         CFG Builder v5.0 - With New Code Generator        ║"
	@echo "║                    (codegen_cfg)                           ║"
	@echo "╚════════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "📋 Build targets:"
	@echo "   make              - Build cfg_builder (default)"
	@echo "   make clean        - Remove build artifacts"
	@echo "   make distclean    - Full cleanup (including output/)"
	@echo ""
	@echo "🧪 Testing targets:"
	@echo "   make test         - Test with test.txt (outputs AST, CFG, etc.)"
	@echo "   make test_asm     - Test with assembly generation"
	@echo "   make visualize    - Test and generate PNG graphs"
	@echo "   make assemble     - Assemble generated code for VM"
	@echo ""
	@echo "ℹ️  Info targets:"
	@echo "   make info         - Show module information"
	@echo "   make help         - Show this help"
	@echo ""
	@echo "📁 Output files (in output/ directory):"
	@echo "   ast_output.dot    - Abstract Syntax Tree"
	@echo "   cfg_output.dot    - Control Flow Graph"
	@echo "   calltree_output.dot - Call Tree"
	@echo "   test.asm          - Generated assembly code"
	@echo ""
	@echo "💻 Usage examples:"
	@echo "   ./cfg_builder test.txt -o output"
	@echo "   ./cfg_builder test.txt -o output -asm output/test.asm"
	@echo ""
	@echo "🆕 New Code Generator (codegen_cfg):"
	@echo "   - Works directly with your CFG graph"
	@echo "   - Generates assembly from op_tree"
	@echo "   - Supports all CFG node types"
	@echo "   - Includes register management"
	@echo "   - Exports assembly code"
	@echo ""
	@echo "📚 Documentation:"
	@echo "   README_CODEGEN.txt  - Complete documentation"
	@echo "   INTEGRATION.txt     - Integration examples"
	@echo "   codegen_cfg_h.txt   - Header file (copy to codegen_cfg.h)"
	@echo "   codegen_cfg_c.txt   - Implementation (copy to codegen_cfg.c)"
	@echo ""

.PHONY: all clean distclean test test_asm visualize assemble help info
