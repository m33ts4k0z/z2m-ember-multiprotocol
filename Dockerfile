FROM debian:bookworm

ARG TARGETPLATFORM
ARG BUILDPLATFORM
ARG TARGETARCH

RUN echo "Building for $TARGETPLATFORM from $BUILDPLATFORM on $TARGETARCH"
  
RUN apt update && apt install -y --no-install-recommends \
    build-essential \
    supervisor \
    socat \
    git \
    libreadline-dev \
    ca-certificates 

# Compile the cpc daemon 
RUN set -x \
&& apt install -y --no-install-recommends \
   cmake \
    libmbedtls-dev \
    && git clone https://github.com/SiliconLabs/cpc-daemon.git \
    && cd cpc-daemon \
    && mkdir build \
    && cd build \
    && cmake ../ \
    -DENABLE_ENCRYPTION=FALSE \
    && make \
    && make install 

# Copy the cpcd binaries to the correct location, including libcpc.so
RUN cp -r /usr/local/* /usr/
# Copy the zigbeed source code for the current arch
COPY zigbeed/$TARGETARCH /zigbeed

RUN \
    set -x \
    && apt install -y --no-install-recommends \
    libmbedtls-dev \
    && cd /zigbeed \
    && make -f zigbeed.Makefile \
    && cp /zigbeed/build/debug/zigbeed /usr/local/bin

COPY supervisord.conf /etc/supervisor/conf.d/supervisord.conf

COPY rootfs /

# Deploy zigbee2mqtt from source
RUN apt install -y --no-install-recommends \
    libsystemd-dev \
    npm \
    nodejs \
    linux-headers-generic

RUN node --version
RUN npm --version

RUN mkdir /opt/zigbee2mqtt \
    && chown -R ${USER}: /opt/zigbee2mqtt \
    && git clone --depth 1 -b dev https://github.com/Koenkk/zigbee2mqtt.git /opt/zigbee2mqtt \
    && cd /opt/zigbee2mqtt \
    && npm ci --no-audit --no-optional --no-update-notifier \
    && npm rebuild --build-from-source
  #  && npm run build 

COPY configuration.yaml /opt/zigbee2mqtt/data/configuration.yaml

RUN sed -i 's|mqtt://localhost|mqtt://192.168.1.3|g' /opt/zigbee2mqtt/data/configuration.yaml 

# Update the configs
COPY update_configs.sh /update_configs.sh
RUN chmod +x /update_configs.sh
ENTRYPOINT ["/update_configs.sh"]

CMD ["supervisord", "-c", "/etc/supervisor/conf.d/supervisord.conf"]