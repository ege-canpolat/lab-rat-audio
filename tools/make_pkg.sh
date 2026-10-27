#!/bin/bash
# Builds a macOS installer (.pkg) for one of the playground's plugins.
#
#   tools/make_pkg.sh <PluginName> <version> <artefacts dir> <installer resources dir> <output .pkg>
#
# The artefacts dir is the JUCE build output with VST3/ and AU/ inside, e.g.
#   Elsewhere/build-share/Elsewhere_artefacts/Release
# The resources dir holds welcome.html and conclusion.html for the installer screens.
# The developer name and bundle ID prefix come from developer.cmake.
#
# The package is unsigned: on first open, macOS sends people to
# System Settings > Privacy & Security > "Open Anyway". Set SIGN_IDENTITY to a
# "Developer ID Installer: ..." identity to sign it instead.
set -euo pipefail
export COPYFILE_DISABLE=1

if [ $# -ne 5 ]; then
    sed -n '2,13p' "$0"
    exit 1
fi

NAME=$1
VERSION=$2
ARTEFACTS=$3
RESOURCES=$4
OUTPUT=$5
# The studio name and ID come from developer.cmake, like the plugins themselves.
CONFIG="$(cd "$(dirname "$0")/.." && pwd)/developer.cmake"
setting() { sed -n "s/^set($1 *\"\(.*\)\")/\1/p" "$CONFIG"; }
DEVELOPER_NAME=$(setting DEVELOPER_NAME)
DEVELOPER_ID=$(setting DEVELOPER_ID)
ID="com.$DEVELOPER_ID.$(echo "$NAME" | tr '[:upper:]' '[:lower:]')"

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# One component package per plugin format.
component() {
    local format=$1 extension=$2 destination=$3
    local root="$WORK/root-$format"
    mkdir -p "$root"
    # No resource forks or extended attributes: they turn into stray "._" files in the payload.
    ditto --norsrc --noextattr --noacl "$ARTEFACTS/$format/$NAME.$extension" "$root/$NAME.$extension"

    # Without this, Installer "relocates" the bundle to wherever it finds another copy
    # with the same bundle ID (say, a build folder) instead of the plug-ins folder.
    pkgbuild --analyze --root "$root" "$WORK/$format.plist" > /dev/null
    plutil -replace 0.BundleIsRelocatable -bool NO "$WORK/$format.plist"

    pkgbuild --root "$root" --component-plist "$WORK/$format.plist" \
             --identifier "$ID.$(echo "$format" | tr '[:upper:]' '[:lower:]')" --version "$VERSION" \
             --install-location "$destination" "$WORK/$NAME-$format.pkg" > /dev/null
}

component VST3 vst3      "/Library/Audio/Plug-Ins/VST3"
component AU   component "/Library/Audio/Plug-Ins/Components"

cat > "$WORK/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>$NAME $VERSION</title>
    <welcome file="welcome.html" mime-type="text/html"/>
    <conclusion file="conclusion.html" mime-type="text/html"/>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <allowed-os-versions><os-version min="11.0"/></allowed-os-versions>
    <domains enable_localSystem="true"/>
    <choices-outline>
        <line choice="vst3"/>
        <line choice="au"/>
    </choices-outline>
    <choice id="vst3" title="VST3" description="For Ableton Live, FL Studio, Bitwig, Reaper, Cubase and most other DAWs.">
        <pkg-ref id="$ID.vst3"/>
    </choice>
    <choice id="au" title="Audio Unit" description="For Logic Pro, GarageBand and other Audio Unit hosts.">
        <pkg-ref id="$ID.au"/>
    </choice>
    <pkg-ref id="$ID.vst3" version="$VERSION">$NAME-VST3.pkg</pkg-ref>
    <pkg-ref id="$ID.au" version="$VERSION">$NAME-AU.pkg</pkg-ref>
</installer-gui-script>
EOF

# Installer screens can say @DEVELOPER_NAME@ and @VERSION@.
mkdir -p "$WORK/resources"
for f in "$RESOURCES"/*; do
    sed -e "s/@DEVELOPER_NAME@/$DEVELOPER_NAME/g" -e "s/@VERSION@/$VERSION/g" "$f" > "$WORK/resources/$(basename "$f")"
done

mkdir -p "$(dirname "$OUTPUT")"
if [ -n "${SIGN_IDENTITY:-}" ]; then
    productbuild --distribution "$WORK/distribution.xml" --resources "$WORK/resources" --package-path "$WORK" \
                 --sign "$SIGN_IDENTITY" "$OUTPUT"
else
    productbuild --distribution "$WORK/distribution.xml" --resources "$WORK/resources" --package-path "$WORK" "$OUTPUT"
fi
echo "wrote $OUTPUT"
