-- source/premake5.lua
project "Pathtracer"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++17"
    
    files {
        "source/**.h",
        "source/**.cpp",
        "source/**.hpp",
        "../extern/source/tinygltf/tiny_gltf.h",
        "../extern/source/stb/stb_image.h",
        "../extern/source/stb/stb_image_write.h",
        "../extern/source/json/json.hpp"
    }
    
    includedirs {
        "source",
        "../extern/source/glfw/include",
        "../extern/source/tinyobjloader",
        "../extern/source/tinygltf",
        "../extern/source/stb",
        "../extern/source/json",
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