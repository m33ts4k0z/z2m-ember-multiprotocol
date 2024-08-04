FROM ubuntu:24.04

ARG TARGETPLATFORM
ARG BUILDPLATFORM
ARG TARGETARCH
ENV TARGETARCH2=amd64

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

# Wait 10 seconds before trying to attach the hci device for the bluetooth to work.
# Maybe this delay is not needed, testing needed.
RUN sed -i '/^ExecStart/i ExecStartPre=/bin/sleep 10' /root/silabs-binaries/systemd/hciattach.service

# Remove the bluetooth dependency from the hciattach service since we will use the host's bluetooth
RUN sed -i '/^BindsTo=/ s/ bluetooth\.service//; /^After=/ s/ bluetooth\.service//' /root/silabs-binaries/systemd/hciattach.service

# Wait 20 seconds before starting the otbr web service so we have enough time to edit the config
RUN sed -i '/^ExecStart/i ExecStartPre=/bin/sleep 20' /root/silabs-binaries/otbr/web/otbr-web.service

ARG OTBR_WEB_PORT
RUN OTBR_WEB_PORT=$(grep OTBR_WEB_PORT .env | cut -d '=' -f2) && \
    echo "OTBR_WEB_OPTS=\"-I wpan0 -d7 -p $OTBR_WEB_PORT\"" > /etc/default/otbr-web

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
RUN chmod -x /etc/systemd/system/update_otbr_web.service

RUN systemctl enable zigbee2mqtt
RUN systemctl enable update_otbr_web

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
COPY update_otbr_web.sh /update_otbr_web.sh

RUN chmod +x /update_configs.sh 
RUN chmod +x /update_otbr_web.sh

ENTRYPOINT ["/update_configs.sh"]
RUN dos2unix /usr/local/etc/*.conf

RUN wget https://github.com/mikefarah/yq/releases/latest/download/yq_linux_$TARGETARCH -O /usr/bin/yq && chmod +x /usr/bin/yq

# Start all services
CMD ["/sbin/init"]