#!/bin/bash

echo "Starting zigbeed startup script"

# Function to read .env file
read_env() {
    if [ -f .env ]; then
        while IFS= read -r line; do
            # Ignore comments and empty lines
            if [[ $line != \#* ]] && [[ -n "$line" ]]; then
                # Extract section name (if present)
                if [[ $line == \[*\] ]]; then
                    section=${line#[}
                    section=${section%]}
                else
                    # Extract key and value
                    key=${line%%=*}
                    value=${line#*=}
                    # Remove any surrounding quotes from the value
                    value=$(echo $value | sed -e 's/^"//' -e 's/"$//')
                    # Export the variable with section prefix if applicable
                    if [ -n "$section" ]; then
                        export "${section}_${key}=$value"
                    else
                        export "$key=$value"
                    fi
                fi
            fi
        done < .env
    else
        echo "Error: .env file not found"
        exit 1
    fi
}

# Read the .env file
read_env

# Rest of the script remains the same
if [ "$zigbeed_EZSP_INTERFACE" = "/tmp/ttyZigbeeNCPTCP" ]; then
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
else
    echo "EZSP_INTERFACE is not set to /tmp/ttyZigbeeNCPTCP, starting zigbeed directly"
fi

# Check if zigbeed exists and is executable
if [ ! -x "/usr/local/bin/zigbeed" ]; then
    echo "Error: /usr/local/bin/zigbeed not found or not executable"
    ls -l /usr/local/bin/zigbeed
    exit 1
fi

# Start zigbeed
echo "Executing: /usr/bin/stdbuf -o0 /usr/local/bin/zigbeed"
/usr/bin/stdbuf -o0 /usr/local/bin/zigbeed