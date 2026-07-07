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

PLUGIN_DIR="Documents/VirtualDJ/PluginsMacArm/VideoEffect"
BUNDLE_NAME="TellyMedia-reborn.bundle"
VERSION="4.0.0"
IDENTIFIER="com.tellymedia.reborn.installer"

# Find the actual install path inside the root
if [ -d "$INSTALL_ROOT/$PLUGIN_DIR/$BUNDLE_NAME" ]; then
    PAYLOAD_SRC="$INSTALL_ROOT"
elif [ -d "$INSTALL_ROOT/usr/local/$PLUGIN_DIR/$BUNDLE_NAME" ]; then
    PAYLOAD_SRC="$INSTALL_ROOT/usr/local"
else
    echo "Error: Could not find $BUNDLE_NAME in $INSTALL_ROOT"
    exit 1
fi

echo "Building PKG installer..."
echo "  Source: $PAYLOAD_SRC"
echo "  Bundle: $PLUGIN_DIR/$BUNDLE_NAME"
echo ""

mkdir -p "$OUTPUT_DIR"

# Ad-hoc sign the bundles in the payload (required on Apple Silicon)
for BASE in "$PAYLOAD_SRC/Documents/VirtualDJ/PluginsMacArm/VideoEffect" \
            "$PAYLOAD_SRC/Library/Application Support/VirtualDJ/PluginsMacArm/VideoEffect"; do
    if [ -d "$BASE/$BUNDLE_NAME" ]; then
        codesign --force --deep -s - "$BASE/$BUNDLE_NAME" || true
    fi
done

# Postinstall script: remove quarantine + re-sign in the user's home
SCRIPTS_DIR="$OUTPUT_DIR/pkg-scripts"
mkdir -p "$SCRIPTS_DIR"
cat > "$SCRIPTS_DIR/postinstall" <<'EOF'
#!/bin/bash
# Runs as the installing user (home-dir install).
for DIR in "$HOME/Documents/VirtualDJ/PluginsMacArm/VideoEffect" \
           "$HOME/Library/Application Support/VirtualDJ/PluginsMacArm/VideoEffect"; do
    B="$DIR/TellyMedia-reborn.bundle"
    if [ -d "$B" ]; then
        xattr -dr com.apple.quarantine "$B" 2>/dev/null || true
        codesign --force --deep -s - "$B" 2>/dev/null || true
    fi
done
exit 0
EOF
chmod +x "$SCRIPTS_DIR/postinstall"

# Build the component package (relative to user's home directory)
pkgbuild \
    --root "$PAYLOAD_SRC" \
    --scripts "$SCRIPTS_DIR" \
    --identifier "$IDENTIFIER" \
    --version "$VERSION" \
    --install-location "/" \
    "$OUTPUT_DIR/TellyMedia-component.pkg"

# Create distribution XML that installs into the current user's home directory.
# enable_currentUserHome makes the payload land in ~/ instead of the system volume,
# which works on all modern macOS versions without admin rights.
cat > "$OUTPUT_DIR/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="1">
    <title>TellyMedia v4 for VirtualDJ</title>
    <options customize="never" require-scripts="true" hostArchitectures="arm64,x86_64"/>
    <domains enable_anywhere="false" enable_currentUserHome="true" enable_localSystem="false"/>
    <choices-outline>
        <line choice="default">
            <line choice="com.tellymedia.reborn"/>
        </line>
    </choices-outline>
    <choice id="default"/>
    <choice id="com.tellymedia.reborn" visible="false">
        <pkg-ref id="$IDENTIFIER"/>
    </choice>
    <pkg-ref id="$IDENTIFIER" version="$VERSION" onConclusion="none">TellyMedia-component.pkg</pkg-ref>
</installer-gui-script>
EOF

# Build the final product archive
productbuild \
    --distribution "$OUTPUT_DIR/distribution.xml" \
    --package-path "$OUTPUT_DIR" \
    "$OUTPUT_DIR/TellyMedia-reborn-$VERSION.pkg"

# Clean up intermediates
rm -f "$OUTPUT_DIR/TellyMedia-component.pkg" "$OUTPUT_DIR/distribution.xml"

echo ""
echo "Created: $OUTPUT_DIR/TellyMedia-reborn-$VERSION.pkg"
