#!/bin/bash
# Docker development helper script for embedded-ruby-vm

set -e

CMD=${1:-help}

case "$CMD" in
    setup)
        echo "🚀 Setting up Docker development environment..."
        docker-compose build
        docker-compose run --rm source-sync-in
        docker-compose up -d dev
        echo "✅ Setup complete! Run '$0 shell' to enter the container."
        ;;

    build-image)
        echo "🔨 Building Docker image..."
        docker-compose build
        ;;

    sync)
        echo "📦 Syncing source code to container..."
        docker-compose run --rm source-sync-in
        echo "✅ Source code synced!"
        ;;

    start)
        echo "▶️  Starting development container..."
        docker-compose up -d dev
        echo "✅ Container started! Run '$0 shell' to enter."
        ;;

    stop)
        echo "⏹️  Stopping development container..."
        docker-compose down
        echo "✅ Container stopped!"
        ;;

    shell)
        echo "🐚 Entering development container..."
        docker-compose exec dev bash
        ;;

    build)
        TARGET_ARCH=${2:-}
        if [ -z "$TARGET_ARCH" ]; then
            echo "🔨 Building project (auto-detect architecture)..."
            docker-compose exec dev ./gradlew build -PbuildType=Debug -PbuildWrapperShared=true
        else
            echo "🔨 Building project for $TARGET_ARCH..."
            docker-compose exec dev ./gradlew build -PbuildType=Debug -PbuildWrapperShared=true -PtargetArch="$TARGET_ARCH"
        fi
        ;;

    release)
        TARGET_ARCH=${2:-}
        if [ -z "$TARGET_ARCH" ]; then
            echo "🔨 Building project (auto-detect architecture)..."
            docker-compose exec dev ./gradlew build -PbuildType=Release
        else
            echo "🔨 Building project for $TARGET_ARCH..."
            docker-compose exec dev ./gradlew build -PbuildType=Release -PtargetArch="$TARGET_ARCH"
        fi
        ;;

    desktop-jar)
        echo "📦 Building desktop JAR... (no cache)"
        docker-compose exec dev ./gradlew :ruby-vm-kmp:clean :ruby-vm-kmp:desktopJar -PbuildType=Debug --no-build-cache
        ;;

    test)
        echo "🧪 Running tests..."
        docker-compose exec dev bash -c "cd build && ./bin/test_core && ./bin/test_jni"
        ;;

    export)
        echo "📤 Exporting build artifacts..."
        docker-compose run --rm artifact-export
        echo "✅ Artifacts available in ./docker-output/"
        ;;

    clean)
        echo "🧹 Cleaning build artifacts..."
        docker-compose exec dev bash -c " rm -f CMakeCache.txt && rm -rf CMakeFiles && ./gradlew clean && rm -rf build kmp/build"
        docker volume rm embedded-ruby-vm_build-artifacts embedded-ruby-vm_kmp-artifacts || true
        echo "✅ Build artifacts cleaned!"
        ;;

    clean-all)
        echo "🧹 Cleaning all volumes and containers..."
        docker-compose down -v
        echo "✅ All volumes removed! Run '$0 setup' to start fresh."
        ;;

    logs)
        docker-compose logs -f dev
        ;;

    android-setup)
        echo "🤖 Setting up Android development environment..."
        docker-compose -f docker-compose.yml -f docker-compose.override.yml build dev-android
        docker-compose run --rm source-sync-in
        docker-compose -f docker-compose.yml -f docker-compose.override.yml up -d dev-android
        echo "✅ Android setup complete!"
        ;;

    android-shell)
        echo "🐚 Entering Android development container..."
        docker-compose exec dev-android bash
        ;;

    help|*)
        cat << 'EOF'
🐳 Docker Development Helper for embedded-ruby-vm

Usage: ./docker-dev.sh <command> [arguments]

Setup Commands:
  setup             - Initial setup: build image, sync code, start container
  build-image       - Build Docker image only
  sync              - Sync source code from host to container
  start             - Start the development container
  stop              - Stop the development container

Development Commands:
  shell             - Enter the development container shell
  build [arch]      - Build the project (optional: x86_64, arm64, all)
  desktop-jar       - Build desktop JAR
  test              - Run all tests
  export            - Export build artifacts to ./docker-output/

Maintenance Commands:
  clean             - Clean build artifacts (keeps cache)
  clean-all         - Remove all volumes and containers (fresh start)
  logs              - View container logs

Android Commands:
  android-setup     - Setup Android development environment
  android-shell     - Enter Android development container

Examples:
  ./docker-dev.sh setup              # Initial setup
  ./docker-dev.sh sync               # Sync code changes
  ./docker-dev.sh shell              # Enter container
  ./docker-dev.sh build x86_64       # Build for x86_64
  ./docker-dev.sh export             # Export artifacts

Quick Workflow:
  1. ./docker-dev.sh setup           # One-time setup
  2. Make changes in your IDE
  3. ./docker-dev.sh sync            # Sync changes
  4. ./docker-dev.sh build           # Build in container
  5. ./docker-dev.sh export          # Get artifacts

EOF
        ;;
esac
