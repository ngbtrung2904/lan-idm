#!/bin/bash
set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"
branch="$(git -C "$repo_root" rev-parse --abbrev-ref HEAD)"

# Detached HEAD: fall back to the commit hash.
if [ "$branch" = "HEAD" ]; then
    branch="$(git -C "$repo_root" rev-parse --short HEAD)"
fi

# Branch names may contain '/', which cannot be used in a file name.
repo_name="$(basename "$repo_root")"
archive_name="${repo_name}-${branch//\//-}.zip"

git -C "$repo_root" archive --format=zip --output="$repo_root/$archive_name" "$branch"
echo "Archived '$branch' to $archive_name"
