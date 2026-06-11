# 顶层 Makefile：配置并交叉编译 curl/libcurl
# 约定：在远端交叉编译环境中执行

CURL_VER ?= 7.88.1
CURL_TAR ?= curl-$(CURL_VER).tar.xz
CURL_DIR ?= curl-$(CURL_VER)

PREFIX ?= $(CURDIR)/install
CURL_PREFIX ?= $(PREFIX)/curl
TARGET_PREFIX ?= /usr
TARGET_LIBDIR ?= $(TARGET_PREFIX)/lib

# 交叉编译工具链前缀
CROSS_COMPILE ?= arm-linux-gnueabihf-
EXTRA_LIB_PATH ?= $(CURDIR)/..

# BY ZF: 兼容 build <目标设备> <Makefile.cross> 注入的平台变量。
PLATFORM ?= $(TARGET_DEVICE)
ifeq ($(strip $(PLATFORM)),)
ifneq (,$(findstring arm-none-linux-gnueabi,$(CROSS_COMPILE)))
PLATFORM := jzq
else ifneq (,$(findstring arm-buildroot-linux-gnueabihf,$(CROSS_COMPILE)))
PLATFORM := jzq
else ifneq (,$(findstring arm-nuvoton-linux-uclibceabi,$(CROSS_COMPILE)))
PLATFORM := nuc980
else ifneq (,$(findstring arm-linux-gnueabihf,$(CROSS_COMPILE)))
PLATFORM := imx6ul
endif
endif

ifeq ($(strip $(OPENSSL_PREFIX)),)
ifeq ($(PLATFORM),jzq)
OPENSSL_PREFIX := $(EXTRA_LIB_PATH)/openSSL/install-jzq
else ifeq ($(PLATFORM),nuc980)
OPENSSL_PREFIX := $(EXTRA_LIB_PATH)/openSSL/install-nuc980
else
OPENSSL_PREFIX := $(EXTRA_LIB_PATH)/openSSL/install-arm
endif
endif

CROSS_NAME := $(notdir $(CROSS_COMPILE))
CROSS_HOST := $(patsubst %-,%,$(CROSS_NAME))
HOST ?= $(CROSS_HOST)
ifeq ($(strip $(HOST)),)
HOST := $(shell $(CC) -dumpmachine 2>/dev/null || echo arm-linux-gnueabihf)
endif

ifeq ($(strip $(CROSS_COMPILE)),)
ifeq ($(origin CC),default)
CC := gcc
endif
ifeq ($(origin AR),default)
AR := ar
endif
ifeq ($(origin RANLIB),default)
RANLIB := ranlib
endif
ifeq ($(origin STRIP),default)
STRIP := strip
endif
else
# BY ZF: 默认使用 CROSS_COMPILE，也允许 build 脚本通过环境变量覆盖 CC/AR/RANLIB/STRIP。
ifeq ($(origin CC),default)
CC := $(CROSS_COMPILE)gcc
endif
ifeq ($(origin AR),default)
AR := $(CROSS_COMPILE)ar
endif
ifeq ($(origin RANLIB),default)
RANLIB := $(CROSS_COMPILE)ranlib
endif
ifeq ($(origin STRIP),default)
STRIP := $(CROSS_COMPILE)strip
endif
endif

ifeq ($(strip $(CC)),)
CC := $(if $(CROSS_COMPILE),$(CROSS_COMPILE)gcc,gcc)
endif
ifeq ($(strip $(AR)),)
AR := $(if $(CROSS_COMPILE),$(CROSS_COMPILE)ar,ar)
endif
ifeq ($(strip $(RANLIB)),)
RANLIB := $(if $(CROSS_COMPILE),$(CROSS_COMPILE)ranlib,ranlib)
endif
ifeq ($(strip $(STRIP)),)
STRIP := $(if $(CROSS_COMPILE),$(CROSS_COMPILE)strip,strip)
endif

CURL_CONFIGURE_OPTIONS = \
	--host=$(HOST) \
	--prefix=$(TARGET_PREFIX) \
	--with-openssl=$(OPENSSL_PREFIX) \
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

.PHONY: all prepare extract-curl configure build install runtime clean distclean

all: prepare build install runtime

prepare:
	@mkdir -p $(PREFIX)
	@test -d "$(OPENSSL_PREFIX)" || (echo "缺少 OpenSSL 安装目录: $(OPENSSL_PREFIX)"; exit 1)

extract-curl:
	@test -f "$(CURL_TAR)" || (echo "缺少 $(CURL_TAR)，请放入 $(CURDIR)"; exit 1)
	@test -d "$(CURL_DIR)" || tar xf "$(CURL_TAR)"

configure: prepare extract-curl
	cd $(CURL_DIR) && \
		CC=$(CC) AR=$(AR) RANLIB=$(RANLIB) \
		CPPFLAGS="-I$(OPENSSL_PREFIX)/include" \
		LDFLAGS="-L$(OPENSSL_PREFIX)/lib -Wl,-rpath,$(TARGET_LIBDIR) -Wl,-rpath-link,$(OPENSSL_PREFIX)/lib" \
		PKG_CONFIG_PATH="$(OPENSSL_PREFIX)/lib/pkgconfig" \
		./configure $(CURL_CONFIGURE_OPTIONS)

build: configure
	cd $(CURL_DIR) && $(MAKE)

install: build
	cd $(CURL_DIR) && $(MAKE) install DESTDIR=$(CURL_PREFIX)
	-@$(STRIP) $(CURL_PREFIX)$(TARGET_PREFIX)/bin/curl >/dev/null 2>&1 || true
	-@find $(CURL_PREFIX)$(TARGET_LIBDIR) -type f -name 'libcurl.so*' -exec $(STRIP) {} \; >/dev/null 2>&1 || true

runtime: install
	@mkdir -p $(PREFIX)/runtime/curl/bin $(PREFIX)/runtime/curl/lib $(PREFIX)/runtime/openSSL/lib
	@cp -af $(CURL_PREFIX)$(TARGET_PREFIX)/bin/curl $(PREFIX)/runtime/curl/bin/
	@cp -af $(CURL_PREFIX)$(TARGET_LIBDIR)/libcurl.so* $(PREFIX)/runtime/curl/lib/
	@cp -af $(OPENSSL_PREFIX)/lib/libssl.so* $(PREFIX)/runtime/openSSL/lib/
	@cp -af $(OPENSSL_PREFIX)/lib/libcrypto.so* $(PREFIX)/runtime/openSSL/lib/
	@echo "运行时文件已输出到: $(PREFIX)/runtime"

clean:
	-@test ! -d "$(CURL_DIR)" || $(MAKE) -C $(CURL_DIR) clean

distclean:
	-@test ! -d "$(CURL_DIR)" || $(MAKE) -C $(CURL_DIR) distclean
	@rm -rf $(PREFIX)

info:
	@echo "CROSS_COMPILE: $(CROSS_COMPILE)"
	@echo "CC: $(CC)"
	@echo "AR: $(AR)"
	@echo "RANLIB: $(RANLIB)"
	@echo "STRIP: $(STRIP)"
	@echo "HOST: $(HOST)"
	@echo "PLATFORM: $(PLATFORM)"
	@echo "EXTRA_LIB_PATH: $(EXTRA_LIB_PATH)"
	@echo "OPENSSL_PREFIX: $(OPENSSL_PREFIX)"
	@echo "CURL_PREFIX: $(CURL_PREFIX)"
	@echo "TARGET_PREFIX: $(TARGET_PREFIX)"
	@echo "TARGET_LIBDIR: $(TARGET_LIBDIR)"
