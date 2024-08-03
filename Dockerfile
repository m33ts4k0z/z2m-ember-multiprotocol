FROM ubuntu:24.04

ARG TARGETPLATFORM
ARG BUILDPLATFORM
ARG TARGETARCH
ENV TARGETARCH2=amd64
ENV OTBR_WEB_PORT=8787

RUN echo "Building for $TARGETPLATFORM from $BUILDPLATFORM on $TARGETARCH"
  
RUN apt update && apt install -y --no-install-recommends \
    build-essential \
    git \
    tar \
    xz-utils\
    ca-certificates \
    dos2unix \
    sudo \
    curl \
    systemd \
    systemd-sysv \
    dbus \
    dbus-user-session \
    libmbedtls-dev \
    iproute2 \
    lsb-release \
    bluetooth \
    bluez \
    bluez-tools \
    rfkill \
    libbluetooth-dev \
    iptables \
    nano


COPY set_targetarch.sh /set_targetarch.sh
RUN chmod +x /set_targetarch.sh
RUN /set_targetarch.sh
RUN mkdir /root/silabs-binaries
RUN tar -xvJf /root/silabs-binaries.tar.xz -C /root/silabs-binaries

# Wait 10 seconds before trying to attach the hci device for the bluetooth to work
RUN sed -i '/^ExecStart/i ExecStartPre=/bin/sleep 10' /root/silabs-binaries/systemd/hciattach.service

# Change the port for the otbr web interface
RUN PORT=${OTBR_WEB_PORT:-85} && sed -i "s/\(.*-p \)80/\1$PORT/" /root/silabs-binaries/otbr/otbr_entrypoint.sh

# Remove the sneaky - from the EnvironmentFile line
RUN sed -i 's/^EnvironmentFile=-/EnvironmentFile=/' /root/silabs-binaries/otbr/web/otbr-web.service

RUN ls -lh /root/silabs-binaries

RUN cd /root/silabs-binaries \
&& ./pre-setup.sh \
--device-path ${DEVICE_PATH} \
--baudrate ${BAUDRATE} \
--hardware-flow ${HARDWARE_FLOW} \
--disable_encryption ${DISABLE_ENCRYPTION} \
--disable-conflict-services ${DISABLE_CONFLICT_SERVICES} \
--zigbeed-iid ${ZIGBEED_IID} \
--otbr-iid ${OTBR_IID}

RUN cd /root/silabs-binaries && ./install.sh --all

# Copy the config files to their locations inside the container
COPY rootfs /

RUN chmod -x /etc/systemd/system/zigbee2mqtt.service

RUN systemctl enable zigbee2mqtt

RUN curl -fsSL https://deb.nodesource.com/setup_22.x | bash - \
    && apt install -y nodejs \
    && npm install -g npm@10

# Deploy zigbee2mqtt from source
RUN apt install -y --no-install-recommends \
    libsystemd-dev \
    linux-headers-generic \
    wget 

RUN mkdir /opt/zigbee2mqtt \
    && chown -R ${USER}: /opt/zigbee2mqtt \
    && git clone --depth 1 -b dev https://github.com/Koenkk/zigbee2mqtt.git /opt/zigbee2mqtt \
    && cd /opt/zigbee2mqtt \
    && npm ci --no-audit --no-optional --no-update-notifier \
    && npm rebuild --build-from-source

COPY configuration.yaml /opt/zigbee2mqtt/data/configuration.yaml

# Disable bluez on the container. 
# We will use the host's bluez making the BT adapter available to all other containers as long as they run --privileged and --net host
RUN service bluetooth stop
RUN systemctl mask bluetooth.service

# Update the configs
COPY update_configs.sh /update_configs.sh
RUN chmod +x /update_configs.sh
ENTRYPOINT ["/update_configs.sh"]
RUN dos2unix /usr/local/etc/*.conf

RUN wget https://github.com/mikefarah/yq/releases/latest/download/yq_linux_$TARGETARCH -O /usr/bin/yq && chmod +x /usr/bin/yq

# Start all services
CMD ["/sbin/init"]