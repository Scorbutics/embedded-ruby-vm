# Multi-stage Dockerfile for embedded-ruby-vm development
# Using Alpine Linux for musl libc compatibility with static Ruby builds
FROM eclipse-temurin:17-jdk-alpine AS builder

# Build argument to control custom certificate installation
ARG INSTALL_CUSTOM_CERTS=false

# Fix Alpine package repository access issues
# Use HTTP mirrors as a workaround for SSL certificate issues
RUN echo "http://dl-cdn.alpinelinux.org/alpine/v3.22/main" > /etc/apk/repositories && \
    echo "http://dl-cdn.alpinelinux.org/alpine/v3.22/community" >> /etc/apk/repositories

# Optional: Install custom CA certificates for corporate environments
# This runs conditionally based on the build argument
RUN if [ "$INSTALL_CUSTOM_CERTS" = "true" ]; then \
        apk add --no-cache openssl ca-certificates && \
        mkdir -p /tmp/certs; \
    fi

# Copy certificates from local docker-certs/ directory
# This directory exists as a placeholder - populate it with your certs if needed
COPY docker-certs /tmp/certs-source/

# Install certificates if custom certs were provided
RUN if [ "$INSTALL_CUSTOM_CERTS" = "true" ] && [ "$(ls -A /tmp/certs-source 2>/dev/null | grep -v README)" ]; then \
        echo "Installing custom CA certificates..." && \
        openssl version && \
        chmod +x /tmp/certs-source/*.sh 2>/dev/null || true && \
        cd /tmp/certs-source && \
        ./install_certs.sh && \
        mv splitted/*.crt /usr/local/share/ca-certificates/ 2>/dev/null || true && \
        update-ca-certificates && \
        echo "Custom certificates installed successfully"; \
    else \
        echo "Skipping custom certificate installation (no certificates found or not enabled)"; \
    fi

# Clean up temporary cert files
RUN rm -rf /tmp/certs-source

# Install build tools and dependencies for Alpine/musl
# Install CMake, build tools, and Ruby dependencies
RUN apk add --no-cache \
    cmake \
    make \
    g++ \
    git \
    bash \
    gmp-dev \
    zlib-dev

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
