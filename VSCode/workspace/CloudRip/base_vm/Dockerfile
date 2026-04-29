FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive \
    WINEPREFIX=/root/.wine \
    WINEARCH=win64 \
    DISPLAY=:99

# 32-bit Support für Wine
RUN dpkg --add-architecture i386

RUN apt update && \
    apt install -y --no-install-recommends \
    wine \
    wine32:i386 \
    winetricks \
    xvfb \
    mesa-utils \
    libgl1-mesa-dri \
    libgl1-mesa-dri:i386 \
    curl \
    ffmpeg \
    ca-certificates && \
    apt clean && \
    rm -rf /var/lib/apt/lists/*

# Wine prefix initialisieren
RUN wineboot --init || true

RUN mkdir -p /workspace && \
    echo "CloudRip Container" > /workspace/README.txt

WORKDIR /workspace

# Startscript
COPY start.sh /start.sh
RUN chmod +x /start.sh

CMD ["/start.sh"]
