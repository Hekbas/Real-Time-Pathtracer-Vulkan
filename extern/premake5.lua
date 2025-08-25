-- vendor/premake5.lua

-- GLFW
project "glfw"
    kind "StaticLib"
    language "C"
    
    -- Common files for all platforms
    files {
        "source/glfw/include/GLFW/glfw3.h",
        "source/glfw/include/GLFW/glfw3native.h",
        
        -- Core source files
        "source/glfw/src/context.c",
        "source/glfw/src/init.c",
        "source/glfw/src/input.c",
        "source/glfw/src/monitor.c",
        "source/glfw/src/platform.c",
        "source/glfw/src/vulkan.c",
        "source/glfw/src/window.c",
        
        -- Platform abstraction layer
        "source/glfw/src/internal.h",
        
        -- Null backend (fallback)
        "source/glfw/src/null_*.c",
        "source/glfw/src/null_*.h"
    }
    
    includedirs {
        "source/glfw/include",
        "source/glfw/src"  -- Add src directory for internal headers
    }
    
    -- Windows-specific files
    filter "system:windows"
        files {
            -- Win32 platform implementation
            "source/glfw/src/win32_*.c",
            "source/glfw/src/win32_*.h",
            "source/glfw/src/wgl_context.c",
            "source/glfw/src/egl_context.c",
            "source/glfw/src/osmesa_context.c"
        }
        defines { 
            "_GLFW_WIN32",
            "_CRT_SECURE_NO_WARNINGS"
        }
    
    -- Linux-specific files
    filter "system:linux"
        files {
            -- X11 platform implementation
            "source/glfw/src/x11_*.c",
            "source/glfw/src/x11_*.h",
            "source/glfw/src/xkb_*.c",
            "source/glfw/src/xkb_*.h",
            "source/glfw/src/posix_*.c",
            "source/glfw/src/posix_*.h",
            "source/glfw/src/linux_*.c",
            "source/glfw/src/linux_*.h",
            "source/glfw/src/glx_context.c",
            "source/glfw/src/egl_context.c",
            "source/glfw/src/osmesa_context.c"
        }
        defines { "_GLFW_X11" }
    
    -- macOS-specific files
    filter "system:macosx"
        files {
            -- Cocoa platform implementation
            "source/glfw/src/cocoa_*.m",
            "source/glfw/src/cocoa_*.c",
            "source/glfw/src/cocoa_*.h",
            "source/glfw/src/posix_*.c",
            "source/glfw/src/posix_*.h",
            "source/glfw/src/nsgl_context.m",
            "source/glfw/src/egl_context.c",
            "source/glfw/src/osmesa_context.c"
        }
        defines { "_GLFW_COCOA" }
    
    filter {}

-- tinyobjloader
project "tinyobjloader"
    kind "StaticLib"
    language "C++"
    cppdialect "C++11"
    
    files {
        "source/tinyobjloader/tiny_obj_loader.h",
        "source/tinyobjloader/tiny_obj_loader.cc"
    }
    
    includedirs {
        "source/tinyobjloader"
    }