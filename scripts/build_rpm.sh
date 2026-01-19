#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NAME="k10-barrel-emulator"
VERSION="$(cat "${ROOT}/VERSION")"
BUILDROOT="${ROOT}/.build"
RPMBUILD="${BUILDROOT}/rpmbuild"
TARBALL="${NAME}-${VERSION}.tar.gz"

rm -rf "${BUILDROOT}"
mkdir -p "${RPMBUILD}"/{BUILD,RPMS,SOURCES,SPECS,SRPMS}

tar --exclude-vcs --exclude=.build --exclude=build \
  --transform "s,^,${NAME}-${VERSION}/," \
  -czf "${RPMBUILD}/SOURCES/${TARBALL}" -C "${ROOT}" .
cp "${ROOT}/packaging/rpm/${NAME}.spec" "${RPMBUILD}/SPECS/"

rpmbuild --define "_topdir ${RPMBUILD}" -ba "${RPMBUILD}/SPECS/${NAME}.spec"

echo "RPMs written to ${RPMBUILD}/RPMS"
