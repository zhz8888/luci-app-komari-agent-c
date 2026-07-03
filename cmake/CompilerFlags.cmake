# Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
# Licensed under MIT License
#
# Centralized compiler flag configuration for komari-agent-c.
# Included from the top-level CMakeLists.txt AFTER Platform.cmake so
# that KOMARI_COMPILER_GCC_LIKE / KOMARI_TARGET_* flags are visible.

include_guard(GLOBAL)

include(CheckCCompilerFlag)
include(CMakePushCheckState)

# ---------------------------------------------------------------------------
# Helper: try to add a compile flag, fall back gracefully if unsupported.
# ---------------------------------------------------------------------------
function(komari_add_c_flag FLAG DESCRIPTION)
    cmake_push_check_state(RESET)
    set(CMAKE_REQUIRED_QUIET ON)
    check_c_compiler_flag("${FLAG}" _komari_flag_ok)
    cmake_pop_check_state()
    if(_komari_flag_ok)
        add_compile_options(${FLAG})
        if(KOMARI_VERBOSE_CMAKE)
            message(STATUS "  enabled: ${DESCRIPTION} (${FLAG})")
        endif()
    else()
        if(KOMARI_VERBOSE_CMAKE)
            message(STATUS "  skipped: ${DESCRIPTION} (${FLAG} not supported)")
        endif()
    endif()
    unset(_komari_flag_ok CACHE)
endfunction()

# ---------------------------------------------------------------------------
# Base warning & language flags
# ---------------------------------------------------------------------------
# These are required by the project regardless of the compiler family.
add_compile_options(
    -Wall
    -Wextra
)

if(KOMARI_COMPILER_GCC_LIKE)
    # Additional warnings useful for C99 projects
    komari_add_c_flag(-Wformat=2            "Improved printf/scanf format checks")
    komari_add_c_flag(-Wconversion          "Warn on implicit type conversions")
    komari_add_c_flag(-Wshadow              "Warn on variable shadowing")
    komari_add_c_flag(-Wstrict-prototypes   "Warn on non-prototyped function decls")
    komari_add_c_flag(-Wmissing-prototypes  "Warn on missing prototypes")
    komari_add_c_flag(-Wwrite-strings       "Treat string literals as const char*")
    komari_add_c_flag(-Wundef               "Warn on undefined macro in #if")

    # Feature macros required across the codebase
    add_compile_options(
        -D_GNU_SOURCE
        -D_POSIX_C_SOURCE=200809L
        -D_DEFAULT_SOURCE
    )

    if(KOMARI_WERROR)
        add_compile_options(-Werror)
    endif()
else()
    # Unknown compiler — just add basic flags we know are portable.
    add_compile_options(-D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE)
endif()

# ---------------------------------------------------------------------------
# Hardening flags (Linux/OpenWrt only; macOS not supported for full build)
# ---------------------------------------------------------------------------
if(KOMARI_TARGET_LINUX OR KOMARI_TARGET_OPENWRT)
    if(KOMARI_HARDEN_STACK)
        komari_add_c_flag(-fstack-protector-strong "Stack smashing protection")
    endif()

    if(KOMARI_HARDEN_VISIBILITY)
        komari_add_c_flag(-fvisibility=hidden "Hide non-exported symbols")
    endif()

    # _FORTIFY_SOURCE requires optimization, so only enable for -O1+
    if(KOMARI_HARDEN_FORTIFY AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        # Skip on musl/OpenWrt where _FORTIFY_SOURCE support varies
        if(NOT KOMARI_TARGET_OPENWRT)
            komari_add_c_flag(-D_FORTIFY_SOURCE=2 "Run-time buffer overflow detection")
        endif()
    endif()

    if(KOMARI_HARDEN_FORMAT)
        komari_add_c_flag(-Wformat        "Warn on bad format strings")
        komari_add_c_flag(-Werror=format-security "Treat format string bugs as errors")
    endif()
endif()

# ---------------------------------------------------------------------------
# Position-Independent Executable
# ---------------------------------------------------------------------------
# CMP0083 (NEW) is required for check_pie_supported() to work. CMake < 3.9
# does not have this policy; fall back to manual -pie linker flag there.
if(KOMARI_ENABLE_PIE AND (KOMARI_TARGET_LINUX OR KOMARI_TARGET_OPENWRT))
    komari_add_c_flag(-fPIE "Generate position-independent code (executables)")
    if(POLICY CMP0083)
        cmake_policy(SET CMP0083 NEW)
        include(CheckPIESupported OPTIONAL RESULT_VARIABLE _check_pie_supported_module)
        if(_check_pie_supported_module)
            check_pie_supported(LANGUAGES C)
            if(CMAKE_C_LINK_PIE_SUPPORTED)
                set(CMAKE_POSITION_INDEPENDENT_CODE ON)
            else()
                # PIE not supported by linker — add -pie manually as fallback.
                komari_add_c_flag(-pie "Link as position-independent executable")
            endif()
        endif()
    else()
        # CMake < 3.9 fallback: set linker flags directly.
        komari_add_c_flag(-pie "Link as position-independent executable")
    endif()
endif()

# ---------------------------------------------------------------------------
# Build-type specific flags
# ---------------------------------------------------------------------------
set(CMAKE_C_FLAGS_DEBUG            "-O0 -g3 -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE          "-O2 -DNDEBUG")
set(CMAKE_C_FLAGS_RELWITHDEBINFO   "-O2 -g3 -DNDEBUG")
set(CMAKE_C_FLAGS_MINSIZEREL       "-Os -DNDEBUG")

# Custom build types
set(CMAKE_C_FLAGS_COVERAGE         "-O0 -g3 --coverage -DDEBUG")
set(CMAKE_C_FLAGS_SANITIZE         "-O0 -g3 -fsanitize=address,undefined -fno-omit-frame-pointer -DDEBUG")

# Linker flags must mirror the compile flags for coverage/sanitizers
set(CMAKE_EXE_LINKER_FLAGS_COVERAGE  "--coverage")
set(CMAKE_EXE_LINKER_FLAGS_SANITIZE  "-fsanitize=address,undefined")

# Mark these as advanced — users rarely need to edit them directly
mark_as_advanced(
    CMAKE_C_FLAGS_DEBUG
    CMAKE_C_FLAGS_RELEASE
    CMAKE_C_FLAGS_RELWITHDEBINFO
    CMAKE_C_FLAGS_MINSIZEREL
    CMAKE_C_FLAGS_COVERAGE
    CMAKE_C_FLAGS_SANITIZE
    CMAKE_EXE_LINKER_FLAGS_COVERAGE
    CMAKE_EXE_LINKER_FLAGS_SANITIZE
)

# ---------------------------------------------------------------------------
# Sanitizer support (standalone switch)
# ---------------------------------------------------------------------------
if(KOMARI_ENABLE_SANITIZERS AND KOMARI_COMPILER_GCC_LIKE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
    message(STATUS "Sanitizers enabled: AddressSanitizer + UndefinedBehaviorSanitizer")
endif()

# ---------------------------------------------------------------------------
# Coverage instrumentation
# ---------------------------------------------------------------------------
if(KOMARI_ENABLE_COVERAGE AND KOMARI_COMPILER_GCC_LIKE)
    add_compile_options(--coverage)
    add_link_options(--coverage)
    message(STATUS "Code coverage instrumentation enabled")
endif()

# ---------------------------------------------------------------------------
# Link-Time Optimization (LTO)
# ---------------------------------------------------------------------------
if(KOMARI_ENABLE_LTO)
    include(CheckIPOSupported OPTIONAL RESULT_VARIABLE _check_ipo_module)
    if(_check_ipo_module)
        check_ipo_supported(RESULT _ipo_supported OUTPUT _ipo_output)
        if(_ipo_supported)
            set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
            message(STATUS "Link-Time Optimization enabled")
        else()
            message(WARNING "LTO requested but not supported: ${_ipo_output}")
        endif()
    else()
        # CMake < 3.9 fallback
        if(KOMARI_COMPILER_GCC_LIKE)
            komari_add_c_flag(-flto "Link-Time Optimization (manual)")
            add_link_options(-flto)
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# Disable clang-format / clang-tidy when cross-compiling — they may not be
# available for the target architecture.
# ---------------------------------------------------------------------------
if(KOMARI_CROSSCOMPILING)
    set(CMAKE_C_CLANG_TIDY "")
    set(CMAKE_C_CPPCHECK "")
endif()
