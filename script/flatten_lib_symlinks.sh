#!/bin/bash
# Flatten symlinks in a directory by replacing each symlink with a copy of its target.
# This is needed because FAT32/exFAT SD cards do not support symlinks.

set -e

LIB_DIR="$1"
if [ -z "$LIB_DIR" ]; then
    echo "Usage: $0 <lib_dir>"
    exit 1
fi

if [ ! -d "$LIB_DIR" ]; then
    echo "Directory not found: $LIB_DIR"
    exit 1
fi

for link in $(find "$LIB_DIR" -maxdepth 1 -type l); do
    target=$(readlink -f "$link")
    if [ -f "$target" ]; then
        rm -f "$link"
        cp -a "$target" "$link"
        echo "Flattened: $(basename "$link") -> $(basename "$target")"
    fi
done

echo "Done flattening symlinks in $LIB_DIR"
