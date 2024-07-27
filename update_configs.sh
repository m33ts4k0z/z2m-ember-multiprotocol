#!/bin/bash

set -e
set -x

# Function to read a specific section from the .env file
read_section() {
    local section=$1
    local file=$2
    sed -n "/^\[$section\]/,/^\[/p" "$file" | sed '/^\[/d; /^$/d; /^#/d'
}

# Function to update config file
update_config() {
    local file=$1
    local key=$2
    local value=$3
    local format=$4
    local convert_underscore=$5

    echo "Updating $file: key=$key, value=$value, format=$format, convert_underscore=$convert_underscore"

    if [ "$convert_underscore" = "true" ]; then
        key=$(echo "$key" | tr '_' '-')
        echo "Converted key to: $key"
    fi

    # Convert key to lowercase for case-insensitive matching
    local lowercase_key=$(echo "$key" | tr '[:upper:]' '[:lower:]')

    if [ "$format" = "colon" ]; then
        awk -v key="$lowercase_key" -v value="$value" '
        tolower($0) ~ "^[[:space:]]*"key":" {
            print key": "value
            next
        }
        {print}
        ' "$file" > "${file}.tmp" && mv "${file}.tmp" "$file"
    else
        awk -v key="$lowercase_key" -v value="$value" '
        tolower($0) ~ "^[[:space:]]*"key"=" {
            print key"="value
            next
        }
        {print}
        ' "$file" > "${file}.tmp" && mv "${file}.tmp" "$file"
    fi

    echo "Updated $file"
    grep -i "^[[:space:]]*${lowercase_key}[=:]" "$file" || echo "Key not found in file"
}

# Define mapping of sections to config files, their formats, and underscore conversion
declare -A section_to_config=(
    ["cpcd"]="/usr/local/etc/cpcd.conf:colon:false"
    ["zigbeed"]="/usr/local/etc/zigbeed.conf:equals:true"
   # ["otbr"]="/usr/local/etc/otbr.conf:equals:false"
)

# Path to your .env file
ENV_FILE="/.env"

echo "Starting config update process"
echo "Contents of .env file:"
cat "$ENV_FILE"

# Process each section
for section in "${!section_to_config[@]}"; do
    echo "Processing section: $section"
    IFS=':' read -r config_file format convert_underscore <<< "${section_to_config[$section]}"
    
    echo "Config file: $config_file"
    echo "Format: $format"
    echo "Convert underscore: $convert_underscore"
    
    while IFS='=' read -r key value; do
        # Remove any leading/trailing whitespace
        key=$(echo "$key" | xargs)
        value=$(echo "$value" | xargs)
        
        echo "Updating key: $key with value: $value"
        if [ "$key" = "RADIO_URL" ]; then
            echo "Special handling for RADIO_URL"
            awk -v value="$value" '
            /^[[:space:]]*radio-url=/ {
                print "radio-url="value
                next
            }
            {print}
            ' "$config_file" > "${config_file}.tmp" && mv "${config_file}.tmp" "$config_file"
        else
            update_config "$config_file" "$key" "$value" "$format" "$convert_underscore"
        fi
    done < <(read_section "$section" "$ENV_FILE")
    
    echo "Final contents of $config_file:"
    cat "$config_file"
done

echo "Config update process completed"

# Convert line endings to Unix format
echo "Converting config files to Unix line endings"
dos2unix /usr/local/etc/*.conf

# Run the main application
echo "Executing main command: $@"
exec "$@"