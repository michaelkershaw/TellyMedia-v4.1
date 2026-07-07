#!/bin/bash
#
# TellyMedia v4 - macOS PKG Installer Build Script
# Creates a .pkg installer that installs the plugin to the user's VirtualDJ plugins folder.
#
# Usage:
#   ./build_pkg.sh <install_root> <output_dir>
#
# Arguments:
#   install_root  - Directory containing the installed bundle (from make install DESTDIR=...)
#   output_dir    - Where to write the .pkg file
#

set -e

INSTALL_ROOT="${1:?Error: install_root argument required}"
OUTPUT_DIR="${2:?Error: output_dir argument required}"

PLUGIN_DIR="Library/Application Support/VirtualDJ/PluginsMacArm/VideoEffect"
BUNDLE_NAME="TellyMedia-reborn.bundle"
VERSION="4.0.0"
IDENTIFIER="com.tellymedia.reborn.installer"

# Find the actual install path inside the root
# (make install with DESTDIR may prepend /usr/local)
if [ -d "$INSTALL_ROOT/usr/local/$PLUGIN_DIR/$BUNDLE_NAME" ]; then
    PAYLOAD_SRC="$INSTALL_ROOT/usr/local"
elif [ -d "$INSTALL_ROOT/$PLUGIN_DIR/$BUNDLE_NAME" ]; then
    PAYLOAD_SRC="$INSTALL_ROOT"
else
    echo "Error: Could not find $BUNDLE_NAME in $INSTALL_ROOT"
    exit 1
fi

echo "Building PKG installer..."
echo "  Source: $PAYLOAD_SRC"
echo "  Bundle: $PLUGIN_DIR/$BUNDLE_NAME"
echo ""

mkdir -p "$OUTPUT_DIR"

# Build the component package
pkgbuild \
    --root "$PAYLOAD_SRC" \
    --identifier "$IDENTIFIER" \
    --version "$VERSION" \
    --install-location "/" \
    "$OUTPUT_DIR/TellyMedia-reborn-$VERSION.pkg"

echo ""
echo "Created: $OUTPUT_DIR/TellyMedia-reborn-$VERSION.pkg"
