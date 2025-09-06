-- source/premake5.lua
project "Pathtracer"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++17"
    
    files {
        "source/**.h",
        "source/**.cpp",
        "source/**.hpp"
    }
    
    includedirs {
        "source",
        "../extern/source/glfw/include",
        "../extern/source/tinyobjloader",
        "../extern/source/stb",
        "../extern/source",
        os.getenv("VULKAN_SDK") and (os.getenv("VULKAN_SDK") .. "/Include") or "",
        "."
    }
    
    links {
        "glfw",
        "tinyobjloader"
    }
    
    filter "system:windows"
        links {
            "opengl32",
            "gdi32"
        }
        libdirs { os.getenv("VULKAN_SDK") .. "/Lib" }
        links { "vulkan-1" }
        systemversion "latest"
    
    filter "system:linux"
        links {
            "GL",
            "X11",
            "pthread",
            "dl",
            "vulkan"
        }
    
    filter "system:macosx"
        links {
            "Cocoa",
            "IOKit",
            "CoreFoundation",
            "OpenGL.framework",
            "vulkan"
        }