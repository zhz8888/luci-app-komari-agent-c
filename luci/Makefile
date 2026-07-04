# Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
# Licensed under MIT License

include $(TOPDIR)/rules.mk

LUCI_TITLE:=LuCI support for Komari Agent (C Language Version)
LUCI_DESCRIPTION:=LuCI web interface for Komari Agent configuration and monitoring.
LUCI_DEPENDS:=+komari-agent-c +luci-compat
LUCI_PKGARCH:=all

PKG_NAME:=luci-app-komari-agent-c
PKG_VERSION:=1.0.0
PKG_RELEASE:=1

PKG_MAINTAINER:=zhz8888
PKG_LICENSE:=MIT

include $(TOPDIR)/feeds/luci/luci.mk

define Package/luci-app-komari-agent-c/description
  LuCI web interface for Komari Agent (C Language Version) configuration and monitoring.

  Features:
  - Configuration management with validation
  - Real-time status monitoring
  - Log viewer with auto-refresh
  - Connection testing
  - Multi-language support (English, Chinese)
endef

$(eval $(call BuildPackage,luci-app-komari-agent-c))
