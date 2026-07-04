-- Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
-- Licensed under MIT License

local m = Map("komari-agent-c", translate("Komari Agent (C Language Version) Configuration"), translate("Configuration for Komari Agent (C Language Version) - a lightweight monitoring agent for OpenWrt."))

-- Basic Settings
local s = m:section(TypedSection, "komari-agent-c", translate("Basic Settings"))
s.anonymous = true

local token = s:option(Value, "token", translate("Authentication Token"), translate("Token for authentication with the panel server"))
token.password = true
token.rmempty = false
token.datatype = "minlength(8)"

function token.validate(self, value, section)
    if not value or value == "" then
        return nil, translate("Token is required")
    end
    if string.len(value) < 8 then
        return nil, translate("Token must be at least 8 characters")
    end
    if string.len(value) > 256 then
        return nil, translate("Token must not exceed 256 characters")
    end
    return value
end

local endpoint = s:option(Value, "endpoint", translate("Panel Server URL"), translate("URL of the Komari Monitor panel server (e.g., https://panel.example.com)"))
endpoint.rmempty = false
endpoint.datatype = "url"

function endpoint.validate(self, value, section)
    if not value or value == "" then
        return nil, translate("Endpoint URL is required")
    end
    if not string.match(value, "^https?://") then
        return nil, translate("URL must start with http:// or https://")
    end
    return value
end

local interval = s:option(Value, "interval", translate("Report Interval (seconds)"), translate("Interval between status reports (0.5 - 300 seconds)"))
interval.datatype = "float"
interval.default = "5"

function interval.validate(self, value, section)
    local num = tonumber(value)
    if not num then
        return nil, translate("Must be a number")
    end
    if num < 0.5 then
        return nil, translate("Minimum interval is 0.5 seconds")
    end
    if num > 300 then
        return nil, translate("Maximum interval is 300 seconds")
    end
    return value
end

-- Connection Settings
local s2 = m:section(TypedSection, "komari-agent-c", translate("Connection Settings"))
s2.anonymous = true

local dns = s2:option(Value, "custom_dns", translate("Custom DNS Server"), translate("Custom DNS server for resolving panel hostname (optional)"))
dns.datatype = "ipaddr"

local ignore_cert = s2:option(Flag, "ignore_unsafe_cert", translate("Ignore Certificate Errors"), translate("Ignore SSL certificate validation errors (not recommended for production)"))
ignore_cert.default = 0

local auto_discovery = s2:option(Value, "auto_discovery_key", translate("Auto Discovery Key"), translate("Auto-discovery key for auto-registration to the panel (optional)"))
auto_discovery.password = true
auto_discovery.rmempty = true

local protocol_version = s2:option(ListValue, "protocol_version", translate("Protocol Version"), translate("Protocol version to use (1=legacy, 2=current)"))
protocol_version.default = "2"
protocol_version:value("1", translate("v1 (Legacy)"))
protocol_version:value("2", translate("v2 (Current)"))

local max_retries = s2:option(Value, "max_retries", translate("Maximum Retries"), translate("Maximum number of connection retry attempts"))
max_retries.datatype = "uinteger"
max_retries.default = "5"

function max_retries.validate(self, value, section)
    local num = tonumber(value)
    if not num then
        return nil, translate("Must be a number")
    end
    if num < 1 then
        return nil, string.format(translate("Minimum value is %d"), 1)
    end
    if num > 100 then
        return nil, string.format(translate("Maximum value is %d"), 100)
    end
    return value
end

local reconnect_interval = s2:option(Value, "reconnect_interval", translate("Reconnect Interval (seconds)"), translate("Seconds to wait between reconnection attempts"))
reconnect_interval.datatype = "uinteger"
reconnect_interval.default = "30"

function reconnect_interval.validate(self, value, section)
    local num = tonumber(value)
    if not num then
        return nil, translate("Must be a number")
    end
    if num < 5 then
        return nil, string.format(translate("Minimum value is %d"), 5)
    end
    if num > 600 then
        return nil, string.format(translate("Maximum value is %d"), 600)
    end
    return value
end

local info_interval = s2:option(Value, "info_report_interval", translate("Info Report Interval (minutes)"), translate("Minutes between full system information reports"))
info_interval.datatype = "uinteger"
info_interval.default = "5"

function info_interval.validate(self, value, section)
    local num = tonumber(value)
    if not num then
        return nil, translate("Must be a number")
    end
    if num < 1 then
        return nil, string.format(translate("Minimum value is %d"), 1)
    end
    if num > 60 then
        return nil, string.format(translate("Maximum value is %d"), 60)
    end
    return value
end

-- Language Settings (read-only: follows system language)
local s3 = m:section(TypedSection, "komari-agent-c", translate("Language Settings"))
s3.anonymous = true

local lang_note = s3:option(DummyValue, "_lang_note", translate("Interface Language"))
lang_note.default = translate("Auto (follows system)")
lang_note.description = translate("Interface language follows the system language setting. To switch languages, install the corresponding luci-i18n-komari-agent-c-* package and configure the system language in System → System → Language.")

-- Web SSH Settings
local s4 = m:section(TypedSection, "komari-agent-c", translate("Web SSH Settings"))
s4.anonymous = true

local disable_ssh = s4:option(Flag, "disable_web_ssh", translate("Disable Web SSH"), translate("Disable the Web SSH terminal feature for security"))
disable_ssh.default = 0

-- Network Interface Settings
local s5 = m:section(TypedSection, "komari-agent-c", translate("Network Interface Settings"))
s5.anonymous = true

local include_nics = s5:option(Value, "include_nics", translate("Include Network Interfaces"), translate("Comma-separated list of network interfaces to monitor (empty = all)"))
include_nics.rmempty = true

local exclude_nics = s5:option(Value, "exclude_nics", translate("Exclude Network Interfaces"), translate("Comma-separated list of network interfaces to exclude from monitoring"))
exclude_nics.rmempty = true

-- Disk Settings
local s6 = m:section(TypedSection, "komari-agent-c", translate("Disk Settings"))
s6.anonymous = true

local include_mountpoints = s6:option(Value, "include_mountpoints", translate("Include Mount Points"), translate("Comma-separated list of mount points to monitor (empty = all)"))
include_mountpoints.rmempty = true

-- Traffic Statistics
local s7 = m:section(TypedSection, "komari-agent-c", translate("Traffic Statistics"))
s7.anonymous = true

local month_rotate = s7:option(Value, "month_rotate", translate("Month Rotate Day"), translate("Day of month for traffic statistics rotation (0 = disabled, 1-28)"))
month_rotate.datatype = "uinteger"
month_rotate.default = "1"

function month_rotate.validate(self, value, section)
    local num = tonumber(value)
    if not num then
        return nil, translate("Must be a number")
    end
    if num < 0 then
        return nil, string.format(translate("Minimum value is %d"), 0)
    end
    if num > 28 then
        return nil, string.format(translate("Maximum value is %d"), 28)
    end
    return value
end

-- Custom IP Addresses
local s8 = m:section(TypedSection, "komari-agent-c", translate("Custom IP Addresses"))
s8.anonymous = true

local custom_ipv4 = s8:option(Value, "custom_ipv4", translate("Custom IPv4 Address"), translate("Override detected IPv4 address (optional)"))
custom_ipv4.datatype = "ip4addr"
custom_ipv4.rmempty = true

function custom_ipv4.validate(self, value, section)
    if not value or value == "" then
        return value
    end
    if not string.match(value, "^%d+%.%d+%.%d+%.%d+$") then
        return nil, translate("Invalid IPv4 address format")
    end
    local a, b, c, d = string.match(value, "(%d+)%.(%d+)%.(%d+)%.(%d+)")
    if tonumber(a) > 255 or tonumber(b) > 255 or tonumber(c) > 255 or tonumber(d) > 255 then
        return nil, translate("IPv4 octets must be between 0 and 255")
    end
    return value
end

local custom_ipv6 = s8:option(Value, "custom_ipv6", translate("Custom IPv6 Address"), translate("Override detected IPv6 address (optional)"))
custom_ipv6.datatype = "ip6addr"
custom_ipv6.rmempty = true

function custom_ipv6.validate(self, value, section)
    if not value or value == "" then
        return value
    end
    if not string.match(value, "^[%x:]+$") then
        return nil, translate("Invalid IPv6 address format")
    end
    return value
end

-- Advanced Settings
local s9 = m:section(TypedSection, "komari-agent-c", translate("Advanced Settings"))
s9.anonymous = true

local enable_gpu = s9:option(Flag, "enable_gpu", translate("Enable GPU Monitoring"), translate("Enable GPU usage monitoring (if supported by hardware)"))
enable_gpu.default = 0

local disable_compression = s9:option(Flag, "disable_compression", translate("Disable Compression"), translate("Disable data compression for protocol communication"))
disable_compression.default = 0

local disable_auto_update = s9:option(Flag, "disable_auto_update", translate("Disable Auto Update Check"), translate("Disable automatic update checking on startup"))
disable_auto_update.default = 0

return m
