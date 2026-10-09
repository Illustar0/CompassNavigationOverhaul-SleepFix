set_xmakever("3.0.0")
set_project("CompassNavigationOverhaul-SleepFix")
set_version("0.1.0")
set_license("GPL-3.0-or-later")

set_allowedplats("windows")
set_allowedarchs("x64")
set_languages("c++23")
set_encodings("utf-8")
set_warnings("allextra")
add_rules("mode.debug", "mode.releasedbg")
set_defaultmode("releasedbg")
set_runtimes(is_mode("debug") and "MDd" or "MD")

set_config("skyrim_se", true)
set_config("skyrim_ae", true)
set_config("skyrim_vr", false)
set_config("tests", false)

includes("lib/commonlibsse-ng")

target("CompassNavigationOverhaul-SleepFix")
    set_kind("shared")
    set_default(true)
    set_basename("!CompassNavigationOverhaul-SleepFix")
    add_deps("commonlibsse-ng")
    add_rules("commonlibsse-ng.plugin", {
        name = "CompassNavigationOverhaul-SleepFix",
        description = "Startup sleep fix for Compass Navigation Overhaul",
        options = { signature_scanning = true }
    })
    add_defines("WIN32_LEAN_AND_MEAN", "NOMINMAX", "_WIN32_WINNT=0x0A00")
    add_syslinks("bcrypt")
    add_files("src/*.cpp")
    set_installdir("build/package")
