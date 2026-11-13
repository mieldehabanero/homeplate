FROM python:3.13-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential ca-certificates curl git unzip \
    && rm -rf /var/lib/apt/lists/* \
    && pip install --no-cache-dir platformio==6.1.19

CMD ["pio", "test", "-e", "native"]
