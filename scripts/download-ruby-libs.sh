#!/bin/bash
#
# Download prebuilt Ruby libraries from ruby-for-android GitHub Releases.
#
# Usage:
#   ./scripts/download-ruby-libs.sh [PLATFORM...]
#
# If no platforms are specified, downloads all platforms.
#
# Examples:
#   ./scripts/download-ruby-libs.sh                           # All platforms
#   ./scripts/download-ruby-libs.sh android-arm64             # Only Android ARM64
#   ./scripts/download-ruby-libs.sh linux-x86_64 ios-device   # Linux + iOS device
#
# Environment variables:
#   GITHUB_TOKEN       Optional token for authenticated GitHub API requests
#   RUBY_LIBS_REPO     Override the GitHub repo (default: Scorbutics/ruby-for-android)
#   RUBY_VARIANT       Ruby minor version to download (default: 3.0), a key of RUBY_LIBS_VERSIONS
#   RUBY_LIBS_VERSION  Override the release tag (default: the RUBY_VARIANT line of RUBY_LIBS_VERSIONS)
#   RUBY_LIBS_LOCAL_DIR  Take the ruby_full-*.zip archives from this directory instead of
#                        downloading them (e.g. a local ruby-for-android build's target/)
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Defaults
REPO="${RUBY_LIBS_REPO:-Scorbutics/ruby-for-android}"
VERSION_FILE="$PROJECT_ROOT/RUBY_LIBS_VERSIONS"
VARIANT="${RUBY_VARIANT:-3.0}"

if [ -n "${RUBY_LIBS_VERSION:-}" ]; then
    VERSION="$RUBY_LIBS_VERSION"
else
    VERSION="$(awk -v v="$VARIANT" '$1 == v { print $2 }' "$VERSION_FILE")"
    if [ -z "$VERSION" ]; then
        echo "Error: Ruby variant '$VARIANT' has no line in $VERSION_FILE." >&2
        exit 1
    fi
fi
if [ -n "${RUBY_LIBS_LOCAL_DIR:-}" ]; then
    # Never let a local build pass for the pinned release in the markers, and
    # always reinstall it: the same tag may name several local builds.
    VERSION="$VERSION-local"
fi

echo "Ruby variant: $VARIANT"
echo "Ruby libs version: $VERSION"
echo "Repository: $REPO"

# Platform definitions: platform_name -> "archive_name:triplet[:ios_rename_from]"
# Uses a function instead of associative arrays for bash 3.x compatibility (macOS).
ALL_PLATFORMS=("android-arm64" "android-x86_64" "linux-x86_64" "ios-device" "ios-simulator" "macos-arm64")

platform_info() {
    case "$1" in
        android-arm64)   echo "ruby_full-android-arm64.zip:aarch64-linux-android" ;;
        android-x86_64)  echo "ruby_full-android-x86_64.zip:x86_64-linux-android" ;;
        linux-x86_64)    echo "ruby_full-linux-x86_64.zip:x86_64-linux-gnu" ;;
        ios-device)      echo "ruby_full-ios-device-arm64.zip:aarch64-apple-ios-device:aarch64-apple-darwin" ;;
        ios-simulator)   echo "ruby_full-ios-simulator-arm64.zip:aarch64-apple-ios-simulator:aarch64-apple-darwin" ;;
        macos-arm64)     echo "ruby_full-macos-arm64.zip:aarch64-apple-darwin" ;;
        *)               return 1 ;;
    esac
}

# Parse arguments
if [ $# -gt 0 ]; then
    REQUESTED_PLATFORMS=("$@")
else
    REQUESTED_PLATFORMS=("${ALL_PLATFORMS[@]}")
fi

# Validate requested platforms
for plat in "${REQUESTED_PLATFORMS[@]}"; do
    if ! platform_info "$plat" > /dev/null 2>&1; then
        echo "Error: Unknown platform '$plat'. Available: ${ALL_PLATFORMS[*]}" >&2
        exit 1
    fi
done

# Version marker directory
MARKER_DIR="$PROJECT_ROOT/external/lib/.versions"
mkdir -p "$MARKER_DIR"

# Download helper
download_url() {
    local url="$1"
    local output="$2"
    local curl_args=(-fSL --retry 3 --retry-delay 5 -o "$output")
    if [ -n "${GITHUB_TOKEN:-}" ]; then
        curl_args+=(-H "Authorization: token $GITHUB_TOKEN")
    fi
    curl "${curl_args[@]}" "$url"
}

DOWNLOAD_DIR="$(mktemp -d)"
trap "rm -rf '$DOWNLOAD_DIR'" EXIT

for plat in "${REQUESTED_PLATFORMS[@]}"; do
    IFS=':' read -r archive_name triplet ios_rename_from <<< "$(platform_info "$plat")"

    # Check if already downloaded for this version
    marker_file="$MARKER_DIR/${plat}.version"
    if [ -z "${RUBY_LIBS_LOCAL_DIR:-}" ] && [ -f "$marker_file" ] && [ "$(cat "$marker_file")" = "$VERSION" ]; then
        # Verify libs directory actually exists
        if [ -d "$PROJECT_ROOT/external/lib/$triplet" ]; then
            echo "[$plat] Already up to date ($VERSION), skipping."
            continue
        fi
    fi

    zip_path="$DOWNLOAD_DIR/$plat-$archive_name"
    if [ -n "${RUBY_LIBS_LOCAL_DIR:-}" ]; then
        local_zip="$(find "$RUBY_LIBS_LOCAL_DIR" -name "$archive_name" | head -1)"
        if [ -z "$local_zip" ]; then
            echo "Error: [$plat] $archive_name not found under $RUBY_LIBS_LOCAL_DIR" >&2
            exit 1
        fi
        echo "[$plat] Using local $local_zip"
        cp "$local_zip" "$zip_path"
    else
        echo "[$plat] Downloading $archive_name..."
        url="https://github.com/$REPO/releases/download/$VERSION/$archive_name"
        download_url "$url" "$zip_path"
    fi

    echo "[$plat] Extracting..."
    extract_dir="$DOWNLOAD_DIR/$plat-extracted"
    mkdir -p "$extract_dir"
    unzip -qo "$zip_path" -d "$extract_dir"

    # Determine source triplet (for iOS, the archive uses aarch64-apple-darwin)
    src_triplet="${ios_rename_from:-$triplet}"

    # Copy libraries
    src_lib="$extract_dir/external/lib/$src_triplet"
    if [ -d "$src_lib" ]; then
        dest_lib="$PROJECT_ROOT/external/lib/$triplet"
        rm -rf "$dest_lib"
        mkdir -p "$(dirname "$dest_lib")"
        cp -r "$src_lib" "$dest_lib"
        echo "[$plat] Installed libs -> external/lib/$triplet/"
    else
        echo "Warning: [$plat] No library directory found at external/lib/$src_triplet in archive" >&2
    fi

    # Copy platform-specific includes
    src_inc="$extract_dir/external/include/$src_triplet"
    if [ -d "$src_inc" ]; then
        dest_inc="$PROJECT_ROOT/external/include/$triplet"
        rm -rf "$dest_inc"
        mkdir -p "$(dirname "$dest_inc")"
        cp -r "$src_inc" "$dest_inc"
        echo "[$plat] Installed includes -> external/include/$triplet/"
    fi

    # Generic files (ruby/, openssl/, ruby.h, zlib.h, zconf.h, ruby-stdlib.zip)
    # are the same in every platform archive of a release: install them once,
    # and replace them when the release changes (another Ruby variant has
    # other headers and another stdlib).
    generic_marker="$MARKER_DIR/generic.version"
    if [ -n "${RUBY_LIBS_LOCAL_DIR:-}" ] && [ -z "${generic_done:-}" ] ||
       [ ! -f "$generic_marker" ] || [ "$(cat "$generic_marker")" != "$VERSION" ]; then
        generic_done=1
        for item in ruby openssl ruby.h zlib.h zconf.h; do
            if [ -e "$extract_dir/external/include/$item" ]; then
                rm -rf "$PROJECT_ROOT/external/include/$item"
                cp -r "$extract_dir/external/include/$item" "$PROJECT_ROOT/external/include/"
            fi
        done
        if [ -f "$extract_dir/assets/files/ruby-stdlib.zip" ]; then
            mkdir -p "$PROJECT_ROOT/assets/files"
            cp "$extract_dir/assets/files/ruby-stdlib.zip" "$PROJECT_ROOT/assets/files/"
        fi
        echo "$VERSION" > "$generic_marker"
        echo "[$plat] Installed generic includes and ruby-stdlib.zip"
    fi

    # Copy asset files
    src_assets="$extract_dir/assets/files/$src_triplet"
    if [ -d "$src_assets" ]; then
        dest_assets="$PROJECT_ROOT/assets/files/$triplet"
        rm -rf "$dest_assets"
        mkdir -p "$(dirname "$dest_assets")"
        cp -r "$src_assets" "$dest_assets"
    fi

    # Write version marker
    echo "$VERSION" > "$marker_file"
    echo "[$plat] Done."
done

echo ""
echo "All requested platforms downloaded successfully."
