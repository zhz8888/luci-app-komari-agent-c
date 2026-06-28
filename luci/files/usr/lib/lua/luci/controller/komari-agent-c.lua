-- Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
-- Licensed under MIT License

module("luci.controller.komari-agent-c", package.seeall)

function index()
    if not nixio.fs.access("/etc/config/komari-agent-c") then
        return
    end

    local page = entry({"admin", "services", "komari-agent-c"},
        alias("admin", "services", "komari-agent-c", "config"),
        _("Komari Agent (C Language Version)"), 60)
    page.dependent = true
    page.acl_depends = { "luci-app-komari-agent-c" }

    entry({"admin", "services", "komari-agent-c", "config"},
        cbi("komari-agent-c/config"),
        _("Configuration"), 10).leaf = true

    entry({"admin", "services", "komari-agent-c", "status"},
        call("action_status"),
        _("Status"), 20).leaf = true

    entry({"admin", "services", "komari-agent-c", "log"},
        call("action_log"),
        _("Log"), 30).leaf = true

    entry({"admin", "services", "komari-agent-c", "api", "status"},
        call("api_get_status")).leaf = true

    entry({"admin", "services", "komari-agent-c", "api", "start"},
        call("api_start_service")).leaf = true

    entry({"admin", "services", "komari-agent-c", "api", "stop"},
        call("api_stop_service")).leaf = true

    entry({"admin", "services", "komari-agent-c", "api", "restart"},
        call("api_restart_service")).leaf = true

    entry({"admin", "services", "komari-agent-c", "api", "test_connection"},
        call("api_test_connection")).leaf = true

    entry({"admin", "services", "komari-agent-c", "api", "log"},
        call("api_get_log")).leaf = true

    entry({"admin", "services", "komari-agent-c", "api", "clear_log"},
        call("api_clear_log")).leaf = true
end

function get_service_status()
    local running = false
    local pid = luci.util.exec("pidof komari-agent-c 2>/dev/null")
    if pid and pid ~= "" then
        running = true
    end
    return running
end

function get_uptime()
    local uptime = luci.util.exec("ubus call system info 2>/dev/null | jsonfilter -e '@.uptime' 2>/dev/null")
    if uptime and uptime ~= "" then
        return math.floor(tonumber(uptime) or 0)
    end
    return 0
end

function action_status()
    local running = get_service_status()
    local status = {
        running = running,
        uptime = get_uptime(),
        version = "1.0.0"
    }

    if running then
        local f = io.open("/tmp/komari-agent-c-status.json", "r")
        if f then
            local content = f:read("*all")
            f:close()
            if content and content ~= "" then
                local json = require("luci.jsonc")
                local data = json.parse(content)
                if data then
                    status.connected = data.connected or false
                    status.endpoint = data.endpoint or ""
                    status.last_update = data.last_update or 0
                    status.cpu_usage = data.cpu_usage or 0
                    status.memory_usage = data.memory_usage or 0
                    status.disk_usage = data.disk_usage or 0
                    status.rx_speed = data.rx_speed or 0
                    status.tx_speed = data.tx_speed or 0
                end
            end
        end
    end

    luci.template.render("komari-agent-c/status", {status = status})
end

function action_log()
    luci.template.render("komari-agent-c/log")
end

function api_get_status()
    local running = get_service_status()
    local response = {
        code = 0,
        running = running,
        uptime = get_uptime()
    }

    if running then
        local f = io.open("/tmp/komari-agent-c-status.json", "r")
        if f then
            local content = f:read("*all")
            f:close()
            if content and content ~= "" then
                local json = require("luci.jsonc")
                local data = json.parse(content)
                if data then
                    response.data = data
                end
            end
        end
    end

    luci.http.prepare_content("application/json")
    luci.http.write_json(response)
end

function api_start_service()
    local result = os.execute("/etc/init.d/komari-agent-c start >/dev/null 2>&1")
    luci.http.prepare_content("application/json")
    luci.http.write_json({
        code = result == 0 and 0 or 1,
        message = result == 0 and "Service started" or "Failed to start service"
    })
end

function api_stop_service()
    local result = os.execute("/etc/init.d/komari-agent-c stop >/dev/null 2>&1")
    luci.http.prepare_content("application/json")
    luci.http.write_json({
        code = result == 0 and 0 or 1,
        message = result == 0 and "Service stopped" or "Failed to stop service"
    })
end

function api_restart_service()
    local result = os.execute("/etc/init.d/komari-agent-c restart >/dev/null 2>&1")
    luci.http.prepare_content("application/json")
    luci.http.write_json({
        code = result == 0 and 0 or 1,
        message = result == 0 and "Service restarted" or "Failed to restart service"
    })
end

function api_test_connection()
    local uci = require("luci.model.uci").cursor()
    local endpoint = uci:get("komari-agent-c", "komari-agent-c", "endpoint") or ""
    local token = uci:get("komari-agent-c", "komari-agent-c", "token") or ""
    local ignore_cert = uci:get("komari-agent-c", "komari-agent-c", "ignore_unsafe_cert") or "0"

    if endpoint == "" then
        luci.http.prepare_content("application/json")
        luci.http.write_json({
            code = 1,
            message = "Endpoint not configured"
        })
        return
    end

    local curl_opts = ""
    if ignore_cert == "1" then
        curl_opts = "-k"
    end

    local cmd = string.format(
        "curl -s -o /dev/null -w '%%{http_code}' --connect-timeout 5 %s '%s' 2>/dev/null",
        curl_opts, endpoint
    )

    local http_code = luci.util.exec(cmd)
    http_code = tonumber(http_code) or 0

    luci.http.prepare_content("application/json")
    if http_code > 0 then
        luci.http.write_json({
            code = 0,
            message = "Connection successful (HTTP " .. http_code .. ")",
            http_code = http_code
        })
    else
        luci.http.write_json({
            code = 1,
            message = "Connection failed"
        })
    end
end

function api_get_log()
    local lines = tonumber(luci.http.formvalue("lines")) or 100
    local log_content = luci.util.exec(
        string.format("logread -e komari-agent-c | tail -n %d 2>/dev/null", lines)
    )

    luci.http.prepare_content("application/json")
    luci.http.write_json({
        code = 0,
        log = log_content or ""
    })
end

function api_clear_log()
    local result = os.execute("logrotate -f /etc/logrotate.d/komari-agent-c 2>/dev/null || true")

    luci.http.prepare_content("application/json")
    luci.http.write_json({
        code = 0,
        message = "Log cleared"
    })
end
