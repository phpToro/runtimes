#!/bin/bash
#
# build.sh — Build libphptoro_ext.a for a target platform.
#
# Usage:
#   ./build.sh ios-arm64          # iPhone device
#   ./build.sh ios-arm64-sim      # iPhone simulator (Apple Silicon)
#   ./build.sh macos-arm64        # macOS (Apple Silicon)
#

set -euo pipefail

TARGET="${1:-}"

if [ -z "$TARGET" ]; then
    echo "Usage: $0 <target>"
    echo "  Targets: ios-arm64, ios-arm64-sim, macos-arm64"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
EXT_DIR="$SCRIPT_DIR/ext"
BUILD_DIR="$SCRIPT_DIR/build/$TARGET"
PHP_DIR="$SCRIPT_DIR/php/$TARGET"

if [ ! -d "$PHP_DIR" ]; then
    echo "Error: PHP runtime not found: $PHP_DIR"
    exit 1
fi

PHP_INCLUDE="$PHP_DIR/include/php"

# Determine compiler flags per target
case "$TARGET" in
    ios-arm64)
        SDK="iphoneos"
        ARCH="arm64"
        MIN_VERSION="-miphoneos-version-min=16.0"
        ;;
    ios-arm64-sim)
        SDK="iphonesimulator"
        ARCH="arm64"
        MIN_VERSION="-mios-simulator-version-min=16.0"
        ;;
    macos-arm64)
        SDK="macosx"
        ARCH="arm64"
        MIN_VERSION="-mmacosx-version-min=14.0"
        ;;
    *)
        echo "Unknown target: $TARGET"
        exit 1
        ;;
esac

SDKROOT="$(xcrun --sdk "$SDK" --show-sdk-path)"
CC="$(xcrun --sdk "$SDK" --find clang)"

CFLAGS="-arch $ARCH $MIN_VERSION -isysroot $SDKROOT -O2 -Wall -Wextra -Wno-unused-parameter"
CFLAGS="$CFLAGS -I$PHP_INCLUDE -I$PHP_INCLUDE/main -I$PHP_INCLUDE/TSRM -I$PHP_INCLUDE/Zend -I$PHP_INCLUDE/ext"

mkdir -p "$BUILD_DIR"

SOURCES=(
    "$EXT_DIR/phptoro_sapi.c"
    "$EXT_DIR/phptoro_ext.c"
    "$EXT_DIR/phptoro_phpinfo.c"
)

OBJECTS=()

echo "Building libphptoro_ext.a for $TARGET..."

for src in "${SOURCES[@]}"; do
    name="$(basename "$src" .c)"
    obj="$BUILD_DIR/$name.o"
    echo "  CC $name.c"
    "$CC" $CFLAGS -c "$src" -o "$obj"
    OBJECTS+=("$obj")
done

OUTPUT="$BUILD_DIR/libphptoro_ext.a"
echo "  AR libphptoro_ext.a"
ar rcs "$OUTPUT" "${OBJECTS[@]}"

# Copy output to php/<target>/lib/ for convenience
cp "$OUTPUT" "$PHP_DIR/lib/libphptoro_ext.a"

echo "Done: $OUTPUT"
echo "  $(wc -c < "$OUTPUT" | tr -d ' ') bytes"
