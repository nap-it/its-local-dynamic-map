FROM code.nap.av.it.pt:5050/external-tools/misc-docker-images/debian:bullseye-slim AS build
WORKDIR /src
COPY . /src
RUN apt-get update && apt-get install -y cmake g++ make pkg-config && rm -rf /var/lib/apt/lists/*
RUN cmake -S . -B build && cmake --build build --target ldm-core

FROM code.nap.av.it.pt:5050/external-tools/misc-docker-images/debian:bullseye-slim AS runtime
WORKDIR /ldm
COPY --from=build /src/build/ldm-core/ldm-core /ldm/ldm-core
ENTRYPOINT ["/ldm/ldm-core"]
