#!/bin/bash
# Prepara Ubuntu 24.04 (WSL) para compilar el motor para la PC y para la
# Raspberry Pi (aarch64). Se corre como root:
#   wsl -d Ubuntu -u root -- bash scripts/setup-wsl.sh
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive

sources=/etc/apt/sources.list.d/ubuntu.sources
if ! grep -q '^Architectures:' "$sources"; then
    sed -i '/^Types: deb/a Architectures: amd64' "$sources"
fi

cat > /etc/apt/sources.list.d/ubuntu-ports-arm64.sources <<'EOF'
Types: deb
URIs: http://ports.ubuntu.com/ubuntu-ports
Suites: noble noble-updates noble-security
Components: main universe
Architectures: arm64
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg
EOF

dpkg --add-architecture arm64
apt-get update -qq
apt-get install -y -qq \
    build-essential cmake pkg-config git zlib1g-dev libsdl2-dev \
    gcc-aarch64-linux-gnu libsdl2-dev:arm64 \
    g++-aarch64-linux-gnu mingw-w64 # el importador para la Pi y para Windows (scripts/build-importador.sh)

aarch64-linux-gnu-gcc --version | head -1
ls /usr/lib/aarch64-linux-gnu/cmake/SDL2/
