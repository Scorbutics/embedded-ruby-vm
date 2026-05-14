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

    example)
        EXAMPLE_NAME=${2:-}
        if [ -z "$EXAMPLE_NAME" ]; then
            cat << EOF
📚 Available Kotlin/JVM examples:
  - ImprovedApiExample      (default; batch execution, builder pattern)
  - JvmExample              (manual CountDownLatch flow)
  - JvmRemoteDebugExample   (arms the rdbg/DAP listener — blocks at binding.break
                             until a debugger attaches; see the example's
                             header for adb forward / VS Code instructions)
  - JvmRemoteEvalExample    (arms the line-eval console — connect with
                             \`rlwrap nc 127.0.0.1 7777\` and the cookie shown
                             at startup; holds scope for 5 min so you can poke)

Usage: $0 example <name>
  $0 example JvmExample
  $0 example JvmRemoteDebugExample
  $0 example JvmRemoteEvalExample

Outside docker (equivalent shell):
  cd examples/kotlin-jvm && ../../gradlew runExample -PexampleClass=<name>
EOF
            exit 0
        fi
        echo "🎯 Running example: $EXAMPLE_NAME"
        # --no-daemon keeps the JVM tied to this terminal: Ctrl-C kills the
        # actual example process, so debug listeners stop binding the host
        # port the moment you stop the example.
        docker-compose exec dev bash -c "cd examples/kotlin-jvm && ../../gradlew runExample -PexampleClass=$EXAMPLE_NAME --no-daemon"
        ;;

    examples)
        # Runs the non-interactive examples back-to-back. The debug example is
        # excluded on purpose — it blocks at binding.break waiting for a DAP
        # client and is not meant for unattended runs.
        echo "🎯 Running all non-interactive examples (skips JvmRemoteDebugExample)..."
        docker-compose exec dev bash -c "cd examples/kotlin-jvm && ../../gradlew runAllExamples --no-daemon"
        ;;

    static-test)
        echo "🧪 Building fully static libraries and running tests..."
        docker-compose exec dev bash -c "
            ./gradlew build -PbuildType=Debug -PbuildWrapperShared=false -PbuildSharedLibs=false -PforceRebuild=true && \
            cd build && \
            ./bin/test_core && \
            ./bin/test_jni
        "
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
  static-test       - Build fully static libraries and run tests
  example [name]    - Run a Kotlin/JVM example by class name (list with no arg)
  examples          - Run all non-interactive Kotlin/JVM examples sequentially
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
  ./docker-dev.sh example            # List available Kotlin/JVM examples
  ./docker-dev.sh example JvmRemoteDebugExample
                                     # Run the remote DAP debugger example
  ./docker-dev.sh examples           # Run all non-interactive examples
  ./docker-dev.sh export             # Export artifacts

Without docker (host-side equivalents):
  cd examples/kotlin-jvm && ../../gradlew runExample -PexampleClass=<name>
  cd examples/kotlin-jvm && ../../gradlew runAllExamples

Quick Workflow:
  1. ./docker-dev.sh setup           # One-time setup
  2. Make changes in your IDE
  3. ./docker-dev.sh sync            # Sync changes
  4. ./docker-dev.sh build           # Build in container
  5. ./docker-dev.sh export          # Get artifacts

EOF
        ;;
esac
