#!/bin/bash
# ThreadSanitizer Runner Script
# This script builds and runs your application with ThreadSanitizer enabled

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GUI_DIR="$SCRIPT_DIR/src/gui"
BUILD_DIR="$GUI_DIR/tsan_build"

echo "=== ThreadSanitizer Build ==="
echo ""

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Backup original .pro file if it exists
if [ -f "$GUI_DIR/seizure_analyzer.pro" ]; then
    if [ ! -f "$GUI_DIR/seizure_analyzer.pro.backup" ]; then
        cp "$GUI_DIR/seizure_analyzer.pro" "$GUI_DIR/seizure_analyzer.pro.backup"
        echo "✓ Backed up seizure_analyzer.pro"
    fi
fi

# Create temporary .pro file with TSan flags
cat > "$BUILD_DIR/seizure_analyzer_tsan.pro" << 'EOF'
# ThreadSanitizer build configuration
CONFIG += debug
QMAKE_CXXFLAGS += -fsanitize=thread -g -O1
QMAKE_LDFLAGS += -fsanitize=thread
QMAKE_CXXFLAGS_RELEASE -= -O2
QMAKE_CXXFLAGS_RELEASE += -O1

# Include original .pro file
include(../seizure_analyzer.pro)
EOF

echo "Building with ThreadSanitizer..."
echo "  - This will be slower than normal (2-10x)"
echo "  - Memory usage will be higher (2-5x)"
echo ""

# Run qmake with TSan flags
qmake "$BUILD_DIR/seizure_analyzer_tsan.pro" \
    QMAKE_CXXFLAGS+="-fsanitize=thread -g -O1" \
    QMAKE_LDFLAGS+="-fsanitize=thread"

# Build
make clean 2>/dev/null || true
make -j$(sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo ""
echo "=== Build Complete ==="
echo ""
echo "To run with ThreadSanitizer:"
echo "  cd $BUILD_DIR"
echo "  ./seizure_analyzer"
echo ""
echo "ThreadSanitizer will automatically detect and report any race conditions."
echo "Look for output starting with 'WARNING: ThreadSanitizer:'"
echo ""

# Ask if user wants to run now
read -p "Run application now? (y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    echo "Running application..."
    echo "Perform various actions (click buttons, change folders, etc.)"
    echo "Press Ctrl+C to stop"
    echo ""
    ./seizure_analyzer 2>&1 | tee tsan_output.log
    echo ""
    echo "Output saved to tsan_output.log"
fi
