FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV VCPKG_ROOT=/opt/vcpkg
ENV CMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake

# Base image for building and running the CMake-based C++ server in Docker.
RUN apt-get update -o Acquire::Retries=5 && \
    apt-get install -y --no-install-recommends \
      build-essential \
      cmake \
      pkg-config \
      python3 \
      git \
      ca-certificates \
      curl \
      wget \
      zip \
      unzip \
      tar \
      vim \
      tree \
      gdb \
      less \
      man-db \
      manpages \
      manpages-dev \
	      nlohmann-json3-dev \
	      libpq-dev \
	      libcurl4-openssl-dev \
	      libssl-dev \
	      libgtest-dev \
    && yes | unminimize \
    && rm -rf /var/lib/apt/lists/*

RUN git clone https://github.com/microsoft/vcpkg.git ${VCPKG_ROOT} \
    && ${VCPKG_ROOT}/bootstrap-vcpkg.sh \
    && ${VCPKG_ROOT}/vcpkg install minio-cpp jwt-cpp

WORKDIR /workspace

COPY . .

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --target webserver --parallel 2

CMD ["./build/bin/webserver", "8080"]
