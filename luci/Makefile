# Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
# Licensed under MIT License

include $(TOPDIR)/rules.mk

LUCI_TITLE:=LuCI support for Komari Agent (C Language Version)
LUCI_DEPENDS:=+komari-agent-c +luci-compat
LUCI_PKGARCH:=all

PKG_NAME:=luci-app-komari-agent-c
PKG_VERSION:=1.0.1
PKG_RELEASE:=1

PKG_MAINTAINER:=zhz8888
PKG_LICENSE:=MIT

include $(TOPDIR)/feeds/luci/luci.mk

define Package/luci-app-komari-agent-c
  SECTION:=luci
  CATEGORY:=LuCI
  SUBMENU:=3. Applications
  TITLE:=$(LUCI_TITLE)
  DEPENDS:=$(LUCI_DEPENDS)
  PKGARCH:=$(LUCI_PKGARCH)
endef

define Package/luci-app-komari-agent-c/description
  LuCI web interface for Komari Agent (C Language Version) configuration and monitoring.
  
  Features:
  - Configuration management with validation
  - Real-time status monitoring
  - Log viewer with auto-refresh
  - Connection testing
  - Multi-language support (English, Chinese)
endef

define Build/Compile
endef

define Package/luci-app-komari-agent-c/install
	$(INSTALL_DIR) $(1)/usr/lib/lua/luci/controller
	$(INSTALL_DIR) $(1)/usr/lib/lua/luci/model/cbi/komari-agent-c
	$(INSTALL_DIR) $(1)/usr/lib/lua/luci/view/komari-agent-c
	$(INSTALL_DIR) $(1)/usr/share/luci/menu.d
	$(INSTALL_DIR) $(1)/usr/share/rpcd/acl.d
	$(INSTALL_DIR) $(1)/usr/share/rpcd/acl.d
	
	$(INSTALL_DATA) ./files/usr/lib/lua/luci/controller/komari-agent-c.lua \
		$(1)/usr/lib/lua/luci/controller/
	$(INSTALL_DATA) ./files/usr/lib/lua/luci/model/cbi/komari-agent-c/config.lua \
		$(1)/usr/lib/lua/luci/model/cbi/komari-agent-c/
	$(INSTALL_DATA) ./files/usr/lib/lua/luci/view/komari-agent-c/*.htm \
		$(1)/usr/lib/lua/luci/view/komari-agent-c/
	$(INSTALL_DATA) ./files/usr/share/luci/menu.d/*.json \
		$(1)/usr/share/luci/menu.d/
	$(INSTALL_DATA) ./files/usr/share/rpcd/acl.d/*.json \
		$(1)/usr/share/rpcd/acl.d/
endef

define Package/luci-app-komari-agent-c/postinst
#!/bin/sh
[ -n "$${IPKG_INSTROOT}" ] || {
	( . /etc/uci-defaults/luci-app-komari-agent-c ) && rm -f /etc/uci-defaults/luci-app-komari-agent-c
	rm -f /tmp/luci-indexcache
	rm -f /tmp/luci-modulecache/*
	exit 0
}
endef

$(eval $(call BuildPackage,luci-app-komari-agent-c))
