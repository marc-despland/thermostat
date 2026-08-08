#!/bin/bash

# Pairing script for KETOTEK with Zigbee Gateway
# This script automates the pairing process via serial console

PORT="${1:-/dev/ttyS3}"
BAUD="${2:-115200}"

if [ ! -e "$PORT" ]; then
    echo "Error: Serial port $PORT not found"
    echo "Usage: $0 [port] [baud]"
    echo "Example: $0 /dev/ttyS3 115200"
    exit 1
fi

echo "Connecting to gateway on $PORT at $BAUD baud..."
echo "Press Ctrl+C to exit"
echo ""
echo "Available commands once connected:"
echo "  permit_join 180  - Open network for 180 seconds"
echo "  list_devices     - List all paired devices"
echo "  remove_device 1  - Remove device 1"
echo ""

# Use screen or minicom to connect
if command -v screen &> /dev/null; then
    screen "$PORT" "$BAUD"
elif command -v minicom &> /dev/null; then
    minicom -D "$PORT" -b "$BAUD"
elif command -v picocom &> /dev/null; then
    picocom -b "$BAUD" "$PORT"
elif command -v stty &> /dev/null; then
    stty -F "$PORT" "$BAUD" -echo
    cat "$PORT"
else
    echo "Error: No serial terminal found (screen, minicom, picocom, or stty)"
    exit 1
fi
