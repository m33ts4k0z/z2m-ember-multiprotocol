#!/bin/bash

# Source the environment file if it exists
if [ -f /.env ]; then
    source /.env
fi

# Default port if not provided
PORT=${OTBR_WEB_PORT:-8787}

# Update the port in the configuration file
sed -i "s/-p [0-9]\+/-p $PORT/" /etc/default/otbr-web

echo "OTBR configuration updated"