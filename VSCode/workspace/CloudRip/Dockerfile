# CloudRip Game Container
# Supports X11 forwarding and ffmpeg streaming

FROM ubuntu:22.04

# Install dependencies
RUN apt-get update && apt-get install -y \
    xvfb \
    x11-apps \
    x11-xserver-utils \
    x11-utils \
    xdotool \
    ffmpeg \
    imagemagick \
    netcat \
    libgl1-mesa-glx \
    libxrender1 \
    libxext6 \
    fonts-liberation \
    wget \
    curl \
    ca-certificates \
    chocolate-doom \
    freedoom \
    git \
    python3 \
    python3-pip \
    unzip \
    tar \
    bzip2 \
    p7zip-full \
    && rm -rf /var/lib/apt/lists/*

# Create app directory
WORKDIR /app

# Create startup script that manages Xvfb + game + ffmpeg 
# The script will be dynamically created with proper display number
# For now, just ensure DISPLAY is set internally
ENV DISPLAY=:0
ENV XAUTHORITY=/tmp/.Xauthority

# Default command - will be overridden by launch_command from database
# The CMD should start Xvfb, game, and handle ffmpeg streaming internally
CMD ["/bin/bash"]
