#!/bin/bash
set -e

distro=${1:-xenial}
version=$(git describe|sed 's/^v//;s/-/./;s/-/~/')

if "$distro" -eq "trusty"; then
    cp debian/control{-qt4,}
else
    git checkout HEAD debian/control
fi

git checkout HEAD debian/changelog

dch \
  -M \
  -v "$version~$distro" \
  -D "$distro" "from git commit $(git rev-parse HEAD)"

git diff
