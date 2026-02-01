#!/bin/bash
# Static Analysis Runner Script
# Runs Clang Static Analyzer and Cppcheck

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GUI_DIR="$SCRIPT_DIR/src/gui"

echo "=== Static Analysis ==="
echo ""

# Check for tools
command -v scan-build >/dev/null 2>&1 || {
    echo "⚠ scan-build not found. Install with:"
    echo "  macOS: brew install llvm"
    echo "  Linux: sudo apt-get install clang-tools"
    echo ""
    SCAN_BUILD_AVAILABLE=false
}

command -v cppcheck >/dev/null 2>&1 || {
    echo "⚠ cppcheck not found. Install with:"
    echo "  macOS: brew install cppcheck"
    echo "  Linux: sudo apt-get install cppcheck"
    echo ""
    CPPCHECK_AVAILABLE=false
}

# Clang Static Analyzer
if [ "$SCAN_BUILD_AVAILABLE" != "false" ]; then
    echo "Running Clang Static Analyzer..."
    cd "$GUI_DIR"
    
    # Clean previous results
    rm -rf scan-build-results 2>/dev/null || true
    
    # Run scan-build
    scan-build -o scan-build-results \
        --use-analyzer=$(which clang++) \
        qmake 2>&1 | tee /tmp/scan-build.log
    
    scan-build -o scan-build-results \
        --use-analyzer=$(which clang++) \
        make 2>&1 | tee -a /tmp/scan-build.log
    
    echo ""
    echo "✓ Clang Static Analyzer complete"
    echo "  Results: $GUI_DIR/scan-build-results"
    echo "  View with: open $GUI_DIR/scan-build-results/index.html"
    echo ""
fi

# Cppcheck
if [ "$CPPCHECK_AVAILABLE" != "false" ]; then
    echo "Running Cppcheck..."
    cd "$GUI_DIR"
    
    cppcheck --enable=all \
        --suppress=missingIncludeSystem \
        --suppress=unusedFunction \
        --suppress=unmatchedSuppression \
        --xml --xml-version=2 \
        seizure_analyzer.cpp 2> cppcheck_results.xml
    
    # Also generate human-readable output
    cppcheck --enable=all \
        --suppress=missingIncludeSystem \
        --suppress=unusedFunction \
        --suppress=unmatchedSuppression \
        seizure_analyzer.cpp 2>&1 | tee cppcheck_results.txt
    
    echo ""
    echo "✓ Cppcheck complete"
    echo "  Results: $GUI_DIR/cppcheck_results.txt"
    echo ""
fi

echo "=== Analysis Complete ==="
