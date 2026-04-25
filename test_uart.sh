#!/bin/bash
PORT=${1:-/dev/ttyUSB0}
BAUD=115200
TIMEOUT=5

stty -F "$PORT" "$BAUD" cs8 -cstopb -parenb raw -echo

echo "Reading $PORT for ${TIMEOUT}s..."
OUTPUT=$(timeout "$TIMEOUT" cat "$PORT" 2>/dev/null)

if echo "$OUTPUT" | grep -q "\[A\]" && echo "$OUTPUT" | grep -q "\[B\]"; then
    echo "PASS: both tasks printing"
    echo "$OUTPUT" | grep -E "\[A\]|\[B\]" | head -20
elif echo "$OUTPUT" | grep -qE "\[A\]|\[B\]|\[QLONQ\]|booting"; then
    echo "PARTIAL: got some output but not both tasks"
    echo "$OUTPUT" | head -20
elif [ -z "$OUTPUT" ]; then
    echo "FAIL: no output on $PORT"
else
    echo "FAIL: unexpected output"
    echo "$OUTPUT" | head -20 | cat -v
fi
