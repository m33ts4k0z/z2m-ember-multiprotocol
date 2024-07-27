#!/bin/bash

# Function to update a config file
update_config() {
    section=$1
    config_file=$2
    
    # Read the .env file, extract the specified section
    sed -n "/^\[$section\]/,/^\[/p" /.env | sed '/^\[/d' | while read line
    do
        if [[ $line == *"="* ]]; then
            key=$(echo $line | cut -d'=' -f1)
            value=$(echo $line | cut -d'=' -f2)
            # Replace placeholders in the config file
            sed -i "s|{{$key}}|$value|g" $config_file
        fi
    done
}

# Update specific config files
update_config "SECTION1" "/path/to/config1.yaml"
update_config "SECTION2" "/path/to/config2.conf"

# Run the main application
exec "$@"