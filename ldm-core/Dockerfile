# ==============================
# BUILDER STAGE
# ==============================
FROM code.nap.av.it.pt:5050/mobility-networks/middleware-images/zenoh-libs:1.4.0 AS zenoh-libs
FROM code.nap.av.it.pt:5050/mobility-networks/middleware-images/dds-libs:2.14.4 AS dds-libs
FROM code.nap.av.it.pt:5050/mobility-networks/middleware-images/mqtt-libs:1.3.2 AS mqtt-libs

FROM code.nap.av.it.pt:5050/external-tools/misc-docker-images/debian:bullseye-slim AS builder

COPY --from=zenoh-libs /usr/local /usr/local
COPY --from=dds-libs /usr/local /usr/local
COPY --from=mqtt-libs /usr/local /usr/local

ENV TZ=Europe/Lisbon
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone

RUN printf "deb http://archive.debian.org/debian-archive/debian bullseye-backports main non-free\ndeb-src http://archive.debian.org/debian-archive/debian bullseye-backports main non-free\n" > /etc/apt/sources.list.d/backports.list

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake/bullseye-backports \
    cmake-data/bullseye-backports \
    git \
    ca-certificates \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
COPY . /workspace
RUN cmake -S . -B build && cmake --build build --target ldm-core --parallel $(nproc)

# ==============================
# RUNTIME STAGE
# ==============================
FROM code.nap.av.it.pt:5050/external-tools/misc-docker-images/debian:bullseye-slim AS runtime

ENV TZ=Europe/Lisbon
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone

RUN apt-get update && apt-get install -y --no-install-recommends \
    libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /usr/local/lib/libpaho-mqtt3*.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/libpaho-mqttpp3.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/libfastcdr.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/libfastrtps.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/libzenoh* /usr/local/lib/
RUN ldconfig

WORKDIR /ldm
COPY --from=builder /workspace/build/ldm-core/ldm-core /ldm/core
ENTRYPOINT ["/ldm/core"]
