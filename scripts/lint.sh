#!/bin/bash
# Linting script for hyprlax
# Run this before committing or pushing to catch common issues

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Track if we found any issues
FAILED=0
FIXED=0

# Parse arguments
FIX_MODE=0
if [ "$1" = "--fix" ] || [ "$1" = "-f" ]; then
    FIX_MODE=1
    echo -e "${BLUE}Running in fix mode...${NC}"
fi

echo -e "${BLUE}=== Running hyprlax linter ===${NC}"
echo ""

# 1. Check for trailing whitespace
echo -n "1. Checking for trailing whitespace... "
TRAILING_WS=$(grep -l '[[:space:]]$' src/*.c src/*.h 2>/dev/null || true)
if [ -n "$TRAILING_WS" ]; then
    if [ $FIX_MODE -eq 1 ]; then
        find src/ \( -name "*.c" -o -name "*.h" \) -exec sed -i 's/[[:space:]]*$//' {} \;
        echo -e "${GREEN}FIXED${NC}"
        FIXED=1
    else
        echo -e "${RED}FAILED${NC}"
        echo "   Files with trailing whitespace:"
        for file in $TRAILING_WS; do
            echo "     - $file"
        done
        echo "   Run with --fix to automatically remove trailing whitespace"
        FAILED=1
    fi
else
    echo -e "${GREEN}OK${NC}"
fi

# 2. Check for missing newline at end of files
echo -n "2. Checking for missing newlines at end of files... "
MISSING_NEWLINE=""
for file in src/*.c src/*.h; do
    if [ -f "$file" ] && [ -n "$(tail -c 1 "$file" 2>/dev/null)" ]; then
        MISSING_NEWLINE="$MISSING_NEWLINE $file"
    fi
done

if [ -n "$MISSING_NEWLINE" ]; then
    if [ $FIX_MODE -eq 1 ]; then
        for file in $MISSING_NEWLINE; do
            printf '\n' >> "$file"
        done
        echo -e "${GREEN}FIXED${NC}"
        FIXED=1
    else
        echo -e "${RED}FAILED${NC}"
        echo "   Files missing newline at end:"
        for file in $MISSING_NEWLINE; do
            echo "     - $file"
        done
        echo "   Run with --fix to automatically add missing newlines"
        FAILED=1
    fi
else
    echo -e "${GREEN}OK${NC}"
fi

# 3. Check for tabs vs spaces consistency (optional - can be configured)
# Currently allowing tabs as the codebase uses them
echo -n "3. Checking indentation consistency... "
echo -e "${GREEN}OK${NC} (tabs allowed)"

# 4. Run cppcheck if available
if command -v cppcheck &> /dev/null; then
    echo -n "4. Running cppcheck static analysis... "
    CPPCHECK_OUTPUT=$(cppcheck --enable=warning --error-exitcode=1 \
                               --suppress=missingIncludeSystem \
                               --suppress=unusedFunction \
                               --suppress=constVariablePointer \
                               --suppress=normalCheckLevelMaxBranches \
                               --inline-suppr \
                               --quiet \
                               src/ipc.c src/hyprlax_ctl.c 2>&1 || true)
    
    if [ -z "$CPPCHECK_OUTPUT" ]; then
        echo -e "${GREEN}OK${NC}"
    else
        echo -e "${YELLOW}WARNING${NC}"
        echo "   cppcheck found potential issues:"
        echo "$CPPCHECK_OUTPUT" | sed 's/^/     /'
    fi
else
    echo -e "${YELLOW}4. cppcheck not installed${NC} (install with: sudo apt-get install cppcheck)"
fi

# 5. Check compilation
echo -n "5. Checking compilation... "
COMPILE_OUTPUT=$(make clean >/dev/null 2>&1 && make -j$(nproc) 2>&1)
if [ $? -eq 0 ]; then
    echo -e "${GREEN}OK${NC}"
else
    echo -e "${RED}FAILED${NC}"
    echo "   Compilation errors:"
    echo "$COMPILE_OUTPUT" | grep -E "(error:|warning:)" | head -10 | sed 's/^/     /'
    FAILED=1
fi

# 6. Check for common security issues
echo -n "6. Checking for common security issues... "
SECURITY_ISSUES=""

# Check for unsafe functions
UNSAFE_FUNCS=$(grep -n -E '\b(gets|strcpy|strcat|sprintf|vsprintf)\b' src/*.c src/*.h 2>/dev/null || true)
if [ -n "$UNSAFE_FUNCS" ]; then
    SECURITY_ISSUES="${SECURITY_ISSUES}Unsafe functions found:\n$UNSAFE_FUNCS\n"
fi

if [ -z "$SECURITY_ISSUES" ]; then
    echo -e "${GREEN}OK${NC}"
else
    echo -e "${YELLOW}WARNING${NC}"
    echo -e "   $SECURITY_ISSUES" | sed 's/^/     /'
fi

# Summary
echo ""
echo -e "${BLUE}=== Linting Summary ===${NC}"

if [ $FIXED -eq 1 ]; then
    echo -e "${GREEN}✓ Fixed issues automatically${NC}"
    echo "  Please review the changes and commit them."
fi

if [ $FAILED -ne 0 ]; then
    echo -e "${RED}✗ Found issues that need attention${NC}"
    if [ $FIX_MODE -eq 0 ]; then
        echo "  Run './scripts/lint.sh --fix' to automatically fix some issues"
    fi
    exit 1
else
    echo -e "${GREEN}✓ All checks passed!${NC}"
fi

exit 0
