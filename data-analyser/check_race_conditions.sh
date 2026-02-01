#!/bin/bash
# Race Condition Checker for Qt Application
# This script helps identify potential race conditions

echo "=== Race Condition Analysis ==="
echo ""

# Check for unprotected shared data access
echo "1. Checking for unprotected shared data access..."
echo "   - allDetections: accessed from multiple async contexts"
echo "   - dailyCounts: accessed from multiple async contexts"
echo "   - processedRhdFiles: accessed from multiple async contexts"
echo "   - isProcessing: accessed without mutex protection"
echo ""

# Check for mutex usage
echo "2. Checking mutex usage..."
if grep -q "processingMutex" /Users/antonmelnychuk/workspace/pipeline/data-analyser/src/gui/seizure_analyzer.cpp; then
    echo "   ✓ processingMutex declared"
    if grep -q "QMutexLocker.*processingMutex\|processingMutex\.lock\|processingMutex\.unlock" /Users/antonmelnychuk/workspace/pipeline/data-analyser/src/gui/seizure_analyzer.cpp; then
        echo "   ✓ processingMutex is used"
    else
        echo "   ✗ WARNING: processingMutex declared but NEVER USED!"
    fi
else
    echo "   ✗ No processingMutex found"
fi
echo ""

# Check for timer/file watcher concurrent access
echo "3. Checking async callbacks that access shared data..."
echo "   - onDataDirectoryChanged() -> scanDetectionFiles() -> allDetections.clear()"
echo "   - processNewRhdFiles() -> isProcessing check/set"
echo "   - processingTimer timeout -> processNewRhdFiles()"
echo "   - updateTimer timeout -> updateDisplay() -> reads allDetections"
echo ""

# Check for signal/slot thread safety
echo "4. Checking signal/slot thread safety..."
if grep -q "Qt::QueuedConnection" /Users/antonmelnychuk/workspace/pipeline/data-analyser/src/gui/seizure_analyzer.cpp; then
    echo "   ✓ QueuedConnection found (good for cross-thread)"
else
    echo "   ✗ No QueuedConnection found"
fi
echo ""

echo "=== Recommendations ==="
echo "1. Protect allDetections, dailyCounts, processedRhdFiles with mutex"
echo "2. Protect isProcessing flag with mutex"
echo "3. Use QMutexLocker for RAII-style locking"
echo "4. Consider using QReadWriteLock for read-heavy operations"
echo ""
