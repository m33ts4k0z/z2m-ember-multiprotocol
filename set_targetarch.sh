#!/bin/sh
set -x
echo "Running set_targetarch.sh"
echo "TARGETARCH is $TARGETARCH"
if [ "$TARGETARCH" = "amd64" ]; then
    TARGETARCH2="x86_64"
elif [ "$TARGETARCH" = "arm" ]; then
    TARGETARCH2="arm32"
else
    TARGETARCH2="$TARGETARCH"
fi
echo "TARGETARCH2 is $TARGETARCH2"
curl -L -o /root/silabs-binaries.tar.xz \
"https://github.com/Nerivec/silabs-multiprotocol-builder/releases/download/v2024.6.1/silabs-multiprotocol-components-${TARGETARCH2}-ubuntu_latest.tar.xz"
