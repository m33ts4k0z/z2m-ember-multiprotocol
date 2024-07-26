FROM debian:bookworm AS cross-builder-base

ARG TARGETPLATFORM
ARG BUILDPLATFORM
ARG TARGETARCH
ARG CPCD_VERSION="v4.5.1"
ARG DEBIAN_CROSS_PREFIX
ARG DEBIAN_ARCH

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
&& apt-get install -y --no-install-recommends \
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

RUN cp -r /usr/local/* /usr/
# Copy the zigbeed source code for the current arch
COPY zigbeed/$TARGETARCH /zigbeed

RUN \
    set -x \
    && apt-get install -y --no-install-recommends \
    libmbedtls-dev \
    && cd /zigbeed \
    && make -f zigbeed.Makefile \
    && cp /zigbeed/build/debug/zigbeed /usr/local/bin

COPY supervisord.conf /etc/supervisor/conf.d/supervisord.conf

COPY rootfs /

CMD ["supervisord", "-c", "/etc/supervisor/conf.d/supervisord.conf"]