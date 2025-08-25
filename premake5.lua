-- Root premake5.lua
workspace "PathtracingVulkan"
    configurations { "Debug", "Release" }
    platforms { "x64", "x86" }
    
    -- Common settings for all projects
    filter "configurations:Debug"
        defines { "DEBUG" }
        symbols "On"
        optimize "Off"

    filter "configurations:Release"
        defines { "NDEBUG" }
        optimize "Speed"
        symbols "Off"

    filter {}

    -- Include subdirectories
    include "extern/premake5.lua"
    include "pathtracer/premake5.lua"