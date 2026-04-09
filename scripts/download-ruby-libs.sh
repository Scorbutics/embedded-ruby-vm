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
#   RUBY_LIBS_VERSION  Override the release tag (default: read from RUBY_LIBS_VERSION file)
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Defaults
REPO="${RUBY_LIBS_REPO:-Scorbutics/ruby-for-android}"
VERSION_FILE="$PROJECT_ROOT/RUBY_LIBS_VERSION"

if [ -n "${RUBY_LIBS_VERSION:-}" ]; then
    VERSION="$RUBY_LIBS_VERSION"
elif [ -f "$VERSION_FILE" ]; then
    VERSION="$(tr -d '[:space:]' < "$VERSION_FILE")"
else
    echo "Error: No RUBY_LIBS_VERSION file found and RUBY_LIBS_VERSION env not set." >&2
    exit 1
fi

echo "Ruby libs version: $VERSION"
echo "Repository: $REPO"

# Platform definitions: platform_name -> archive_name, triplet, needs_ios_rename
# Format: "archive_name:triplet[:ios_rename_from]"
# Note: entries assigned individually to avoid "unbound variable" errors with
#       set -u on some bash versions when using compound assignment syntax.
declare -A PLATFORMS
PLATFORMS["android-arm64"]="ruby_full-android-arm64.zip:aarch64-linux-android"
PLATFORMS["android-x86_64"]="ruby_full-android-x86_64.zip:x86_64-linux-android"
PLATFORMS["linux-x86_64"]="ruby_full-linux-x86_64.zip:x86_64-linux-gnu"
PLATFORMS["ios-device"]="ruby_full-ios-arm64.zip:aarch64-apple-ios-device:aarch64-apple-darwin"
PLATFORMS["ios-simulator"]="ruby_full-ios-arm64.zip:aarch64-apple-ios-simulator:aarch64-apple-darwin"

ALL_PLATFORMS=("android-arm64" "android-x86_64" "linux-x86_64" "ios-device" "ios-simulator")

# Parse arguments
if [ $# -gt 0 ]; then
    REQUESTED_PLATFORMS=("$@")
else
    REQUESTED_PLATFORMS=("${ALL_PLATFORMS[@]}")
fi

# Validate requested platforms
for plat in "${REQUESTED_PLATFORMS[@]}"; do
    if [ -z "${PLATFORMS[$plat]+x}" ]; then
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
    IFS=':' read -r archive_name triplet ios_rename_from <<< "${PLATFORMS[$plat]}"

    # Check if already downloaded for this version
    marker_file="$MARKER_DIR/${plat}.version"
    if [ -f "$marker_file" ] && [ "$(cat "$marker_file")" = "$VERSION" ]; then
        # Verify libs directory actually exists
        if [ -d "$PROJECT_ROOT/external/lib/$triplet" ]; then
            echo "[$plat] Already up to date ($VERSION), skipping."
            continue
        fi
    fi

    echo "[$plat] Downloading $archive_name..."

    url="https://github.com/$REPO/releases/download/$VERSION/$archive_name"
    zip_path="$DOWNLOAD_DIR/$plat-$archive_name"
    download_url "$url" "$zip_path"

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

    # Copy generic includes (ruby/, openssl/, zlib.h, zconf.h, ruby.h) — only if not present
    for item in ruby openssl; do
        if [ -d "$extract_dir/external/include/$item" ]; then
            if [ ! -d "$PROJECT_ROOT/external/include/$item" ]; then
                cp -r "$extract_dir/external/include/$item" "$PROJECT_ROOT/external/include/"
            fi
        fi
    done
    for header in ruby.h zlib.h zconf.h; do
        if [ -f "$extract_dir/external/include/$header" ]; then
            if [ ! -f "$PROJECT_ROOT/external/include/$header" ]; then
                cp "$extract_dir/external/include/$header" "$PROJECT_ROOT/external/include/"
            fi
        fi
    done

    # Copy asset files
    src_assets="$extract_dir/assets/files/$src_triplet"
    if [ -d "$src_assets" ]; then
        dest_assets="$PROJECT_ROOT/assets/files/$triplet"
        rm -rf "$dest_assets"
        mkdir -p "$(dirname "$dest_assets")"
        cp -r "$src_assets" "$dest_assets"
    fi
    # Copy generic ruby-stdlib.zip
    if [ -f "$extract_dir/assets/files/ruby-stdlib.zip" ]; then
        if [ ! -f "$PROJECT_ROOT/assets/files/ruby-stdlib.zip" ]; then
            mkdir -p "$PROJECT_ROOT/assets/files"
            cp "$extract_dir/assets/files/ruby-stdlib.zip" "$PROJECT_ROOT/assets/files/"
        fi
    fi

    # Write version marker
    echo "$VERSION" > "$marker_file"
    echo "[$plat] Done."
done

echo ""
echo "All requested platforms downloaded successfully."
