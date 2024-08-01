FROM ubuntu:24.04

ARG TARGETPLATFORM
ARG BUILDPLATFORM
ARG TARGETARCH
ENV TARGETARCH2=amd64

RUN echo "Building for $TARGETPLATFORM from $BUILDPLATFORM on $TARGETARCH"
  
RUN apt update && apt install -y --no-install-recommends \
    build-essential \
    socat \
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
    libmbedtls-dev

COPY set_targetarch.sh /set_targetarch.sh
RUN chmod +x /set_targetarch.sh
RUN /set_targetarch.sh
RUN mkdir /root/silabs-binaries
RUN tar -xvJf /root/silabs-binaries.tar.xz -C /root/silabs-binaries

RUN ls -lh /root/silabs-binaries

RUN cd /root/silabs-binaries \
&& chmod +x pre-setup.sh \
&& ./pre-setup.sh \
--device-path ${DEVICE_PATH} \
--baudrate ${BAUDRATE} \
--hardware-flow ${HARDWARE_FLOW} \
--disable_encryption ${DISABLE_ENCRYPTION} \
--disable-conflict-services ${DISABLE_CONFLICT_SERVICES} \
--zigbeed-iid ${ZIGBEED_IID} \
--otbr-iid ${OTBR_IID} && sleep 10

RUN cd /root/silabs-binaries && ./install.sh --all

# Copy the config files to their locations inside the container
COPY rootfs /

RUN systemctl enable zigbee2mqtt

# Deploy zigbee2mqtt from source
RUN apt install -y --no-install-recommends \
    libsystemd-dev \
    npm \
    nodejs \
    linux-headers-generic \
    wget 

RUN mkdir /opt/zigbee2mqtt \
    && chown -R ${USER}: /opt/zigbee2mqtt \
    && git clone --depth 1 -b dev https://github.com/Koenkk/zigbee2mqtt.git /opt/zigbee2mqtt \
    && cd /opt/zigbee2mqtt \
    && npm ci --no-audit --no-optional --no-update-notifier \
    && npm rebuild --build-from-source

COPY configuration.yaml /opt/zigbee2mqtt/data/configuration.yaml

# Update the configs
COPY update_configs.sh /update_configs.sh
RUN chmod +x /update_configs.sh
ENTRYPOINT ["/update_configs.sh"]
RUN dos2unix /usr/local/etc/*.conf

RUN wget https://github.com/mikefarah/yq/releases/latest/download/yq_linux_$TARGETARCH -O /usr/bin/yq && chmod +x /usr/bin/yq

# Start all services
CMD ["/sbin/init"]