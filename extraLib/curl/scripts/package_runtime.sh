#!/usr/bin/env bash
set -euo pipefail

# BY ZF: Collect curl, libcurl and OpenSSL runtime files for target deployment.

TARGET="${1:-imx6ul}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CURL_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
EXTRALIB_DIR="$(cd "${CURL_DIR}/.." && pwd)"

case "${TARGET}" in
  imx6ul)
    DEFAULT_OPENSSL_DIR="${EXTRALIB_DIR}/openSSL/install-arm"
    ;;
  nuc980)
    DEFAULT_OPENSSL_DIR="${EXTRALIB_DIR}/openSSL/install-nuc980"
    ;;
  jzq)
    DEFAULT_OPENSSL_DIR="${EXTRALIB_DIR}/openSSL/install-jzq"
    ;;
  *)
    echo "Unsupported target: ${TARGET}" >&2
    echo "Usage: $0 imx6ul|nuc980|jzq" >&2
    exit 2
    ;;
esac

INSTALL_DIR="${INSTALL_DIR:-${CURL_DIR}/install-${TARGET}}"
OPENSSL_DIR="${OPENSSL_DIR:-${DEFAULT_OPENSSL_DIR}}"
RUNTIME_DIR="${RUNTIME_DIR:-${CURL_DIR}/runtime-${TARGET}}"

if [[ ! -x "${INSTALL_DIR}/bin/curl" ]]; then
  echo "curl binary not found: ${INSTALL_DIR}/bin/curl" >&2
  exit 1
fi

mkdir -p "${RUNTIME_DIR}/curl/bin" "${RUNTIME_DIR}/curl/lib" "${RUNTIME_DIR}/openSSL/lib"

cp -a "${INSTALL_DIR}/bin/curl" "${RUNTIME_DIR}/curl/bin/"
cp -a "${INSTALL_DIR}/lib"/libcurl.so* "${RUNTIME_DIR}/curl/lib/"
cp -a "${OPENSSL_DIR}/lib"/libssl.so* "${RUNTIME_DIR}/openSSL/lib/"
cp -a "${OPENSSL_DIR}/lib"/libcrypto.so* "${RUNTIME_DIR}/openSSL/lib/"

echo "Runtime package generated:"
echo "${RUNTIME_DIR}"
echo
echo "Deploy to target as:"
echo "/usr/app/extraLib/curl/bin/curl"
echo "/usr/app/extraLib/curl/lib/libcurl.so*"
echo "/usr/app/extraLib/openSSL/lib/libssl.so*"
echo "/usr/app/extraLib/openSSL/lib/libcrypto.so*"
