-- vendor/premake5.lua

-- GLFW
project "glfw"
    kind "StaticLib"
    language "C"
    
    -- Common files for all platforms
    files {
        "glfw/include/GLFW/glfw3.h",
        "glfw/include/GLFW/glfw3native.h",
        
        -- Core source files
        "glfw/src/context.c",
        "glfw/src/init.c",
        "glfw/src/input.c",
        "glfw/src/monitor.c",
        "glfw/src/platform.c",
        "glfw/src/vulkan.c",
        "glfw/src/window.c",
        
        -- Platform abstraction layer
        "glfw/src/internal.h",
        
        -- Null backend (fallback)
        "glfw/src/null_*.c",
        "glfw/src/null_*.h"
    }
    
    includedirs {
        "glfw/include",
        "glfw/src"  -- Add src directory for internal headers
    }
    
    -- Windows-specific files
    filter "system:windows"
        files {
            -- Win32 platform implementation
            "glfw/src/win32_*.c",
            "glfw/src/win32_*.h",
            "glfw/src/wgl_context.c",
            "glfw/src/egl_context.c",
            "glfw/src/osmesa_context.c"
        }
        defines { 
            "_GLFW_WIN32",
            "_CRT_SECURE_NO_WARNINGS"
        }
    
    -- Linux-specific files
    filter "system:linux"
        files {
            -- X11 platform implementation
            "glfw/src/x11_*.c",
            "glfw/src/x11_*.h",
            "glfw/src/xkb_*.c",
            "glfw/src/xkb_*.h",
            "glfw/src/posix_*.c",
            "glfw/src/posix_*.h",
            "glfw/src/linux_*.c",
            "glfw/src/linux_*.h",
            "glfw/src/glx_context.c",
            "glfw/src/egl_context.c",
            "glfw/src/osmesa_context.c"
        }
        defines { "_GLFW_X11" }
    
    -- macOS-specific files
    filter "system:macosx"
        files {
            -- Cocoa platform implementation
            "glfw/src/cocoa_*.m",
            "glfw/src/cocoa_*.c",
            "glfw/src/cocoa_*.h",
            "glfw/src/posix_*.c",
            "glfw/src/posix_*.h",
            "glfw/src/nsgl_context.m",
            "glfw/src/egl_context.c",
            "glfw/src/osmesa_context.c"
        }
        defines { "_GLFW_COCOA" }
    
    filter {}

-- tinyobjloader
project "tinyobjloader"
    kind "StaticLib"
    language "C++"
    cppdialect "C++11"
    
    files {
        "tinyobjloader/tiny_obj_loader.h",
        "tinyobjloader/tiny_obj_loader.cc"
    }
    
    includedirs {
        "tinyobjloader"
    }