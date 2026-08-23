# SuperCollider, headless, for NRT reference renders of sc808.
# ubuntu:22.04 ships SuperCollider 3.11 — sclang to compile the SynthDefs,
# scsynth to render them offline. No jack, no Qt IDE: NRT writes a file.
FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        supercollider-server supercollider-language supercollider-common \
        libsndfile1 ca-certificates python3 python3-numpy \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
