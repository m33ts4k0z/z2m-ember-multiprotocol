#!/bin/bash

echo "Starting zigbeed startup script"

# Wait for the socat device to be created
timeout=60
counter=0
while [ ! -e /tmp/ttyZigbeeNCPTCP ]; do
    echo "Waiting for /tmp/ttyZigbeeNCPTCP to be created... ($counter/$timeout)"
    sleep 1
    counter=$((counter + 1))
    if [ $counter -ge $timeout ]; then
        echo "Timeout waiting for /tmp/ttyZigbeeNCPTCP"
        exit 1
    fi
done

echo "/tmp/ttyZigbeeNCPTCP found, starting zigbeed"

# Check if zigbeed exists and is executable
if [ ! -x "/usr/local/bin/zigbeed" ]; then
    echo "Error: /usr/local/bin/zigbeed not found or not executable"
    ls -l /usr/local/bin/zigbeed
    exit 1
fi

# Start zigbeed
echo "Executing: /usr/bin/stdbuf -o0 /usr/local/bin/zigbeed"
/usr/bin/stdbuf -o0 /usr/local/bin/zigbeed