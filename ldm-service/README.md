# LDM Service

A Local Dynamic Map (LDM) implementation tailored for CPS V2 Clustering deployments. This service set follows ETSI EN 302 895 / TR 102 863 semantics and reuses the middleware and configuration patterns from the CPS project.

## Repository layout

- `ldm-core/` – In-memory LDM store, AoM maintenance, metrics exposer.
- `ldm-ingestion/` – MAPEM and future message ingestion into the LDM.
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

## Run with Docker Compose

```bash
docker compose up --build
```

## Configuration

Each service loads `/config.ini`. Example files live next to each executable.
