FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Base image for building and running the CMake-based C++ server in Docker.
RUN apt-get update -o Acquire::Retries=5 && \
    apt-get install -y --no-install-recommends \
      build-essential \
      cmake \
      pkg-config \
      git \
      curl \
      wget \
      vim \
      tree \
      gdb \
      less \
      man-db \
      manpages \
      manpages-dev \
      nlohmann-json3-dev \
      libmysqlclient-dev \
      libhiredis-dev \
      libssl-dev \
    && yes | unminimize \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

CMD ["bash"]
