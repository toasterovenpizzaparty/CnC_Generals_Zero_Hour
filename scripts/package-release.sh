#!/bin/sh
# Puts together the folder that players get as a zip: the exe, the installer and a README.
#   scripts/package-release.sh <generalszh.exe> <output folder> <version> <commit>
set -eu
exe=$1 out=$2 version=$3 commit=$4
here=$(dirname "$0")
mkdir -p "$out"
cp "$exe" "$out/generalszh.exe"
for f in Install.cmd Uninstall.cmd Install.ps1; do
	# Windows line endings, whatever the checkout used.
	sed 's/\r$//; s/$/\r/' "$here/install/$f" > "$out/$f"
done
sed 's/\r$//; s/@VERSION@/'"$version"'/g; s/@COMMIT@/'"$commit"'/g; s/$/\r/' "$here/install/README.txt" > "$out/README.txt"
(cd "$out" && sha256sum generalszh.exe > SHA256SUMS.txt)
