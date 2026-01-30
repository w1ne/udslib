#!/bin/bash
# Build script for iso14229 external validator

set -e

cd external/iso14229

echo "Attempting to build iso14229_server..."

# We manually compile because cmake/bazel might be missing
# Includes needed: 
# . (for iso14229.h)
# src (for internal headers like tp.h, uds.h, util.h)
# src/tp (for isotp_sock.h)

gcc -DUDS_TP_ISOTP_SOCK -D_GNU_SOURCE \
    -I. \
    examples/linux_server/main.c \
    iso14229.c \
    -o ../../iso14229_server

if [ -f "../../iso14229_server" ]; then
    echo "✅ Successfully built iso14229_server"
else
    echo "❌ Failed to build iso14229_server"
    exit 1
fi
