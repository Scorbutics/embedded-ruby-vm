# Multi-stage Dockerfile for embedded-ruby-vm development
FROM eclipse-temurin:17-jdk AS builder

# Build argument to control custom certificate installation
ARG INSTALL_CUSTOM_CERTS=false

# Optional: Install custom CA certificates for corporate environments
# This runs conditionally based on the build argument
RUN if [ "$INSTALL_CUSTOM_CERTS" = "true" ]; then \
        apt-get update && apt-get install -y openssl ca-certificates && \
        mkdir -p /tmp/certs; \
    fi

# Copy certificates if they exist (requires 'certs' build context)
# This will be skipped if certs context is not provided
COPY --from=certs . /tmp/certs/ 2>/dev/null || true

# Install certificates if custom certs were provided
RUN if [ "$INSTALL_CUSTOM_CERTS" = "true" ] && [ -d /tmp/certs ] && [ "$(ls -A /tmp/certs)" ]; then \
        echo "Installing custom CA certificates..." && \
        openssl version && \
        chmod +x /tmp/certs/*.sh 2>/dev/null || true && \
        cd /tmp/certs && \
        ./install_certs.sh && \
        mv splitted/*.crt /usr/local/share/ca-certificates/ && \
        update-ca-certificates && \
        echo "Custom certificates installed successfully"; \
    else \
        echo "Skipping custom certificate installation"; \
    fi

# Install CMake and build essentials
RUN apt-get update && apt-get install -y \
    cmake \
    build-essential \
    git \
    curl \
    wget \
    unzip \
    libz-dev \
    && rm -rf /var/lib/apt/lists/*

# Verify CMake version
RUN cmake --version

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
