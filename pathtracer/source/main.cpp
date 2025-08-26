
#include <map>
#include <cmath>
#include <string>
#include <fstream>
#include <iostream>
#include <functional>

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <GLFW/glfw3.h>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

static constexpr int WIDTH = 1280;
static constexpr int HEIGHT = 720;


// ------------------------------------------------------------------
//                              Math 
// ------------------------------------------------------------------
const float PI = 3.14159265359f;

// A simple 3D vector
struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    Vec3 operator+(const Vec3& other) const { return {x + other.x, y + other.y, z + other.z}; }
    Vec3 operator-(const Vec3& other) const { return {x - other.x, y - other.y, z - other.z}; }
    Vec3 operator*(float scalar) const { return {x * scalar, y * scalar, z * scalar}; }
    Vec3 operator/(float scalar) const { return {x / scalar, y / scalar, z / scalar}; }
    
    Vec3& operator+=(const Vec3& other) { x += other.x; y += other.y; z += other.z; return *this; }
    Vec3& operator-=(const Vec3& other) { x -= other.x; y -= other.y; z -= other.z; return *this; }
    Vec3& operator*=(float scalar) { x *= scalar; y *= scalar; z *= scalar; return *this; }
    Vec3& operator/=(float scalar) { x /= scalar; y /= scalar; z /= scalar; return *this; }
    
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3 operator+() const { return *this; }
    
    bool operator==(const Vec3& other) const { return x == other.x && y == other.y && z == other.z; }
    bool operator!=(const Vec3& other) const { return !(*this == other); }
};

inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline float dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 normalize(const Vec3& v) {
    float len = sqrt(dot(v, v));
    if (len > 0.0f) {
        return {v.x / len, v.y / len, v.z / len};
    }
    return {0.0f, 0.0f, 0.0f};
}

// A simple 4x4 Matrix (Column-major)
struct Mat4 {
    float m[4][4];  // m[column][row]

    static Mat4 identity() {
        Mat4 mat = {};
        mat.m[0][0] = mat.m[1][1] = mat.m[2][2] = mat.m[3][3] = 1.0f;
        return mat;
    }

    // Creates a look-at view matrix
    static Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
        Vec3 f = normalize(center - eye);
        Vec3 s = normalize(cross(f, up));
        Vec3 u = cross(s, f);

        Mat4 mat = Mat4::identity();
        mat.m[0][0] = s.x;
        mat.m[1][0] = s.y;
        mat.m[2][0] = s.z;
        mat.m[0][1] = u.x;
        mat.m[1][1] = u.y;
        mat.m[2][1] = u.z;
        mat.m[0][2] = -f.x;
        mat.m[1][2] = -f.y;
        mat.m[2][2] = -f.z;
        mat.m[3][0] = -dot(s, eye);
        mat.m[3][1] = -dot(u, eye);
        mat.m[3][2] = dot(f, eye);
        return mat;
    }

    // Creates a perspective projection matrix
    static Mat4 perspective(float fovY, float aspect, float zNear, float zFar) {
        Mat4 mat = {};
        float const tanHalfFovy = tan(fovY / 2.0f);
        mat.m[0][0] = 1.0f / (aspect * tanHalfFovy);
        mat.m[1][1] = 1.0f / (tanHalfFovy);
        mat.m[2][2] = -(zFar + zNear) / (zFar - zNear);
        mat.m[2][3] = -1.0f;
        mat.m[3][2] = -(2.0f * zFar * zNear) / (zFar - zNear);
        return mat;
    }
};

// Function to calculate the inverse of a matrix.
// For a ray tracer, we need the inverse of the View-Projection matrix.
inline Mat4 inverse(const Mat4& mat) {
    float m[16], inv[16], det;
    int i;
    // Transpose and copy to a 1D array
    for (i = 0; i < 4; i++) {
        m[i * 4 + 0] = mat.m[i][0];
        m[i * 4 + 1] = mat.m[i][1];
        m[i * 4 + 2] = mat.m[i][2];
        m[i * 4 + 3] = mat.m[i][3];
    }
    // ... (Complex matrix inversion logic) ...
    // NOTE: A full 4x4 matrix inversion is long. For simplicity,
    // we'll pass camera vectors directly to the shader to avoid this :D
    return Mat4::identity();  // Placeholder
}

struct Vertex {
    Vec3 position;
    Vec3 normal;
    float texCoord[2];
};

struct Face {
    float diffuse[3];
    float emission[3];
    int diffuseTextureID;
};

Vec3 calcNormal(Vec3 v0, Vec3 v1, Vec3 v2) {
    Vec3 e01 = v1 - v0;
    Vec3 e02 = v2 - v0;
    return normalize(cross(e01, e02));
}

void loadFromFile(
    std::vector<Vertex>& vertices,
    std::vector<uint32_t>& indices,
    std::vector<Face>& faces,
    std::vector<std::string>& textureFiles,
    const std::string& modelPath)
{
    // Clear output vectors
    vertices.clear();
    indices.clear();
    faces.clear();
    textureFiles.clear();

    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    // Assumes materials are in a subfolder named "materials" relative to the model path
    std::string modelDir = modelPath.substr(0, modelPath.find_last_of("/\\") + 1);

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, modelPath.c_str(), modelDir.c_str())) {
        throw std::runtime_error("TinyObjLoader: " + warn + err);
    }

    // A map to store unique texture paths and their assigned index
    std::map<std::string, int> textureMap;
    for (const auto& material : materials) {
        if (!material.diffuse_texname.empty()) {
            if (textureMap.find(material.diffuse_texname) == textureMap.end()) {
                int textureId = static_cast<int>(textureFiles.size());
                textureMap[material.diffuse_texname] = textureId;
                textureFiles.push_back(modelDir + material.diffuse_texname);
            }
        }
    }

    // Process each shape in the model
    for (const auto& shape : shapes) {
        // A face in tinyobjloader is a polygon. We assume triangles.
        size_t index_offset = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); ++f) {
            size_t num_verts_in_face = shape.mesh.num_face_vertices[f];

            if (num_verts_in_face != 3) {
                // This logic only supports triangles, so we skip non-triangle faces.
                index_offset += num_verts_in_face;
                continue;
            }

            // Get the material for this face
            Face current_face{};
            int material_id = shape.mesh.material_ids[f];

            if (material_id >= 0) {
                const auto& mat = materials[material_id];
                current_face.diffuse[0] = mat.diffuse[0];
                current_face.diffuse[1] = mat.diffuse[1];
                current_face.diffuse[2] = mat.diffuse[2];
                current_face.emission[0] = mat.emission[0];
                current_face.emission[1] = mat.emission[1];
                current_face.emission[2] = mat.emission[2];

                if (!mat.diffuse_texname.empty()) {
                    current_face.diffuseTextureID = textureMap.at(mat.diffuse_texname);
                }
                else {
                    current_face.diffuseTextureID = -1;
                }
            }
            else {
                // Default material if none is assigned
                current_face.diffuse[0] = 0.8f; current_face.diffuse[1] = 0.8f; current_face.diffuse[2] = 0.8f;
                current_face.emission[0] = 0.0f; current_face.emission[1] = 0.0f; current_face.emission[2] = 0.0f;
                current_face.diffuseTextureID = -1;
            }
            faces.push_back(current_face);

            // Process the 3 vertices for this triangle
            for (size_t v = 0; v < 3; ++v) {
                tinyobj::index_t idx = shape.mesh.indices[index_offset + v];
                Vertex vertex{};

                vertex.position.x = attrib.vertices[3 * idx.vertex_index + 0];
                vertex.position.y = attrib.vertices[3 * idx.vertex_index + 1];
                vertex.position.z = attrib.vertices[3 * idx.vertex_index + 2];

                if (idx.normal_index >= 0) {
                    vertex.normal.x = attrib.normals[3 * idx.normal_index + 0];
                    vertex.normal.y = attrib.normals[3 * idx.normal_index + 1];
                    vertex.normal.z = attrib.normals[3 * idx.normal_index + 2];
                }

                if (idx.texcoord_index >= 0) {
                    vertex.texCoord[0] = attrib.texcoords[2 * idx.texcoord_index + 0];
                    vertex.texCoord[1] = 1.0f - attrib.texcoords[2 * idx.texcoord_index + 1];
                }

                vertices.push_back(vertex);
                indices.push_back(static_cast<uint32_t>(indices.size()));
            }
            index_offset += num_verts_in_face;
        }
    }
}

std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open file!");
    }

    size_t fileSize = file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

struct Context {
    Context() {
        // Create window
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan Pathtracing", nullptr, nullptr);

        // Prepase extensions and layers
        uint32_t glfwExtensionCount = 0;
        const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
        std::vector extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        std::vector layers{"VK_LAYER_KHRONOS_validation"};

        auto vkGetInstanceProcAddr = dl.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
        VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

        // Create instance
        vk::ApplicationInfo appInfo;
        appInfo.setApiVersion(VK_API_VERSION_1_4);

        vk::InstanceCreateInfo instanceInfo;
        instanceInfo.setPApplicationInfo(&appInfo);
        instanceInfo.setPEnabledLayerNames(layers);
        instanceInfo.setPEnabledExtensionNames(extensions);
        instance = vk::createInstanceUnique(instanceInfo);
        VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance);

        // Pick first gpu
        physicalDevice = instance->enumeratePhysicalDevices().front();

        // Create debug messenger
        vk::DebugUtilsMessengerCreateInfoEXT messengerInfo;
        messengerInfo.setMessageSeverity(vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
        messengerInfo.setMessageType(vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);
        messengerInfo.setPfnUserCallback(&debugUtilsMessengerCallback);
        messenger = instance->createDebugUtilsMessengerEXTUnique(messengerInfo);

        // Create surface
        VkSurfaceKHR _surface;
        VkResult res = glfwCreateWindowSurface(VkInstance(*instance), window, nullptr, &_surface);
        if (res != VK_SUCCESS) {
            throw std::runtime_error("failed to create window surface!");
        }
        surface = vk::UniqueSurfaceKHR(vk::SurfaceKHR(_surface), {*instance});

        // Find queue family
        std::vector queueFamilies = physicalDevice.getQueueFamilyProperties();
        for (int i = 0; i < queueFamilies.size(); i++) {
            auto supportCompute = queueFamilies[i].queueFlags & vk::QueueFlagBits::eCompute;
            auto supportPresent = physicalDevice.getSurfaceSupportKHR(i, *surface);
            if (supportCompute && supportPresent) {
                queueFamilyIndex = i;
            }
        }

        // Create device
        const float queuePriority = 1.0f;
        vk::DeviceQueueCreateInfo queueCreateInfo;
        queueCreateInfo.setQueueFamilyIndex(queueFamilyIndex);
        queueCreateInfo.setQueuePriorities(queuePriority);

        const std::vector deviceExtensions{
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
            VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
            VK_KHR_MAINTENANCE3_EXTENSION_NAME,
            VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME,
            VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
            VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
            VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
            VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
        };

        if (!checkDeviceExtensionSupport(deviceExtensions)) {
            throw std::runtime_error("Some required extensions are not supported");
        }

        vk::DeviceCreateInfo deviceInfo;
        deviceInfo.setQueueCreateInfos(queueCreateInfo);
        deviceInfo.setPEnabledExtensionNames(deviceExtensions);

        vk::PhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures{true};
        vk::PhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures{true};
        vk::PhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures{true};
        vk::StructureChain createInfoChain{
            deviceInfo,
            bufferDeviceAddressFeatures,
            rayTracingPipelineFeatures,
            accelerationStructureFeatures,
        };

        device = physicalDevice.createDeviceUnique(createInfoChain.get<vk::DeviceCreateInfo>());
        VULKAN_HPP_DEFAULT_DISPATCHER.init(*device);

        queue = device->getQueue(queueFamilyIndex, 0);

        // Create command pool
        vk::CommandPoolCreateInfo commandPoolInfo;
        commandPoolInfo.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
        commandPoolInfo.setQueueFamilyIndex(queueFamilyIndex);
        commandPool = device->createCommandPoolUnique(commandPoolInfo);

        // Create descriptor pool
        std::vector<vk::DescriptorPoolSize> poolSizes{
            {vk::DescriptorType::eAccelerationStructureKHR, 1},
            {vk::DescriptorType::eStorageImage, 1},
            {vk::DescriptorType::eStorageBuffer, 3},
        };

        vk::DescriptorPoolCreateInfo descPoolInfo;
        descPoolInfo.setPoolSizes(poolSizes);
        descPoolInfo.setMaxSets(1);
        descPoolInfo.setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet);
        descPool = device->createDescriptorPoolUnique(descPoolInfo);
    }

    bool checkDeviceExtensionSupport(const std::vector<const char*>& requiredExtensions) const {
        std::vector<vk::ExtensionProperties> availableExtensions = physicalDevice.enumerateDeviceExtensionProperties();
        std::vector<std::string> requiredExtensionNames(requiredExtensions.begin(), requiredExtensions.end());

        for (const auto& extension : availableExtensions) {
            requiredExtensionNames.erase(std::remove(requiredExtensionNames.begin(), requiredExtensionNames.end(), extension.extensionName),
                                         requiredExtensionNames.end());
        }

        if (requiredExtensionNames.empty()) {
            std::cout << "All required extensions are supported by the device." << std::endl;
            return true;
        } else {
            std::cout << "The following required extensions are not supported by the device:" << std::endl;
            for (const auto& name : requiredExtensionNames) {
                std::cout << "\t" << name << std::endl;
            }
            return false;
        }
    }

    uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) const {
        vk::PhysicalDeviceMemoryProperties memProperties = physicalDevice.getMemoryProperties();
        for (uint32_t i = 0; i != memProperties.memoryTypeCount; ++i) {
            if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        throw std::runtime_error("failed to find suitable memory type");
    }

    void oneTimeSubmit(const std::function<void(vk::CommandBuffer)>& func) const {
        vk::CommandBufferAllocateInfo commandBufferInfo;
        commandBufferInfo.setCommandPool(*commandPool);
        commandBufferInfo.setCommandBufferCount(1);

        vk::UniqueCommandBuffer commandBuffer = std::move(device->allocateCommandBuffersUnique(commandBufferInfo).front());
        commandBuffer->begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        func(*commandBuffer);
        commandBuffer->end();

        vk::SubmitInfo submitInfo;
        submitInfo.setCommandBuffers(*commandBuffer);
        queue.submit(submitInfo);
        queue.waitIdle();
    }

    vk::UniqueDescriptorSet allocateDescSet(vk::DescriptorSetLayout descSetLayout) {
        vk::DescriptorSetAllocateInfo descSetInfo;
        descSetInfo.setDescriptorPool(*descPool);
        descSetInfo.setSetLayouts(descSetLayout);
        return std::move(device->allocateDescriptorSetsUnique(descSetInfo).front());
    }

    static VKAPI_ATTR vk::Bool32 VKAPI_CALL 
        debugUtilsMessengerCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                    vk::DebugUtilsMessageTypeFlagsEXT messageTypes,
                                    vk::DebugUtilsMessengerCallbackDataEXT const* pCallbackData,
                                    void* pUserData) {
        std::cerr << pCallbackData->pMessage << std::endl;
        return VK_FALSE;
    }

    GLFWwindow* window;
    vk::detail::DynamicLoader dl;
    vk::UniqueInstance instance;
    vk::UniqueDebugUtilsMessengerEXT messenger;
    vk::UniqueSurfaceKHR surface;
    vk::UniqueDevice device;
    vk::PhysicalDevice physicalDevice;
    uint32_t queueFamilyIndex;
    vk::Queue queue;
    vk::UniqueCommandPool commandPool;
    vk::UniqueDescriptorPool descPool;
};

struct Buffer {
    enum class Type {
        Scratch,
        AccelInput,
        AccelStorage,
        ShaderBindingTable,
        TransferSrc,
    };

    Buffer() = default;
    Buffer(const Context& context, Type type, vk::DeviceSize size, const void* data = nullptr) {
        vk::BufferUsageFlags usage;
        vk::MemoryPropertyFlags memoryProps;
        using Usage = vk::BufferUsageFlagBits;
        using Memory = vk::MemoryPropertyFlagBits;
        if (type == Type::AccelInput) {
            usage = Usage::eAccelerationStructureBuildInputReadOnlyKHR | Usage::eStorageBuffer | Usage::eShaderDeviceAddress;
            memoryProps = Memory::eHostVisible | Memory::eHostCoherent;
        } else if (type == Type::Scratch) {
            usage = Usage::eStorageBuffer | Usage::eShaderDeviceAddress;
            memoryProps = Memory::eDeviceLocal;
        } else if (type == Type::AccelStorage) {
            usage = Usage::eAccelerationStructureStorageKHR | Usage::eShaderDeviceAddress;
            memoryProps = Memory::eDeviceLocal;
        } else if (type == Type::ShaderBindingTable) {
            usage = Usage::eShaderBindingTableKHR | Usage::eShaderDeviceAddress;
            memoryProps = Memory::eHostVisible | Memory::eHostCoherent;
        } else if (type == Type::TransferSrc) {
            usage = Usage::eTransferSrc;
            memoryProps = Memory::eHostVisible | Memory::eHostCoherent;
        }

        buffer = context.device->createBufferUnique({{}, size, usage});

        // Allocate memory
        vk::MemoryRequirements requirements = context.device->getBufferMemoryRequirements(*buffer);
        uint32_t memoryTypeIndex = context.findMemoryType(requirements.memoryTypeBits, memoryProps);

        vk::MemoryAllocateFlagsInfo flagsInfo{vk::MemoryAllocateFlagBits::eDeviceAddress};

        vk::MemoryAllocateInfo memoryInfo;
        memoryInfo.setAllocationSize(requirements.size);
        memoryInfo.setMemoryTypeIndex(memoryTypeIndex);
        memoryInfo.setPNext(&flagsInfo);
        memory = context.device->allocateMemoryUnique(memoryInfo);

        context.device->bindBufferMemory(*buffer, *memory, 0);

        // Get device address
        vk::BufferDeviceAddressInfoKHR bufferDeviceAI{*buffer};
        deviceAddress = context.device->getBufferAddressKHR(&bufferDeviceAI);

        descBufferInfo.setBuffer(*buffer);
        descBufferInfo.setOffset(0);
        descBufferInfo.setRange(size);

        if (data) {
            void* mapped = context.device->mapMemory(*memory, 0, size);
            memcpy(mapped, data, size);
            context.device->unmapMemory(*memory);
        }
    }

    vk::UniqueBuffer buffer;
    vk::UniqueDeviceMemory memory;
    vk::DescriptorBufferInfo descBufferInfo;
    uint64_t deviceAddress = 0;
};

struct Image {
    Image() = default;
    Image(const Context& context,
        vk::Extent2D extent,
        vk::Format format,
        vk::ImageUsageFlags usage,
        vk::ImageLayout finalLayout = vk::ImageLayout::eGeneral)
    {
        // Create image
        vk::ImageCreateInfo imageInfo;
        imageInfo.setImageType(vk::ImageType::e2D);
        imageInfo.setExtent({extent.width, extent.height, 1});
        imageInfo.setMipLevels(1);
        imageInfo.setArrayLayers(1);
        imageInfo.setFormat(format);
        imageInfo.setUsage(usage);
        image = context.device->createImageUnique(imageInfo);

        // Allocate memory
        vk::MemoryRequirements requirements = context.device->getImageMemoryRequirements(*image);
        uint32_t memoryTypeIndex = context.findMemoryType(requirements.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal);
        vk::MemoryAllocateInfo memoryInfo;
        memoryInfo.setAllocationSize(requirements.size);
        memoryInfo.setMemoryTypeIndex(memoryTypeIndex);
        memory = context.device->allocateMemoryUnique(memoryInfo);

        // Bind memory and image
        context.device->bindImageMemory(*image, *memory, 0);

        // Create image view
        vk::ImageViewCreateInfo imageViewInfo;
        imageViewInfo.setImage(*image);
        imageViewInfo.setViewType(vk::ImageViewType::e2D);
        imageViewInfo.setFormat(format);
        imageViewInfo.setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
        view = context.device->createImageViewUnique(imageViewInfo);

        // Set image info
        descImageInfo.setImageView(*view);
        descImageInfo.setImageLayout(finalLayout);

        // Only transition the layout if a specific final layout is requested
        if (finalLayout != vk::ImageLayout::eUndefined) {
            context.oneTimeSubmit([&](vk::CommandBuffer commandBuffer) {
                setImageLayout(commandBuffer, *image, vk::ImageLayout::eUndefined, finalLayout);
                });
        }
    }

    static vk::AccessFlags toAccessFlags(vk::ImageLayout layout) {
        switch (layout) {
            case vk::ImageLayout::eTransferSrcOptimal:
                return vk::AccessFlagBits::eTransferRead;
            case vk::ImageLayout::eTransferDstOptimal:
                return vk::AccessFlagBits::eTransferWrite;
            default:
                return {};
        }
    }

    static void setImageLayout(vk::CommandBuffer commandBuffer, vk::Image image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout) {
        vk::ImageMemoryBarrier barrier;
        barrier.setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
        barrier.setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED);
        barrier.setImage(image);
        barrier.setOldLayout(oldLayout);
        barrier.setNewLayout(newLayout);
        barrier.setSubresourceRange({vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
        barrier.setSrcAccessMask(toAccessFlags(oldLayout));
        barrier.setDstAccessMask(toAccessFlags(newLayout));
        commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands,  //
                                      vk::PipelineStageFlagBits::eAllCommands,  //
                                      {}, {}, {}, barrier);
    }

    static void copyImage(vk::CommandBuffer commandBuffer, vk::Image srcImage, vk::Image dstImage) {
        vk::ImageCopy copyRegion;
        copyRegion.setSrcSubresource({vk::ImageAspectFlagBits::eColor, 0, 0, 1});
        copyRegion.setDstSubresource({vk::ImageAspectFlagBits::eColor, 0, 0, 1});
        copyRegion.setExtent({WIDTH, HEIGHT, 1});
        commandBuffer.copyImage(srcImage, vk::ImageLayout::eTransferSrcOptimal, dstImage, vk::ImageLayout::eTransferDstOptimal, copyRegion);
    }

    vk::UniqueImage image;
    vk::UniqueImageView view;
    vk::UniqueDeviceMemory memory;
    vk::DescriptorImageInfo descImageInfo;
};

struct Texture {
    Image image;
    vk::UniqueSampler sampler;
};

struct Accel {
    Accel() = default;
    Accel(const Context& context, vk::AccelerationStructureGeometryKHR geometry, uint32_t primitiveCount, vk::AccelerationStructureTypeKHR type) {
        vk::AccelerationStructureBuildGeometryInfoKHR buildGeometryInfo;
        buildGeometryInfo.setType(type);
        buildGeometryInfo.setFlags(vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace);
        buildGeometryInfo.setGeometries(geometry);

        // Create buffer
        vk::AccelerationStructureBuildSizesInfoKHR buildSizesInfo = context.device->getAccelerationStructureBuildSizesKHR(  //
            vk::AccelerationStructureBuildTypeKHR::eDevice, buildGeometryInfo, primitiveCount);
        vk::DeviceSize size = buildSizesInfo.accelerationStructureSize;
        buffer = Buffer{context, Buffer::Type::AccelStorage, size};

        // Create accel
        vk::AccelerationStructureCreateInfoKHR accelInfo;
        accelInfo.setBuffer(*buffer.buffer);
        accelInfo.setSize(size);
        accelInfo.setType(type);
        accel = context.device->createAccelerationStructureKHRUnique(accelInfo);

        // Build
        Buffer scratchBuffer{context, Buffer::Type::Scratch, buildSizesInfo.buildScratchSize};
        buildGeometryInfo.setScratchData(scratchBuffer.deviceAddress);
        buildGeometryInfo.setDstAccelerationStructure(*accel);

        context.oneTimeSubmit([&](vk::CommandBuffer commandBuffer) {  //
            vk::AccelerationStructureBuildRangeInfoKHR buildRangeInfo;
            buildRangeInfo.setPrimitiveCount(primitiveCount);
            buildRangeInfo.setFirstVertex(0);
            buildRangeInfo.setPrimitiveOffset(0);
            buildRangeInfo.setTransformOffset(0);
            commandBuffer.buildAccelerationStructuresKHR(buildGeometryInfo, &buildRangeInfo);
        });

        descAccelInfo.setAccelerationStructures(*accel);
    }

    Buffer buffer;
    vk::UniqueAccelerationStructureKHR accel;
    vk::WriteDescriptorSetAccelerationStructureKHR descAccelInfo;
};

struct Camera {
    Vec3 position = { 0.0f, 100.0f, 0.0f };
    Vec3 front = { 0.0f, 0.0f, -1.0f };
    Vec3 up = { 0.0f, -1.0f, 0.0f };
    Vec3 right = { 1.0f, 0.0f, 0.0f };
    Vec3 worldUp = { 0.0f, -1.0f, 0.0f };

    float yaw = 0.0f;
    float pitch = 0.0f;
    float speed = 50.0f;
    float sensitivity = 0.1f;

    void updateCameraVectors() {
        Vec3 newFront;
        newFront.x = cos(yaw * PI / 180.0f) * cos(pitch * PI / 180.0f);
        newFront.y = sin(pitch * PI / 180.0f);
        newFront.z = sin(yaw * PI / 180.0f) * cos(pitch * PI / 180.0f);
        front = normalize(newFront);

        // For right-handed coordinate system (Vulkan)
        right = normalize(cross(worldUp, front));  // worldUp x front gives right
        up = normalize(cross(front, right));       // front x right gives up
    }

    void processKeyboard(const std::string& direction, float deltaTime) {
        float velocity = speed * deltaTime;
        if (direction == "FORWARD")  position += front * velocity;
        if (direction == "BACKWARD") position -= front * velocity;
        if (direction == "LEFT")     position -= right * velocity;
        if (direction == "RIGHT")    position += right * velocity;
    }

    void processMouse(float xoffset, float yoffset) {
        xoffset *= sensitivity;
        yoffset *= sensitivity;

        yaw += xoffset;
        pitch += yoffset;

        // Constrain pitch to avoid gimbal lock
        if (pitch > 89.0f) pitch = 89.0f;
        if (pitch < -89.0f) pitch = -89.0f;

        updateCameraVectors();
    }
};

struct PushConstants {
    int frame;
    float pad1, pad2, pad3;  // Padding for alignment
    Vec3 cameraPos;
    float pad4;
    Vec3 cameraFront;
    float pad5;
    Vec3 cameraUp;
    float pad6;
    Vec3 cameraRight;
};

Texture createTexture(const Context& context, const std::string& path) {
    // 1. Load image pixels from file using stb_image
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    if (!pixels) {
        throw std::runtime_error("Failed to load texture image: " + path);
    }
    vk::DeviceSize imageSize = texWidth * texHeight * 4;

    // 2. Use your existing Buffer struct to create a staging buffer
    Buffer stagingBuffer(context, Buffer::Type::TransferSrc, imageSize, pixels);
    stbi_image_free(pixels); // We can now free the CPU-side pixels

    // 3. Create the destination GPU image
    vk::Extent2D extent = { (uint32_t)texWidth, (uint32_t)texHeight };
    vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
    // We pass eUndefined to prevent the automatic layout transition
    Image gpuImage(context, extent, vk::Format::eR8G8B8A8Srgb, usage, vk::ImageLayout::eUndefined);

    // 4. Copy data from staging buffer to GPU image and set the correct layout for shaders
    context.oneTimeSubmit([&](vk::CommandBuffer cmd) {
        // Transition layout to be ready for copying
        Image::setImageLayout(cmd, *gpuImage.image, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);

        // Copy the data
        vk::BufferImageCopy region;
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = vk::Offset3D(0, 0, 0);
        region.imageExtent = vk::Extent3D(extent.width, extent.height, 1);
        cmd.copyBufferToImage(*stagingBuffer.buffer, *gpuImage.image, vk::ImageLayout::eTransferDstOptimal, region);

        // Transition layout to be ready for shader reading
        Image::setImageLayout(cmd, *gpuImage.image, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
    });

    // 5. Create a texture sampler
    vk::SamplerCreateInfo samplerInfo{};
    samplerInfo.magFilter = vk::Filter::eLinear;
    samplerInfo.minFilter = vk::Filter::eLinear;
    samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
    samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
    samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
    samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.anisotropyEnable = VK_FALSE;  // requires device feature enabled
    samplerInfo.maxAnisotropy = 1; //context.physicalDevice.getProperties().limits.maxSamplerAnisotropy;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = vk::CompareOp::eAlways;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    vk::UniqueSampler sampler = context.device->createSamplerUnique(samplerInfo);

    // 6. Return the completed texture object
    return { std::move(gpuImage), std::move(sampler) };
}

Camera camera;
bool firstMouse = true;
double lastX = WIDTH / 2.0;
double lastY = HEIGHT / 2.0;

void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    double xoffset = xpos - lastX;
    double yoffset = lastY - ypos;  // Reversed since y-coordinates go from top to bottom
    lastX = xpos;
    lastY = ypos;

    camera.processMouse(xoffset, yoffset);
}

void processInput(GLFWwindow* window, float deltaTime) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        camera.processKeyboard("FORWARD", deltaTime);
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        camera.processKeyboard("BACKWARD", deltaTime);
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        camera.processKeyboard("LEFT", deltaTime);
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        camera.processKeyboard("RIGHT", deltaTime);
}

int main() {
    Context context;

    glfwSetInputMode(context.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetCursorPosCallback(context.window, mouse_callback);

    vk::SwapchainCreateInfoKHR swapchainInfo;
    swapchainInfo.setSurface(*context.surface);
    swapchainInfo.setMinImageCount(3);
    swapchainInfo.setImageFormat(vk::Format::eB8G8R8A8Unorm);
    swapchainInfo.setImageColorSpace(vk::ColorSpaceKHR::eSrgbNonlinear);
    swapchainInfo.setImageExtent({WIDTH, HEIGHT});
    swapchainInfo.setImageArrayLayers(1);
    swapchainInfo.setImageUsage(vk::ImageUsageFlagBits::eTransferDst);
    swapchainInfo.setPreTransform(vk::SurfaceTransformFlagBitsKHR::eIdentity);
    swapchainInfo.setPresentMode(vk::PresentModeKHR::eFifo);
    swapchainInfo.setClipped(true);
    swapchainInfo.setQueueFamilyIndices(context.queueFamilyIndex);
    vk::UniqueSwapchainKHR swapchain = context.device->createSwapchainKHRUnique(swapchainInfo);

    std::vector<vk::Image> swapchainImages = context.device->getSwapchainImagesKHR(*swapchain);

    vk::CommandBufferAllocateInfo commandBufferInfo;
    commandBufferInfo.setCommandPool(*context.commandPool);
    commandBufferInfo.setCommandBufferCount(static_cast<uint32_t>(swapchainImages.size()));
    std::vector<vk::UniqueCommandBuffer> commandBuffers = context.device->allocateCommandBuffersUnique(commandBufferInfo);

    Image outputImage{context,
                      {WIDTH, HEIGHT},
                      vk::Format::eB8G8R8A8Unorm,
                      vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst};

    // Load mesh
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Face> faces;
    std::vector<std::string> textureFiles;
    loadFromFile(vertices, indices, faces, textureFiles, "../assets/models/sponza.obj");
    //loadFromFile(vertices, indices, faces, textureFiles, "../assets/models/CornellBox-Original.obj");
    //loadFromFile(vertices, indices, faces, textureFiles, "../assets/models/dragon.obj");

	// Load textures
    std::vector<Texture> textures;
    textures.reserve(textureFiles.size());
    for (const auto& filePath : textureFiles) {
        textures.push_back(createTexture(context, filePath));
    }

    Buffer vertexBuffer{context, Buffer::Type::AccelInput, sizeof(Vertex) * vertices.size(), vertices.data()};
    Buffer indexBuffer{context, Buffer::Type::AccelInput, sizeof(uint32_t) * indices.size(), indices.data()};
    Buffer faceBuffer{context, Buffer::Type::AccelInput, sizeof(Face) * faces.size(), faces.data()};

    // Create bottom level accel struct
    vk::AccelerationStructureGeometryTrianglesDataKHR triangleData;
    triangleData.setVertexFormat(vk::Format::eR32G32B32Sfloat);
    triangleData.setVertexData(vertexBuffer.deviceAddress);
    triangleData.setVertexStride(sizeof(Vertex));
    triangleData.setMaxVertex(static_cast<uint32_t>(vertices.size()));
    triangleData.setIndexType(vk::IndexType::eUint32);
    triangleData.setIndexData(indexBuffer.deviceAddress);

    vk::AccelerationStructureGeometryKHR triangleGeometry;
    triangleGeometry.setGeometryType(vk::GeometryTypeKHR::eTriangles);
    triangleGeometry.setGeometry({triangleData});
    triangleGeometry.setFlags(vk::GeometryFlagBitsKHR::eOpaque);

    const auto primitiveCount = static_cast<uint32_t>(indices.size() / 3);

    Accel bottomAccel{context, triangleGeometry, primitiveCount, vk::AccelerationStructureTypeKHR::eBottomLevel};

    // Create top level accel struct
    vk::TransformMatrixKHR transformMatrix = std::array{
        std::array{1.0f, 0.0f, 0.0f, 0.0f},
        std::array{0.0f, 1.0f, 0.0f, 0.0f},
        std::array{0.0f, 0.0f, 1.0f, 0.0f},
    };

    vk::AccelerationStructureInstanceKHR accelInstance;
    accelInstance.setTransform(transformMatrix);
    accelInstance.setMask(0xFF);
    accelInstance.setAccelerationStructureReference(bottomAccel.buffer.deviceAddress);
    accelInstance.setFlags(vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable);

    Buffer instancesBuffer{context, Buffer::Type::AccelInput, sizeof(vk::AccelerationStructureInstanceKHR), &accelInstance};

    vk::AccelerationStructureGeometryInstancesDataKHR instancesData;
    instancesData.setArrayOfPointers(false);
    instancesData.setData(instancesBuffer.deviceAddress);

    vk::AccelerationStructureGeometryKHR instanceGeometry;
    instanceGeometry.setGeometryType(vk::GeometryTypeKHR::eInstances);
    instanceGeometry.setGeometry({instancesData});
    instanceGeometry.setFlags(vk::GeometryFlagBitsKHR::eOpaque);

    Accel topAccel{context, instanceGeometry, 1, vk::AccelerationStructureTypeKHR::eTopLevel};

    // Load shaders
    const std::vector<char> raygenCode = readFile("../assets/shaders/raygen.rgen.spv");
    const std::vector<char> missCode = readFile("../assets/shaders/miss.rmiss.spv");
    const std::vector<char> chitCode = readFile("../assets/shaders/closesthit.rchit.spv");

    std::vector<vk::UniqueShaderModule> shaderModules(3);
    shaderModules[0] = context.device->createShaderModuleUnique({{}, raygenCode.size(), reinterpret_cast<const uint32_t*>(raygenCode.data())});
    shaderModules[1] = context.device->createShaderModuleUnique({{}, missCode.size(), reinterpret_cast<const uint32_t*>(missCode.data())});
    shaderModules[2] = context.device->createShaderModuleUnique({{}, chitCode.size(), reinterpret_cast<const uint32_t*>(chitCode.data())});

    std::vector<vk::PipelineShaderStageCreateInfo> shaderStages(3);
    shaderStages[0] = {{}, vk::ShaderStageFlagBits::eRaygenKHR, *shaderModules[0], "main"};
    shaderStages[1] = {{}, vk::ShaderStageFlagBits::eMissKHR, *shaderModules[1], "main"};
    shaderStages[2] = {{}, vk::ShaderStageFlagBits::eClosestHitKHR, *shaderModules[2], "main"};

    std::vector<vk::RayTracingShaderGroupCreateInfoKHR> shaderGroups(3);
    shaderGroups[0] = {vk::RayTracingShaderGroupTypeKHR::eGeneral, 0, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR};
    shaderGroups[1] = {vk::RayTracingShaderGroupTypeKHR::eGeneral, 1, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR};
    shaderGroups[2] = {vk::RayTracingShaderGroupTypeKHR::eTrianglesHitGroup, VK_SHADER_UNUSED_KHR, 2, VK_SHADER_UNUSED_KHR, VK_SHADER_UNUSED_KHR};

    // Note: Ensure your device supports enough samplers. Sponza has ~50 textures.
    // A size of 0 is invalid, so handle the no-texture case.
    const uint32_t textureCount = textures.empty() ? 1 : static_cast<uint32_t>(textures.size());

    // create ray tracing pipeline
    std::vector<vk::DescriptorSetLayoutBinding> bindings{
        {0, vk::DescriptorType::eAccelerationStructureKHR, 1, vk::ShaderStageFlagBits::eRaygenKHR},  // Binding = 0 : TLAS
        {1, vk::DescriptorType::eStorageImage, 1, vk::ShaderStageFlagBits::eRaygenKHR},              // Binding = 1 : Storage image
        {2, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eClosestHitKHR},         // Binding = 2 : Vertices
        {3, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eClosestHitKHR},         // Binding = 3 : Indices
        {4, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eClosestHitKHR},         // Binding = 4 : Faces
		{5, vk::DescriptorType::eCombinedImageSampler, textureCount, vk::ShaderStageFlagBits::eClosestHitKHR},  // Binding = 5 : Textures
    };

    // Create desc set layout
    vk::DescriptorSetLayoutCreateInfo descSetLayoutInfo;
    descSetLayoutInfo.setBindings(bindings);
    vk::UniqueDescriptorSetLayout descSetLayout = context.device->createDescriptorSetLayoutUnique(descSetLayoutInfo);

    // Create pipeline layout
    vk::PushConstantRange pushRange;
    pushRange.setOffset(0);
    pushRange.setSize(sizeof(PushConstants));
    pushRange.setStageFlags(vk::ShaderStageFlagBits::eRaygenKHR);

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
    pipelineLayoutInfo.setSetLayouts(*descSetLayout);
    pipelineLayoutInfo.setPushConstantRanges(pushRange);
    vk::UniquePipelineLayout pipelineLayout = context.device->createPipelineLayoutUnique(pipelineLayoutInfo);

    // Create pipeline
    vk::RayTracingPipelineCreateInfoKHR rtPipelineInfo;
    rtPipelineInfo.setStages(shaderStages);
    rtPipelineInfo.setGroups(shaderGroups);
    rtPipelineInfo.setMaxPipelineRayRecursionDepth(4);
    rtPipelineInfo.setLayout(*pipelineLayout);

    auto result = context.device->createRayTracingPipelineKHRUnique(nullptr, nullptr, rtPipelineInfo);
    if (result.result != vk::Result::eSuccess) {
        throw std::runtime_error("failed to create ray tracing pipeline.");
    }

    vk::UniquePipeline pipeline = std::move(result.value);

    // Get ray tracing properties
    auto properties = context.physicalDevice.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();
    auto rtProperties = properties.get<vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();

    // Calculate shader binding table (SBT) size
    uint32_t handleSize = rtProperties.shaderGroupHandleSize;
    uint32_t handleSizeAligned = rtProperties.shaderGroupHandleAlignment;
    uint32_t groupCount = static_cast<uint32_t>(shaderGroups.size());
    uint32_t sbtSize = groupCount * handleSizeAligned;

    // Get shader group handles
    std::vector<uint8_t> handleStorage(sbtSize);
    if (context.device->getRayTracingShaderGroupHandlesKHR(*pipeline, 0, groupCount, sbtSize, handleStorage.data()) != vk::Result::eSuccess) {
        throw std::runtime_error("failed to get ray tracing shader group handles.");
    }

    // Create SBT
    Buffer raygenSBT{context, Buffer::Type::ShaderBindingTable, handleSize, handleStorage.data() + 0 * handleSizeAligned};
    Buffer missSBT{context, Buffer::Type::ShaderBindingTable, handleSize, handleStorage.data() + 1 * handleSizeAligned};
    Buffer hitSBT{context, Buffer::Type::ShaderBindingTable, handleSize, handleStorage.data() + 2 * handleSizeAligned};

    uint32_t stride = rtProperties.shaderGroupHandleAlignment;
    uint32_t size = rtProperties.shaderGroupHandleAlignment;

    vk::StridedDeviceAddressRegionKHR raygenRegion{raygenSBT.deviceAddress, stride, size};
    vk::StridedDeviceAddressRegionKHR missRegion{missSBT.deviceAddress, stride, size};
    vk::StridedDeviceAddressRegionKHR hitRegion{hitSBT.deviceAddress, stride, size};

    // Create desc set
    vk::UniqueDescriptorSet descSet = context.allocateDescSet(*descSetLayout);

    // Create dummy resources that persist until the end of the program
    vk::UniqueSampler dummySampler;
    vk::UniqueImageView dummyImageView;

    // Prepare the texture info for the descriptor write
    std::vector<vk::DescriptorImageInfo> imageInfos;
    if (textures.empty()) {
        // Create a dummy texture/sampler for the case when there are no textures
        vk::SamplerCreateInfo samplerInfo{};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
        dummySampler = context.device->createSamplerUnique(samplerInfo);

        // Create a dummy 1x1 black texture
        Image dummyImage{ context,
                        {1, 1},
                        vk::Format::eR8G8B8A8Unorm,
                        vk::ImageUsageFlagBits::eSampled,
                        vk::ImageLayout::eShaderReadOnlyOptimal };

        dummyImageView = std::move(dummyImage.view);

        // Add the dummy descriptor
        imageInfos.push_back({ *dummySampler, *dummyImageView, vk::ImageLayout::eShaderReadOnlyOptimal });
    }
    else {
        for (const auto& texture : textures) {
            imageInfos.push_back({ *texture.sampler, *texture.image.view, vk::ImageLayout::eShaderReadOnlyOptimal });
        }
    }

    // Create the descriptor writes
    std::vector<vk::WriteDescriptorSet> writes;
    writes.resize(6);

    writes[0].setDstSet(*descSet);
    writes[0].setDstBinding(0);
    writes[0].setDescriptorCount(1);
    writes[0].setDescriptorType(vk::DescriptorType::eAccelerationStructureKHR);
    writes[0].setPNext(&topAccel.descAccelInfo);

    writes[1].setDstSet(*descSet);
    writes[1].setDstBinding(1);
    writes[1].setDescriptorCount(1);
    writes[1].setDescriptorType(vk::DescriptorType::eStorageImage);
    writes[1].setImageInfo(outputImage.descImageInfo);

    writes[2].setDstSet(*descSet);
    writes[2].setDstBinding(2);
    writes[2].setDescriptorCount(1);
    writes[2].setDescriptorType(vk::DescriptorType::eStorageBuffer);
    writes[2].setBufferInfo(vertexBuffer.descBufferInfo);

    writes[3].setDstSet(*descSet);
    writes[3].setDstBinding(3);
    writes[3].setDescriptorCount(1);
    writes[3].setDescriptorType(vk::DescriptorType::eStorageBuffer);
    writes[3].setBufferInfo(indexBuffer.descBufferInfo);

    writes[4].setDstSet(*descSet);
    writes[4].setDstBinding(4);
    writes[4].setDescriptorCount(1);
    writes[4].setDescriptorType(vk::DescriptorType::eStorageBuffer);
    writes[4].setBufferInfo(faceBuffer.descBufferInfo);

    writes[5].setDstSet(*descSet);
    writes[5].setDstBinding(5);
    writes[5].setDescriptorCount(textureCount);
    writes[5].setDescriptorType(vk::DescriptorType::eCombinedImageSampler);
    writes[5].setImageInfo(imageInfos);

    context.device->updateDescriptorSets(writes, nullptr);

    // Main loop
    float deltaTime = 0.0f;
    float lastFrame = 0.0f;
    uint32_t imageIndex = 0;
    int frame = 0;
    vk::UniqueSemaphore imageAcquiredSemaphore = context.device->createSemaphoreUnique(vk::SemaphoreCreateInfo());

    while (!glfwWindowShouldClose(context.window)) {
        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // Store previous camera position to detect movement
        Vec3 oldPos = camera.position;
        float oldYaw = camera.yaw;
        float oldPitch = camera.pitch;

        // This handles mouse movement via the callback
        glfwPollEvents();
        // This handles keyboard movement
        processInput(context.window, deltaTime);

        // If the camera moved, reset the frame accumulator
        if (oldPos.x != camera.position.x || oldPos.y != camera.position.y || oldPos.z != camera.position.z || oldYaw != camera.yaw ||
            oldPitch != camera.pitch) {
            frame = 0;
        }

        // Acquire next image
        imageIndex = context.device->acquireNextImageKHR(*swapchain, UINT64_MAX, *imageAcquiredSemaphore).value;

        // Populate and push constants
        PushConstants pc;
        pc.frame = frame;
        pc.cameraPos = camera.position;
        pc.cameraFront = camera.front;
        pc.cameraUp = camera.up;
        pc.cameraRight = camera.right;

        // Record commands
        vk::CommandBuffer commandBuffer = *commandBuffers[imageIndex];
        commandBuffer.begin(vk::CommandBufferBeginInfo());
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, *pipeline);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eRayTracingKHR, *pipelineLayout, 0, *descSet, nullptr);
        commandBuffer.pushConstants(*pipelineLayout, vk::ShaderStageFlagBits::eRaygenKHR, 0, sizeof(PushConstants), &pc);
        commandBuffer.traceRaysKHR(raygenRegion, missRegion, hitRegion, {}, WIDTH, HEIGHT, 1);

        vk::Image srcImage = *outputImage.image;
        vk::Image dstImage = swapchainImages[imageIndex];
        Image::setImageLayout(commandBuffer, srcImage, vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferSrcOptimal);
        Image::setImageLayout(commandBuffer, dstImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
        Image::copyImage(commandBuffer, srcImage, dstImage);
        Image::setImageLayout(commandBuffer, srcImage, vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eGeneral);
        Image::setImageLayout(commandBuffer, dstImage, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::ePresentSrcKHR);

        commandBuffer.end();

        // Submit
        context.queue.submit(vk::SubmitInfo().setCommandBuffers(commandBuffer));

        // Present image
        vk::PresentInfoKHR presentInfo;
        presentInfo.setSwapchains(*swapchain);
        presentInfo.setImageIndices(imageIndex);
        presentInfo.setWaitSemaphores(*imageAcquiredSemaphore);
        auto result = context.queue.presentKHR(presentInfo);
        if (result != vk::Result::eSuccess) {
            throw std::runtime_error("failed to present.");
        }
        context.queue.waitIdle();
        frame++;
    }

    context.device->waitIdle();
    glfwDestroyWindow(context.window);
    glfwTerminate();
}
