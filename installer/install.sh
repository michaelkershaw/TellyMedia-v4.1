#!/bin/bash
#
# TellyMedia v4 - macOS Installer Script
# Installs the TellyMedia-reborn.bundle plugin to VirtualDJ's macOS ARM plugins folder.
#
# Usage:
#   ./install.sh              # Install from local build
#   ./install.sh <tar.gz>     # Install from downloaded artifact
#

set -e

PLUGIN_NAME="TellyMedia-reborn.bundle"
VDJ_DIR_DOCS="$HOME/Documents/VirtualDJ/PluginsMacArm/VideoEffect"
VDJ_DIR_LIB="$HOME/Library/Application Support/VirtualDJ/PluginsMacArm/VideoEffect"

echo "============================================"
echo "  TellyMedia v4 - macOS Installer"
echo "============================================"
echo ""

# Create target directories
mkdir -p "$VDJ_DIR_DOCS"
mkdir -p "$VDJ_DIR_LIB"

if [ $# -eq 1 ]; then
    # Install from tar.gz artifact
    TARFILE="$1"
    if [ ! -f "$TARFILE" ]; then
        echo "Error: File not found: $TARFILE"
        exit 1
    fi
    echo "Installing from: $TARFILE"
    tar -xzf "$TARFILE" -C "$HOME/"
    echo ""
    echo "Installed to:"
    echo "  $VDJ_DIR_DOCS/$PLUGIN_NAME"
    echo "  $VDJ_DIR_LIB/$PLUGIN_NAME"
else
    # Install from local build directory
    SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
    PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

    if [ -d "$PROJECT_DIR/build" ]; then
        SOURCE="$PROJECT_DIR/build/$PLUGIN_NAME"
    elif [ -d "$PROJECT_DIR/build/install/Documents/VirtualDJ/PluginsMacArm/VideoEffect/$PLUGIN_NAME" ]; then
        SOURCE="$PROJECT_DIR/build/install/Documents/VirtualDJ/PluginsMacArm/VideoEffect/$PLUGIN_NAME"
    else
        echo "Error: Could not find built plugin. Run cmake/make first, or pass a tar.gz file."
        echo "  ./install.sh TellyMedia-reborn-macOS-ARM.tar.gz"
        exit 1
    fi

    echo "Installing from: $SOURCE"
    cp -R "$SOURCE" "$VDJ_DIR_DOCS/"
    cp -R "$SOURCE" "$VDJ_DIR_LIB/"
    echo "Installed to:"
    echo "  $VDJ_DIR_DOCS/$PLUGIN_NAME"
    echo "  $VDJ_DIR_LIB/$PLUGIN_NAME"
fi

echo ""
echo "Installation complete!"
echo "Restart VirtualDJ to load the plugin."
echo "============================================"
