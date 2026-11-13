# Linux build and test environment for QLC+ 5 and the Director checks.
# See run-tests.sh for how to use it.
FROM ubuntu:25.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build pkg-config git ca-certificates \
    qt6-base-dev qt6-base-private-dev qt6-declarative-dev qt6-declarative-private-dev \
    qt6-multimedia-dev qt6-svg-dev qt6-3d-dev qt6-websockets-dev qt6-serialport-dev \
    qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools libqt6opengl6-dev \
    libasound2-dev libusb-1.0-0-dev libftdi1-dev libudev-dev libmad0-dev libsndfile1-dev \
    liblo-dev libfftw3-dev libgl1-mesa-dev libpulse-dev libxkbcommon-dev libvulkan-dev \
    'qml6-module-*' libgl1-mesa-dri fonts-dejavu-core \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /build
