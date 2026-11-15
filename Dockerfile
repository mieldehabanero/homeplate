FROM python:3.13-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential ca-certificates git \
    && rm -rf /var/lib/apt/lists/*
RUN pip install --no-cache-dir platformio==6.1.19

WORKDIR /workspace
CMD ["pio", "test", "-e", "native", "-e", "native_png", "-e", "native_png_sanitize"]
