# Docker Development Environment

This Docker setup provides a complete development environment for embedded-ruby-vm without installing tools on your host machine. It uses named volumes for better performance on Windows (avoiding slow bind mounts).

## Prerequisites

- Docker Desktop installed
- Docker Compose installed

## Quick Start

### 1. Build the Development Container

```bash
docker-compose build
```

### 2. Sync Source Code to Volume

Copy your source code into the named volume:

```bash
docker-compose run --rm source-sync-in
```

### 3. Start Development Container

```bash
docker-compose up -d dev
```

### 4. Enter the Container

```bash
docker-compose exec dev bash
```

### 5. Build the Project

Inside the container:

```bash
# Build for your architecture (detected automatically)
./gradlew build

# Build for specific architecture
./gradlew build -PtargetArch=x86_64
./gradlew build -PtargetArch=arm64

# Build desktop JAR
./gradlew :ruby-vm-kmp:desktopJar

# Run tests
cd build
./bin/test_core
./bin/test_jni
```

### 6. Export Build Artifacts

After building, export artifacts to your host machine:

```bash
docker-compose run --rm artifact-export
```

Artifacts will be available in `./docker-output/` on your host machine.

## Corporate/Enterprise Environments

If you're working behind a corporate proxy or need custom CA certificates, follow these steps **before** building the Docker image:

### Setup Custom Certificates

1. **Prepare your certificates directory**

   Create a directory with your certificates and installation script:
   ```bash
   mkdir -p docker-certs
   # Copy your certificates and install_certs.sh to docker-certs/
   ```

   Your `docker-certs/` directory should contain:
   - `install_certs.sh` - Script to process and install certificates
   - Your certificate files (`.crt`, `.pem`, etc.)

2. **Create local override configuration**

   Copy the example override file:
   ```bash
   cp docker-compose.override.yml.example docker-compose.override.yml
   ```

3. **Edit `docker-compose.override.yml`**

   Uncomment and configure the certificate section:
   ```yaml
   version: '3.8'

   services:
     dev:
       build:
         additional_contexts:
           certs: ./docker-certs  # or /absolute/path/to/certs
         args:
           - INSTALL_CUSTOM_CERTS=true
   ```

4. **Build and run**

   The build will automatically detect and install your certificates:
   ```bash
   docker-compose build
   docker-compose run --rm source-sync-in
   docker-compose up -d dev
   ```

### Proxy Configuration

If you're behind a corporate proxy, add proxy settings to your `docker-compose.override.yml`:

```yaml
services:
  dev:
    environment:
      - HTTP_PROXY=http://proxy.company.com:8080
      - HTTPS_PROXY=http://proxy.company.com:8080
      - NO_PROXY=localhost,127.0.0.1
```

### For Android Builds with Custom Certs

If you need Android NDK support with custom certificates, configure `dev-android` in your override file:

```yaml
services:
  dev-android:
    build:
      additional_contexts:
        certs: ./docker-certs
      args:
        - INSTALL_CUSTOM_CERTS=true
```

### Important Notes

- `docker-compose.override.yml` is **gitignored** and won't be committed
- The base `docker-compose.yml` works without certificates for external contributors
- Certificates are installed at build time, not runtime
- If you update certificates, rebuild the image: `docker-compose build --no-cache`

## Workflow

### Initial Setup (One Time)

```bash
# Build the Docker image
docker-compose build

# Sync source code to volume
docker-compose run --rm source-sync-in

# Start the container
docker-compose up -d dev
```

### Daily Development

```bash
# Make changes on your host machine using your favorite IDE
# ...

# Sync changes to container
docker-compose run --rm source-sync-in

# Enter container and build
docker-compose exec dev bash
./gradlew build

# Export artifacts when done
docker-compose run --rm artifact-export
```

### Re-sync Source Code

If you make changes on your host:

```bash
docker-compose run --rm source-sync-in
```

## Volume Management

### List Volumes

```bash
docker volume ls | grep embedded-ruby
```

### Clean Build Cache

```bash
# Remove build artifacts (keeps source and Gradle cache)
docker volume rm embedded-ruby-vm_build-artifacts
docker volume rm embedded-ruby-vm_kmp-artifacts
```

### Clean Gradle Cache

```bash
# Remove Gradle cache (forces re-download of dependencies)
docker volume rm embedded-ruby-vm_gradle-cache
```

### Start Fresh

```bash
# Stop containers
docker-compose down

# Remove all volumes
docker-compose down -v

# Rebuild
docker-compose build
docker-compose run --rm source-sync-in
docker-compose up -d dev
```

## Android Builds (Optional)

For Android builds with NDK support:

### 1. Build Android Image

```bash
docker-compose -f docker-compose.yml -f docker-compose.override.yml build dev-android
```

### 2. Sync Source Code

```bash
docker-compose run --rm source-sync-in
```

### 3. Start Android Dev Container

```bash
docker-compose -f docker-compose.yml -f docker-compose.override.yml up -d dev-android
```

### 4. Build Android Artifacts

```bash
docker-compose exec dev-android bash

# Inside container:
./gradlew :ruby-vm-kmp:assembleDebug -PtargetArch=arm64
./gradlew :ruby-vm-kmp:assembleRelease -PtargetArch=all
```

## Helpful Commands

### View Container Logs

```bash
docker-compose logs -f dev
```

### Stop Container

```bash
docker-compose down
```

### Restart Container

```bash
docker-compose restart dev
```

### Execute One-Off Commands

```bash
# Run a Gradle task without entering the container
docker-compose exec dev ./gradlew tasks

# Check CMake version
docker-compose exec dev cmake --version

# Check Java version
docker-compose exec dev java -version
```

## Troubleshooting

### Out of Space

```bash
# Clean up Docker resources
docker system prune -a --volumes
```

### Gradle Daemon Issues

```bash
# Stop Gradle daemon inside container
docker-compose exec dev ./gradlew --stop
```

### CMake Cache Issues

```bash
# Clean build directory
docker-compose exec dev rm -rf build kmp/build
```

### Slow Build on Windows

- Named volumes provide much better performance than bind mounts on Windows
- First build will be slow (downloading dependencies)
- Subsequent builds are faster due to Gradle cache volume
- Consider increasing Docker Desktop memory allocation (Settings → Resources)

## Architecture Notes

- **source-code**: Named volume containing your project source code
- **gradle-cache**: Named volume for Gradle dependencies and build cache
- **build-artifacts**: Named volume for root build outputs
- **kmp-artifacts**: Named volume for KMP module build outputs

The `source-sync-in` service copies from your host to the volume.
The `artifact-export` service copies from volumes to `./docker-output/` on your host.

This approach gives you:
- ✅ Fast builds (no bind mount overhead on Windows)
- ✅ Isolated environment (no local tool installation)
- ✅ Persistent caches (Gradle cache survives container restarts)
- ✅ Easy artifact extraction (dedicated export service)
