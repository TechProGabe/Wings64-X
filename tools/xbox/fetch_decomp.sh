#!/usr/bin/env bash
# fetch_decomp.sh - the Pilotwings 64 decompilation at the pin, patched from
# patches/, in external/pw64-decomp (gitignored; PLAN.md D1, M2).
#
#   tools/xbox/fetch_decomp.sh             fetch if missing or stale, else nothing
#   tools/xbox/fetch_decomp.sh --refresh   throw the tree away and fetch it clean
#
# The decomp is consumed downstream under MIT at a pinned commit, never edited
# in place and never contributed to (its CONTRIBUTING.md bans AI-generated
# contributions; CLAUDE.md). Every change is a patch in patches/ with a .md,
# applied here in name order with `patch -p1`; a patch that does not apply
# exactly stops the script (no fuzz, no partial tree left behind as current).
#
# Runs in Git Bash on Windows (Git for Windows ships GNU patch; MSYS2's
# `pacman -S patch` works too) and on Linux / in the docker image.
set -euo pipefail

# gcsmith/Pilotwings64Decomp redirects here. 1a8558f, 2026-09-28: the revision
# wings64-dc's patches were written against (its docker/pins.env DECOMP_REV).
DECOMP_URL=https://github.com/n64decomp/pw64.git
DECOMP_REV=1a8558f5ceb320cce254d6da0a49a1f2bbc23e21

root="$(cd "$(dirname "$0")/../.." && pwd)"
dest="$root/external/pw64-decomp"
stamp="$dest/.wings64-x-stamp"

refresh=0
case "${1:-}" in
    --refresh) refresh=1 ;;
    "") ;;
    *) echo "usage: $0 [--refresh]" >&2; exit 2 ;;
esac

command -v git >/dev/null || { echo "fetch_decomp: git not found" >&2; exit 1; }
command -v patch >/dev/null || { echo "fetch_decomp: GNU patch not found (MSYS2: pacman -S patch)" >&2; exit 1; }

shopt -s nullglob
patches=("$root"/patches/*.patch)
# sort -s with LC_ALL=C: the name order is the byte order, on every host
if [ ${#patches[@]} -gt 0 ]; then
    mapfile -t patches < <(printf '%s\n' "${patches[@]}" | LC_ALL=C sort)
fi
if command -v sha256sum >/dev/null; then sha() { sha256sum; }; else sha() { shasum -a 256; }; fi
phash=$( { [ ${#patches[@]} -gt 0 ] && cat "${patches[@]}"; true; } | sha | cut -c1-16)
want="$DECOMP_REV $phash"

if [ $refresh = 0 ] && [ -f "$stamp" ] && [ "$(cat "$stamp")" = "$want" ]; then
    echo "fetch_decomp: external/pw64-decomp is current (${DECOMP_REV:0:7}, ${#patches[@]} patches)"
    exit 0
fi

# Build in a temporary directory beside the destination; it replaces the old
# tree only once every patch has applied.
tmp="$root/external/.pw64-decomp.tmp"
rm -rf "$tmp"
mkdir -p "$tmp"
trap 'rm -rf "$tmp"' EXIT
(
    cd "$tmp"
    git init -q
    # LF as committed, whatever the host's core.autocrlf (Git for Windows
    # defaults to true): the patches are LF and the tree must be the same on
    # every host
    git config core.autocrlf false
    git config core.eol lf
    git remote add origin "$DECOMP_URL"
    # one commit, shallow: a fetch by full SHA (GitHub allows it for reachable commits)
    git -c advice.detachedHead=false fetch -q --depth 1 origin "$DECOMP_REV"
    git -c advice.detachedHead=false checkout -q FETCH_HEAD
    got="$(git rev-parse HEAD)"
    [ "$got" = "$DECOMP_REV" ] || { echo "fetch_decomp: $DECOMP_URL gave $got, want $DECOMP_REV" >&2; exit 1; }
    rm -rf .git
)

for p in ${patches[@]+"${patches[@]}"}; do
    name="$(basename "$p")"
    # --fuzz=0: the context must match exactly; --forward: never reverse a hunk;
    # --no-backup-if-mismatch: no .orig files in the tree
    if ! out="$(patch -d "$tmp" -p1 --forward --fuzz=0 --no-backup-if-mismatch -r - < "$p" 2>&1)"; then
        echo "$out" >&2
        echo "fetch_decomp: $name does not apply to ${DECOMP_REV:0:7}" >&2
        exit 1
    fi
    echo "fetch_decomp: applied $name"
done

echo "$want" > "$tmp/.wings64-x-stamp"
rm -rf "$dest"
mv "$tmp" "$dest"
trap - EXIT
echo "fetch_decomp: external/pw64-decomp at ${DECOMP_REV:0:7} (+${#patches[@]} patches)"
