# ldm service

A Local Dynamic Map (LDM) implementation tailored for CPS V2 Clustering deployments. This service set follows ETSI EN 302 895 / TR 102 863 semantics and reuses the middleware and configuration patterns from the CPS project.

## Repository layout

- `ldm-core/` – In-memory ldm store, AoM maintenance, metrics exposer.
- `ldm-ingestion/` – MAPEM and future message ingestion into the ldm.
- `ldm-query/` – Query and subscription front-end for applications.
- `common/` – Shared utilities (locator, geometry, metrics, config, LDM model).
- `agent/` – Compose templates for deployment.
- `metrics/` – Prometheus configuration and examples.

## Build

```bash
cmake -S . -B build
cmake --build build
```

Each service has an individual target (`ldm-core`, `ldm-ingestion`, `ldm-query`).

### Container builds (mirrors cps-v2 images)

The Dockerfiles now follow the same multi-stage layout seen in `cps-v2` (middleware-lib stages + runtime). Building a single service image locally:

```bash
docker build -t ldm-core:local -f ldm-core/Dockerfile .
```

The compose file references registry images under `code.nap.av.it.pt:5050/mobility-networks/ldm/*` just like `cps-v2`, but `docker compose build` will override them during development.

## Run with Docker Compose

```bash
docker compose up --build
```

## Configuration

Each service loads `/config.ini`. Example files live next to each executable and follow the same structure adopted by CPS-V2: `[general]`, `[messaging]`, `[locator]` and a service-specific block such as `[ldm-core]`, `[ingestion]` or `[query]`.

### Messaging backends (Fast DDS, MQTT, Zenoh)

Configuration has been aligned with the CPS-V2 stack so that every service shares a common `messaging` section:

```ini
[messaging]
backend = fastdds    ; fastdds | mqtt | zenoh
domain_id = 0        ; DDS domain id (used when backend = fastdds)
mqtt_host = 127.0.0.1
mqtt_port = 1883
zenoh_endpoint = tcp/127.0.0.1:7447
```

The new `common/messaging` subsystem exposes a transport interface that emulates Fast DDS, MQTT or Zenoh semantics (mirroring CPS-V2) so that ingestion, core and query components can publish/subscribe on shared topics (`generation/mapem`, `ldm/updates`, `ldm/query`, `ldm/response`). Swapping the backend only requires editing the config files, matching the approach taken in the CPS-V2 generation/processing services.

The ingestion service now republishes incoming MAPEM traffic on the configured LDM topic, the core tracks updates and responds to queries, and the query service continuously exercises the Fast DDS/MQTT/Zenoh interface by issuing sample requests. This mirrors the CPS-V2 workflow while keeping the implementation self-contained.
