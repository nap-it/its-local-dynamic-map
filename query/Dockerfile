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
    libboost-date-time-dev \
    libboost-program-options-dev \
    libboost-system-dev \
    libcrypto++-dev \
    libasio-dev \
    libtinyxml2-dev \
    libcurl4-openssl-dev \
    libssl-dev \
    clang \
    git \
    ca-certificates \
    curl \
    pkg-config \
    zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

# ------------------------- Install Prometheus C++ Client -------------------------
WORKDIR /usr/src
RUN git clone https://github.com/jupp0r/prometheus-cpp.git
WORKDIR /usr/src/prometheus-cpp
RUN git submodule update --init --recursive
RUN mkdir build && cd build
WORKDIR /usr/src/prometheus-cpp/build
RUN cmake .. -DBUILD_SHARED_LIBS=ON  -DENABLE_PULL=ON -DENABLE_CIVETWEB=ON -DENABLE_PUSH=OFF -DENABLE_TESTING=OFF
RUN cmake --build . --parallel $(nproc)
RUN cmake --build . --target install
RUN ldconfig

# ------------------------- Build Workspace -------------------------
WORKDIR /workspace
COPY . /workspace
RUN git -c http.sslVerify=false clone --depth 1 https://code.nap.av.it.pt/external-tools/spdlog.git common/spdlog && \
    git -c http.sslVerify=false clone --depth 1 https://code.nap.av.it.pt/external-tools/rapidjson.git common/rapidjson && \
    git -c http.sslVerify=false clone --depth 1 https://github.com/nap-it/paho.mqtt.cpp.wrapper.git paho-mqtt-cpp-wrapper && \
    git -c http.sslVerify=false clone --depth 1 https://github.com/nap-it/fastdds-cpp-wrapper.git fastdds-cpp-wrapper
RUN rm -rf build && cmake -S . -B build && cmake --build build --target query --parallel $(nproc)


# ==============================
# RUNTIME STAGE
# ==============================
FROM code.nap.av.it.pt:5050/external-tools/misc-docker-images/debian:bullseye-slim AS runtime

ENV TZ=Europe/Lisbon
RUN ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone

RUN apt-get update && apt-get install -y --no-install-recommends \
    libboost-date-time1.74.0 \
    libboost-program-options1.74.0 \
    libboost-system1.74.0 \
    libcrypto++-dev \
    libasio-dev \
    libtinyxml2-8 \
    libcurl4-openssl-dev \
    libssl-dev \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /usr/local/lib/libpaho-mqtt3*.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/libpaho-mqttpp3.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/libfastcdr.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/libfastrtps.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/libfoonathan_memory* /usr/local/lib/
COPY --from=builder /usr/local/lib/libspdlog.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/libzenoh* /usr/local/lib/
COPY --from=builder /usr/local/lib/libprometheus-cpp*.so* /usr/local/lib/

RUN ldconfig

WORKDIR /ldm

COPY --from=builder /workspace/build/query/query /ldm/query
COPY --from=builder /workspace/zenoh_config.json5 /ldm/zenoh_config.json5

ENTRYPOINT ["/ldm/query"]
