# Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
# Licensed under MIT License
#
# Third-party dependency discovery and validation.
# Included from the top-level CMakeLists.txt AFTER Platform.cmake so
# KOMARI_TARGET_OPENWRT / KOMARI_CROSSCOMPILING are visible.

include_guard(GLOBAL)

# ---------------------------------------------------------------------------
# Required dependencies
# ---------------------------------------------------------------------------
# OpenSSL is used for TLS (websocket.c) and crypto primitives. Require at
# least 1.1.0 so SSL_CTX_new / TLS_method() are available. OpenSSL 3.x is
# the default on Ubuntu 24.04 / Debian bookworm / OpenWrt master.
find_package(OpenSSL 1.1.0 REQUIRED)
message(STATUS "OpenSSL: ${OPENSSL_VERSION} (include: ${OPENSSL_INCLUDE_DIR})")

# Threading — every target architecture provides pthreads.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
message(STATUS "Threads: ${CMAKE_THREAD_LIBS_INIT}")

# zlib is used by the protocol/ compress.c path.
find_package(ZLIB 1.2.11 REQUIRED)
message(STATUS "ZLIB: ${ZLIB_VERSION_STRING}")

# ---------------------------------------------------------------------------
# Optional: ccache integration
# ---------------------------------------------------------------------------
if(KOMARI_ENABLE_CCACHE)
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        mark_as_advanced(CCACHE_PROGRAM)
        set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE STRING "ccache launcher" FORCE)
        message(STATUS "ccache enabled: ${CCACHE_PROGRAM}")
        if(KOMARI_VERBOSE_CMAKE)
            execute_process(
                COMMAND ${CCACHE_PROGRAM} --version
                OUTPUT_VARIABLE _ccache_version
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            message(STATUS "  ${_ccache_version}")
        endif()
    else()
        message(STATUS "ccache requested but not found — building without cache")
    endif()
endif()

# ---------------------------------------------------------------------------
# Optional: pkg-config for cross-compilation hint discovery
# ---------------------------------------------------------------------------
# On Debian/Ubuntu multiarch hosts, libssl-dev:$arch installs .pc files
# under /usr/lib/<triplet>/pkgconfig. The Docker build.sh script sets
# PKG_CONFIG_LIBDIR explicitly; here we only attempt auto-detection when
# cross-compiling without that env var.
if(KOMARI_CROSSCOMPILING AND NOT DEFINED ENV{PKG_CONFIG_LIBDIR})
    if(CMAKE_LIBRARY_ARCHITECTURE)
        set(ENV{PKG_CONFIG_LIBDIR}
            "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/pkgconfig:/usr/share/pkgconfig")
        message(STATUS "PKG_CONFIG_LIBDIR auto-set to: $ENV{PKG_CONFIG_LIBDIR}")
    endif()
endif()

# ---------------------------------------------------------------------------
# Optional: forkpty / util library (Linux usually has it in libc; OpenWrt
# needs libutil)
# ---------------------------------------------------------------------------
# The terminal/terminal.c module calls forkpty, which historically lives in
# libutil on glibc < 2.34 and in libc on glibc >= 2.34. Detect it here so
# the executable links the right library on every target.
include(CheckLibraryExists)
check_library_exists("util" forkpty "" KOMARI_HAVE_FORKPTY_IN_UTIL)
if(NOT KOMARI_HAVE_FORKPTY_IN_UTIL)
    check_library_exists("" forkpty "" KOMARI_HAVE_FORKPTY_IN_LIBC)
endif()

if(KOMARI_HAVE_FORKPTY_IN_UTIL)
    set(KOMARI_UTIL_LIBRARY util)
    message(STATUS "forkpty: found in libutil")
elseif(KOMARI_HAVE_FORKPTY_IN_LIBC)
    set(KOMARI_UTIL_LIBRARY "")
    message(STATUS "forkpty: found in libc")
else()
    set(KOMARI_UTIL_LIBRARY "")
    message(WARNING "forkpty not found — terminal feature may fail to link")
endif()

# ---------------------------------------------------------------------------
# Optional: resolv library (glibc < 2.34 ships res_* in libresolv; modern
# glibc has res_init in libc but ns_initparse/ns_parserr may still need
# libresolv). Check for the actual symbols used by dns_resolver.c.
# ---------------------------------------------------------------------------
check_library_exists("resolv" ns_initparse "" KOMARI_HAVE_RESOLV_IN_LIBRESOLV)
if(KOMARI_HAVE_RESOLV_IN_LIBRESOLV)
    set(KOMARI_RESOLV_LIBRARY resolv)
    message(STATUS "DNS resolver functions (ns_initparse/ns_parserr): found in libresolv")
else()
    check_library_exists("" ns_initparse "" KOMARI_HAVE_RESOLV_IN_LIBC)
    if(KOMARI_HAVE_RESOLV_IN_LIBC)
        set(KOMARI_RESOLV_LIBRARY "")
        message(STATUS "DNS resolver functions (ns_initparse/ns_parserr): found in libc")
    else()
        set(KOMARI_RESOLV_LIBRARY "")
        message(WARNING
            "ns_initparse not found in libc or libresolv — "
            "dns_resolver.c may fail to link")
    endif()
endif()

# ---------------------------------------------------------------------------
# Optional: rt library (clock_gettime on older glibc)
# ---------------------------------------------------------------------------
check_library_exists("rt" clock_gettime "" KOMARI_HAVE_RT)
if(KOMARI_HAVE_RT)
    set(KOMARI_RT_LIBRARY rt)
else()
    set(KOMARI_RT_LIBRARY "")
endif()

# ---------------------------------------------------------------------------
# Expose the discovered libraries as a single list for targets to use.
# Targets should link against KOMARI_SYSTEM_LIBS instead of naming each
# system library explicitly.
# ---------------------------------------------------------------------------
set(KOMARI_SYSTEM_LIBS
    ${KOMARI_UTIL_LIBRARY}
    ${KOMARI_RESOLV_LIBRARY}
    ${KOMARI_RT_LIBRARY}
    m
)

if(KOMARI_VERBOSE_CMAKE)
    message(STATUS "  KOMARI_SYSTEM_LIBS = ${KOMARI_SYSTEM_LIBS}")
endif()

mark_as_advanced(
    KOMARI_HAVE_FORKPTY_IN_UTIL
    KOMARI_HAVE_FORKPTY_IN_LIBC
    KOMARI_HAVE_RESOLV_IN_LIBRESOLV
    KOMARI_HAVE_RESOLV_IN_LIBC
    KOMARI_HAVE_RT
    KOMARI_UTIL_LIBRARY
    KOMARI_RESOLV_LIBRARY
    KOMARI_RT_LIBRARY
)
