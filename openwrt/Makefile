# Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
# Licensed under MIT License

include $(TOPDIR)/rules.mk

PKG_NAME:=komari-agent-c
PKG_VERSION:=1.0.0
PKG_RELEASE:=1

PKG_MAINTAINER:=zhz8888
PKG_LICENSE:=MIT

PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/zhz8888/luci-app-komari-agent-c.git
PKG_SOURCE_VERSION:=v1.0.0

include $(INCLUDE_DIR)/package.mk

# ---------------------------------------------------------------------------
# Build profile and subdirectory configuration
# ---------------------------------------------------------------------------
# KOMARI_BUILD_SUBDIR separates the OpenWrt CMake build tree from the
# standalone binary build tree (which uses "build/"). This avoids any
# path collision when the same source tree is used for both build modes
# and makes the OpenWrt build artifacts easy to identify.
#
# KOMARI_BUILD_PROFILE=openwrt tells the CMake configuration to disable
# tests/CPack and apply musl-friendly hardening defaults.
KOMARI_BUILD_SUBDIR ?= build-openwrt
KOMARI_BUILD_PROFILE ?= openwrt

define Package/komari-agent-c
	SECTION:=net
	CATEGORY:=Network
	SUBMENU:=Monitoring
	TITLE:=Komari Monitor Agent for OpenWrt
	DEPENDS:=+libpthread +libopenssl +librt +zlib
endef

define Package/komari-agent-c/description
	Komari Agent is a lightweight monitoring agent designed for OpenWrt.
	It collects system metrics (CPU, memory, disk, network) and reports
	to a central Komari Monitor dashboard via WebSocket.

	Features:
	- Real-time system monitoring
	- Web SSH terminal support
	- Monthly traffic statistics
	- Auto-discovery registration
	- UCI configuration support
	- Multi-language support (English, Chinese)
endef

define Package/komari-agent-c/conffiles
/etc/config/komari-agent-c
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)/src
	mkdir -p $(PKG_BUILD_DIR)/include
	mkdir -p $(PKG_BUILD_DIR)/cmake
	$(CP) ./src/* $(PKG_BUILD_DIR)/src/
	$(CP) ./include/* $(PKG_BUILD_DIR)/include/
	$(CP) ./CMakeLists.txt $(PKG_BUILD_DIR)/
	$(CP) ./cmake/* $(PKG_BUILD_DIR)/cmake/
endef

define Build/Compile
	cd $(PKG_BUILD_DIR) && cmake \
		-DCMAKE_TOOLCHAIN_FILE=$(PKG_BUILD_DIR)/cmake/toolchain-openwrt.cmake \
		-DCMAKE_BUILD_TYPE=Release \
		-DKOMARI_BUILD_PROFILE=$(KOMARI_BUILD_PROFILE) \
		-DKOMARI_BUILD_TESTS=OFF \
		-DKOMARI_ENABLE_CPACK=OFF \
		-B $(KOMARI_BUILD_SUBDIR)
	cmake --build $(PKG_BUILD_DIR)/$(KOMARI_BUILD_SUBDIR)
endef

define Package/komari-agent-c/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/$(KOMARI_BUILD_SUBDIR)/bin/komari-agent-c $(1)/usr/bin/

	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/komari-agent-c.init $(1)/etc/init.d/komari-agent-c

	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/komari-agent-c.config $(1)/etc/config/komari-agent-c
endef

define Package/komari-agent-c/postinst
#!/bin/sh
[ -n "$${IPKG_INSTROOT}" ] || {
	/etc/init.d/komari-agent-c enable
}
exit 0
endef

define Package/komari-agent-c/prerm
#!/bin/sh
[ -n "$${IPKG_INSTROOT}" ] || {
	/etc/init.d/komari-agent-c stop
	/etc/init.d/komari-agent-c disable
}
exit 0
endef

$(eval $(call BuildPackage,komari-agent-c))
