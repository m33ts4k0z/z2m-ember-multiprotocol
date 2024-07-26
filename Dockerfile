FROM --platform=$BUILDPLATFORM debian:bookworm AS cross-builder-base

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

# Add ARM64 architecture and update package lists
FROM --platform=${BUILDPLATFORM} cross-builder-base AS cross-builder

COPY debian-${TARGETARCH}.cmake /usr/src/debian.cmake

RUN set -x \
    && if [ "$TARGETARCH" = "armv7" ]; then \
        DEBIAN_ARCH=armhf; \
        CROSS_PACKAGE="crossbuild-essential-armhf"; \
    elif [ "$TARGETARCH" = "arm64" ]; then \
        DEBIAN_ARCH=arm64; \
        CROSS_PACKAGE="crossbuild-essential-arm64"; \
    elif [ "$TARGETARCH" = "amd64" ]; then \
        DEBIAN_ARCH=amd64; \
        CROSS_PACKAGE="build-essential"; \
    else \
        echo "Unsupported architecture: $TARGETARCH"; \
        exit 1; \
    fi \
    && dpkg --add-architecture $DEBIAN_ARCH \
    && apt-get update \
    && apt-get install -y --no-install-recommends $CROSS_PACKAGE

COPY debian-${TARGETARCH}.cmake /usr/src/debian.cmake

FROM --platform=${BUILDPLATFORM} cross-builder AS cpcd-builder
RUN set -x \
&& apt-get install -y --no-install-recommends \
   cmake \
    libmbedtls-dev:$DEBIAN_ARCH \
    && git clone https://github.com/SiliconLabs/cpc-daemon.git \
    && cd cpc-daemon \
    && mkdir build \
    && cd build \
    && cmake ../ \
   # -DCMAKE_TOOLCHAIN_FILE=../debian.cmake \
    -DENABLE_ENCRYPTION=FALSE \
    && make \
    && make install 

FROM --platform=${BUILDPLATFORM} cross-builder AS zigbeed-builder
COPY zigbeed/$TARGETARCH /zigbeed

RUN \
    set -x \
    && apt-get install -y --no-install-recommends \
    libmbedtls-dev:$DEBIAN_ARCH

RUN if [ "$TARGETARCH" = "armv7" ]; then \
        DEBIAN_CROSS_PREFIX=arm-linux-gnueabihf; \
    elif [ "$TARGETARCH" = "amd64" ]; then \
        DEBIAN_CROSS_PREFIX=""; \
    else \
        DEBIAN_CROSS_PREFIX=aarch64-linux-gnu; \
    fi 

COPY --from=cpcd-builder /usr/local/ /usr/${DEBIAN_CROSS_PREFIX}/

RUN cd /zigbeed && make -f zigbeed.Makefile \
    AR="$DEBIAN_CROSS_PREFIX-ar" \
    CC="$DEBIAN_CROSS_PREFIX-gcc" \
    LD="$DEBIAN_CROSS_PREFIX-gcc" \
    CXX="$DEBIAN_CROSS_PREFIX-g++" 

#COPY --from=zigbeed-builder /zigbeed/build/debug/zigbeed /usr/local/bin

COPY supervisord.conf /etc/supervisor/conf.d/supervisord.conf

COPY rootfs /

CMD ["supervisord", "-c", "/etc/supervisor/conf.d/supervisord.conf"]