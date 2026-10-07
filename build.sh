#!/bin/bash
set -e

# VitaSDK Environment
export VITASDK="${VITASDK:-$HOME/Developer/vitasdk}"
export PATH="/opt/homebrew/bin:$VITASDK/bin:$PATH"

if [ ! -d "$VITASDK" ]; then
    echo "Error: VitaSDK not found at $VITASDK"
    exit 1
fi

echo "==> Building vita-luna package using VitaSDK..."
mkdir -p build
cd build
cmake -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" ..
make -j$(sysctl -n hw.ncpu)

echo ""
echo "=========================================================="
echo " SUCCESS! Built: $(pwd)/vita-luna.vpk"
echo "=========================================================="

if [ -n "$1" ]; then
    echo ""
    python3 ../scripts/deploy_vitacompanion.py "$1" "$(pwd)/vita-luna.vpk"
fi
