# Multi-stage Dockerfile for embedded-ruby-vm development
# Using Debian Linux with glibc for better compatibility with Kotlin/Native

# Build argument to control custom certificate installation.
# Declared globally so it can drive stage selection in the FROM below.
ARG INSTALL_CUSTOM_CERTS=false

FROM eclipse-temurin:17-jdk-jammy AS base

# Stage used when INSTALL_CUSTOM_CERTS=false: no-op, no docker-certs/ needed.
FROM base AS certs-false

# Stage used when INSTALL_CUSTOM_CERTS=true: copy and install certs from docker-certs/.
FROM base AS certs-true
RUN apt-get update && \
    apt-get install -y --no-install-recommends openssl ca-certificates && \
    rm -rf /var/lib/apt/lists/*
COPY docker-certs /tmp/certs-source/
RUN if [ "$(ls -A /tmp/certs-source 2>/dev/null | grep -v README)" ]; then \
        echo "Installing custom CA certificates..." && \
        openssl version && \
        chmod +x /tmp/certs-source/*.sh 2>/dev/null || true && \
        cd /tmp/certs-source && \
        ./install_certs.sh && \
        mv splitted/*.crt /usr/local/share/ca-certificates/ 2>/dev/null || true && \
        update-ca-certificates && \
        echo "Custom certificates installed successfully"; \
    else \
        echo "Skipping custom certificate installation (no certificates found)"; \
    fi && \
    rm -rf /tmp/certs-source

# Builder starts from whichever cert stage matches the build arg.
# BuildKit only builds the stage actually selected, so the unused branch's
# COPY (which would fail without a local docker-certs/ dir) is never executed.
FROM certs-${INSTALL_CUSTOM_CERTS} AS builder

# Install build tools and dependencies for Debian/glibc
# Install CMake, build tools, and Ruby dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake \
    make \
    g++ \
    git \
    bash \
    curl \
    unzip \
    libgmp-dev \
    zlib1g-dev \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

# Verify tools are available
RUN cmake --version && gcc --version

# Set working directory
WORKDIR /workspace

# Create gradle cache directory
RUN mkdir -p /root/.gradle

# Default command: bash shell for interactive development
CMD ["/bin/bash"]

# Development stage - includes all tools for building
FROM builder AS dev

# Set environment variables for Gradle
ENV GRADLE_OPTS="-Dorg.gradle.daemon=false -Dorg.gradle.parallel=true -Dorg.gradle.caching=true"
ENV GRADLE_USER_HOME=/gradle-cache

# Expose common ports (if needed for future services)
EXPOSE 8080

# Default working directory
WORKDIR /workspace

# Keep container running for interactive use
CMD ["tail", "-f", "/dev/null"]
