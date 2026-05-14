#!/usr/bin/env bash
set -euo pipefail

# BY ZF: Build curl/libcurl for target devices with the local OpenSSL install.

TARGET="${1:-imx6ul}"
CURL_VERSION="${CURL_VERSION:-7.88.1}"
JOBS="${JOBS:-8}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CURL_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
EXTRALIB_DIR="$(cd "${CURL_DIR}/.." && pwd)"
PROJECT_ROOT="$(cd "${EXTRALIB_DIR}/.." && pwd)"

case "${TARGET}" in
  imx6ul)
    DEFAULT_HOST="arm-linux-gnueabihf"
    DEFAULT_OPENSSL_DIR="${EXTRALIB_DIR}/openSSL/install-arm"
    ;;
  nuc980)
    DEFAULT_HOST="${HOST:-arm-linux-gnueabi}"
    DEFAULT_OPENSSL_DIR="${EXTRALIB_DIR}/openSSL/install-nuc980"
    ;;
  jzq)
    DEFAULT_HOST="${HOST:-arm-linux-gnueabihf}"
    DEFAULT_OPENSSL_DIR="${EXTRALIB_DIR}/openSSL/install-jzq"
    ;;
  *)
    echo "Unsupported target: ${TARGET}" >&2
    echo "Usage: $0 imx6ul|nuc980|jzq" >&2
    exit 2
    ;;
esac

HOST="${HOST:-${DEFAULT_HOST}}"
OPENSSL_DIR="${OPENSSL_DIR:-${DEFAULT_OPENSSL_DIR}}"
INSTALL_DIR="${INSTALL_DIR:-${CURL_DIR}/install-${TARGET}}"
BUILD_DIR="${BUILD_DIR:-${CURL_DIR}/build-${TARGET}}"
SRC_DIR="${CURL_DIR}/curl-${CURL_VERSION}"
ARCHIVE="${CURL_DIR}/curl-${CURL_VERSION}.tar.xz"
URL="https://curl.se/download/curl-${CURL_VERSION}.tar.xz"

if [[ ! -d "${OPENSSL_DIR}" ]]; then
  echo "OpenSSL install dir not found: ${OPENSSL_DIR}" >&2
  echo "Set OPENSSL_DIR=/path/to/openssl/install or build OpenSSL first." >&2
  exit 1
fi

if [[ -z "${CROSS_COMPILE:-}" ]]; then
  CROSS_COMPILE="${HOST}-"
fi

export CC="${CC:-${CROSS_COMPILE}gcc}"
export AR="${AR:-${CROSS_COMPILE}ar}"
export RANLIB="${RANLIB:-${CROSS_COMPILE}ranlib}"
export STRIP="${STRIP:-${CROSS_COMPILE}strip}"
export LD="${LD:-${CROSS_COMPILE}ld}"

echo "PROJECT_ROOT=${PROJECT_ROOT}"
echo "TARGET=${TARGET}"
echo "HOST=${HOST}"
echo "CROSS_COMPILE=${CROSS_COMPILE}"
echo "CC=${CC}"
echo "OPENSSL_DIR=${OPENSSL_DIR}"
echo "INSTALL_DIR=${INSTALL_DIR}"

command -v "${CC}" >/dev/null

mkdir -p "${CURL_DIR}" "${BUILD_DIR}"

if [[ ! -f "${ARCHIVE}" ]]; then
  if command -v curl >/dev/null 2>&1; then
    curl -L "${URL}" -o "${ARCHIVE}"
  elif command -v wget >/dev/null 2>&1; then
    wget -O "${ARCHIVE}" "${URL}"
  else
    echo "Neither curl nor wget is available to download ${URL}" >&2
    exit 1
  fi
fi

if [[ ! -d "${SRC_DIR}" ]]; then
  tar xf "${ARCHIVE}" -C "${CURL_DIR}"
fi

cd "${SRC_DIR}"
make distclean >/dev/null 2>&1 || true

export CPPFLAGS="-I${OPENSSL_DIR}/include ${CPPFLAGS:-}"
export LDFLAGS="-L${OPENSSL_DIR}/lib -Wl,-rpath-link,${OPENSSL_DIR}/lib ${LDFLAGS:-}"
export PKG_CONFIG_PATH="${OPENSSL_DIR}/lib/pkgconfig:${PKG_CONFIG_PATH:-}"

./configure \
  --host="${HOST}" \
  --prefix="${INSTALL_DIR}" \
  --with-openssl="${OPENSSL_DIR}" \
  --enable-shared \
  --disable-static \
  --disable-ldap \
  --disable-ldaps \
  --disable-rtsp \
  --disable-dict \
  --disable-telnet \
  --disable-tftp \
  --disable-pop3 \
  --disable-imap \
  --disable-smtp \
  --disable-gopher \
  --disable-mqtt \
  --disable-smb \
  --disable-manual \
  --without-libidn2 \
  --without-nghttp2 \
  --without-zlib \
  --without-brotli \
  --without-zstd \
  --without-libpsl

make -j"${JOBS}"
make install

"${STRIP}" "${INSTALL_DIR}/bin/curl" >/dev/null 2>&1 || true
find "${INSTALL_DIR}/lib" -type f -name 'libcurl.so*' -exec "${STRIP}" {} \; >/dev/null 2>&1 || true

echo
echo "Build finished."
echo "curl binary: ${INSTALL_DIR}/bin/curl"
echo "libcurl dir: ${INSTALL_DIR}/lib"
echo
echo "Check target runtime with:"
echo "LD_LIBRARY_PATH=/usr/app/extraLib/curl/lib:/usr/app/extraLib/openSSL/lib /usr/app/extraLib/curl/bin/curl --version"
