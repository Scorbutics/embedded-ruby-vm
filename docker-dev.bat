@echo off
REM Docker development helper script for embedded-ruby-vm (Windows)

setlocal EnableDelayedExpansion

set CMD=%1

if "%CMD%"=="setup" goto setup
if "%CMD%"=="build-image" goto build-image
if "%CMD%"=="sync" goto sync
if "%CMD%"=="start" goto start
if "%CMD%"=="stop" goto stop
if "%CMD%"=="shell" goto shell
if "%CMD%"=="build" goto build
if "%CMD%"=="desktop-jar" goto desktop-jar
if "%CMD%"=="test" goto test
if "%CMD%"=="export" goto export
if "%CMD%"=="clean" goto clean
if "%CMD%"=="clean-all" goto clean-all
if "%CMD%"=="logs" goto logs
if "%CMD%"=="android-setup" goto android-setup
if "%CMD%"=="android-shell" goto android-shell
goto help

:setup
echo Setting up Docker development environment...
docker-compose build
docker-compose run --rm source-sync-in
docker-compose up -d dev
echo Setup complete! Run 'docker-dev.bat shell' to enter the container.
goto end

:build-image
echo Building Docker image...
docker-compose build
goto end

:sync
echo Syncing source code to container...
docker-compose run --rm source-sync-in
echo Source code synced!
goto end

:start
echo Starting development container...
docker-compose up -d dev
echo Container started! Run 'docker-dev.bat shell' to enter.
goto end

:stop
echo Stopping development container...
docker-compose down
echo Container stopped!
goto end

:shell
echo Entering development container...
docker-compose exec dev bash
goto end

:build
set TARGET_ARCH=%2
if "%TARGET_ARCH%"=="" (
    echo Building project ^(auto-detect architecture^)...
    docker-compose exec dev ./gradlew build
) else (
    echo Building project for %TARGET_ARCH%...
    docker-compose exec dev ./gradlew build -PtargetArch=%TARGET_ARCH%
)
goto end

:desktop-jar
echo Building desktop JAR...
docker-compose exec dev ./gradlew :ruby-vm-kmp:desktopJar
goto end

:test
echo Running tests...
docker-compose exec dev bash -c "cd build && ./bin/test_core && ./bin/test_jni"
goto end

:export
echo Exporting build artifacts...
docker-compose run --rm artifact-export
echo Artifacts available in .\docker-output\
goto end

:clean
echo Cleaning build artifacts...
docker-compose exec dev bash -c "./gradlew clean && rm -rf build kmp/build"
docker volume rm embedded-ruby-vm_build-artifacts embedded-ruby-vm_kmp-artifacts 2>nul
echo Build artifacts cleaned!
goto end

:clean-all
echo Cleaning all volumes and containers...
docker-compose down -v
echo All volumes removed! Run 'docker-dev.bat setup' to start fresh.
goto end

:logs
docker-compose logs -f dev
goto end

:android-setup
echo Setting up Android development environment...
docker-compose -f docker-compose.yml -f docker-compose.override.yml build dev-android
docker-compose run --rm source-sync-in
docker-compose -f docker-compose.yml -f docker-compose.override.yml up -d dev-android
echo Android setup complete!
goto end

:android-shell
echo Entering Android development container...
docker-compose exec dev-android bash
goto end

:help
echo Docker Development Helper for embedded-ruby-vm
echo.
echo Usage: docker-dev.bat ^<command^> [arguments]
echo.
echo Setup Commands:
echo   setup             - Initial setup: build image, sync code, start container
echo   build-image       - Build Docker image only
echo   sync              - Sync source code from host to container
echo   start             - Start the development container
echo   stop              - Stop the development container
echo.
echo Development Commands:
echo   shell             - Enter the development container shell
echo   build [arch]      - Build the project ^(optional: x86_64, arm64, all^)
echo   desktop-jar       - Build desktop JAR
echo   test              - Run all tests
echo   export            - Export build artifacts to .\docker-output\
echo.
echo Maintenance Commands:
echo   clean             - Clean build artifacts ^(keeps cache^)
echo   clean-all         - Remove all volumes and containers ^(fresh start^)
echo   logs              - View container logs
echo.
echo Android Commands:
echo   android-setup     - Setup Android development environment
echo   android-shell     - Enter Android development container
echo.
echo Examples:
echo   docker-dev.bat setup              # Initial setup
echo   docker-dev.bat sync               # Sync code changes
echo   docker-dev.bat shell              # Enter container
echo   docker-dev.bat build x86_64       # Build for x86_64
echo   docker-dev.bat export             # Export artifacts
echo.
echo Quick Workflow:
echo   1. docker-dev.bat setup           # One-time setup
echo   2. Make changes in your IDE
echo   3. docker-dev.bat sync            # Sync changes
echo   4. docker-dev.bat build           # Build in container
echo   5. docker-dev.bat export          # Get artifacts
goto end

:end
endlocal
