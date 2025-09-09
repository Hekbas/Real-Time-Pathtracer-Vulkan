#include "model_loader.h"
#include "math/mat3.h"
#include "math/mat4.h"

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE_WRITE
#include "tiny_gltf.h"

#include <fstream>
#include <stdexcept>
#include <iostream>
#include <unordered_map>
#include <filesystem>

namespace fs = std::filesystem;

// Helper function to get transformation matrix from a GLTF node
Mat4 getNodeMatrix(const tinygltf::Node& node) {
    Mat4 matrix = Mat4::identity();

    if (!node.matrix.empty()) {
        // Node has a direct matrix transformation
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                matrix.m[i][j] = static_cast<float>(node.matrix[i * 4 + j]);
            }
        }
    }
    else {
        // Node has separate TRS components
        Vec3 translation(0.0f);
        Vec3 scale(1.0f);

        if (!node.translation.empty()) {
            translation = Vec3(
                static_cast<float>(node.translation[0]),
                static_cast<float>(node.translation[1]),
                static_cast<float>(node.translation[2])
            );
        }

        if (!node.scale.empty()) {
            scale = Vec3(
                static_cast<float>(node.scale[0]),
                static_cast<float>(node.scale[1]),
                static_cast<float>(node.scale[2])
            );
        }

        // Create transformation matrix from TRS
        Mat4 translationMat = Mat4::translate(translation);
        Mat4 scaleMat = Mat4::scale(scale);

        // Handle rotation (quaternion)
        if (!node.rotation.empty()) {
            float x = static_cast<float>(node.rotation[0]);
            float y = static_cast<float>(node.rotation[1]);
            float z = static_cast<float>(node.rotation[2]);
            float w = static_cast<float>(node.rotation[3]);

            // Convert quaternion to rotation matrix
            Mat4 rotationMat;
            rotationMat.m[0][0] = 1 - 2 * y * y - 2 * z * z;
            rotationMat.m[0][1] = 2 * x * y + 2 * w * z;
            rotationMat.m[0][2] = 2 * x * z - 2 * w * y;

            rotationMat.m[1][0] = 2 * x * y - 2 * w * z;
            rotationMat.m[1][1] = 1 - 2 * x * x - 2 * z * z;
            rotationMat.m[1][2] = 2 * y * z + 2 * w * x;

            rotationMat.m[2][0] = 2 * x * z + 2 * w * y;
            rotationMat.m[2][1] = 2 * y * z - 2 * w * x;
            rotationMat.m[2][2] = 1 - 2 * x * x - 2 * y * y;

            rotationMat.m[3][3] = 1.0f;

            matrix = translationMat * rotationMat * scaleMat;
        }
        else {
            matrix = translationMat * scaleMat;
        }
    }

    return matrix;
}

// Recursive function to process nodes
void processNode(
    const tinygltf::Model& model,
    const tinygltf::Node& node,
    const Mat4& parentMatrix,
    std::vector<Vertex>& vertices,
    std::vector<uint32_t>& indices,
    std::vector<Face>& faces,
    std::unordered_map<std::string, int>& textureIndexMap,
    std::vector<std::string>& textureFiles,
    const std::string& modelDir,
    uint32_t& vertexOffset) {

    // Calculate current node's transformation matrix
    Mat4 nodeMatrix = getNodeMatrix(node);
    Mat4 worldMatrix = parentMatrix * nodeMatrix;

    // Extract the 3x3 part for normal transformation (inverse transpose)
    Mat3 worldMatrix3x3;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            worldMatrix3x3.m[i][j] = worldMatrix.m[i][j];
        }
    }

    // Calculate inverse transpose for normal transformation
    Mat3 inverseTranspose = worldMatrix3x3.inverse();

    // Process mesh if this node has one
    if (node.mesh >= 0) {
        const auto& mesh = model.meshes[node.mesh];

        for (const auto& primitive : mesh.primitives) {
            // Skip non-triangle primitives
            if (primitive.mode != TINYGLTF_MODE_TRIANGLES) continue;

            // Get indices
            const auto& indexAccessor = model.accessors[primitive.indices];
            const auto& indexBufferView = model.bufferViews[indexAccessor.bufferView];
            const auto& indexBuffer = model.buffers[indexBufferView.buffer];
            const uint8_t* indexData = indexBuffer.data.data() + indexBufferView.byteOffset + indexAccessor.byteOffset;
            uint32_t indexCount = indexAccessor.count;

            // Extract indices
            std::vector<uint32_t> primitiveIndices;
            if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                const uint16_t* src = reinterpret_cast<const uint16_t*>(indexData);
                primitiveIndices.assign(src, src + indexCount);
            }
            else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                const uint32_t* src = reinterpret_cast<const uint32_t*>(indexData);
                primitiveIndices.assign(src, src + indexCount);
            }

            // Get vertex attributes
            const float* positionPtr = nullptr;
            const float* normalPtr = nullptr;
            const float* texCoordPtr = nullptr;
            size_t vertexCount = 0;

            if (primitive.attributes.find("POSITION") != primitive.attributes.end()) {
                const auto& accessor = model.accessors[primitive.attributes.at("POSITION")];
                const auto& bufferView = model.bufferViews[accessor.bufferView];
                const auto& buffer = model.buffers[bufferView.buffer];
                positionPtr = reinterpret_cast<const float*>(buffer.data.data() + bufferView.byteOffset + accessor.byteOffset);
                vertexCount = accessor.count;
            }

            if (primitive.attributes.find("NORMAL") != primitive.attributes.end()) {
                const auto& accessor = model.accessors[primitive.attributes.at("NORMAL")];
                const auto& bufferView = model.bufferViews[accessor.bufferView];
                const auto& buffer = model.buffers[bufferView.buffer];
                normalPtr = reinterpret_cast<const float*>(buffer.data.data() + bufferView.byteOffset + accessor.byteOffset);
            }

            if (primitive.attributes.find("TEXCOORD_0") != primitive.attributes.end()) {
                const auto& accessor = model.accessors[primitive.attributes.at("TEXCOORD_0")];
                const auto& bufferView = model.bufferViews[accessor.bufferView];
                const auto& buffer = model.buffers[bufferView.buffer];
                texCoordPtr = reinterpret_cast<const float*>(buffer.data.data() + bufferView.byteOffset + accessor.byteOffset);
            }

            // Create vertices with transformations applied
            for (size_t i = 0; i < vertexCount; ++i) {
                Vertex vertex;

                // Transform position
                Vec3 pos(positionPtr[3 * i], positionPtr[3 * i + 1], positionPtr[3 * i + 2]);
                vertex.position = worldMatrix.transformPoint(pos);

                // Transform normal
                if (normalPtr) {
                    Vec3 norm(normalPtr[3 * i], normalPtr[3 * i + 1], normalPtr[3 * i + 2]);
                    // Apply inverse transpose transformation for normals
                    norm = Vec3(
                        inverseTranspose.m[0][0] * norm.x + inverseTranspose.m[1][0] * norm.y + inverseTranspose.m[2][0] * norm.z,
                        inverseTranspose.m[0][1] * norm.x + inverseTranspose.m[1][1] * norm.y + inverseTranspose.m[2][1] * norm.z,
                        inverseTranspose.m[0][2] * norm.x + inverseTranspose.m[1][2] * norm.y + inverseTranspose.m[2][2] * norm.z
                    );
                    vertex.normal = norm.normalized();
                }
                else {
                    vertex.normal = Vec3{ 0.0f, 0.0f, 0.0f };
                }

                if (texCoordPtr) {
                    vertex.texCoord[0] = texCoordPtr[2 * i];
                    vertex.texCoord[1] = texCoordPtr[2 * i + 1];
                }
                else {
                    vertex.texCoord[0] = 0.0f;
                    vertex.texCoord[1] = 0.0f;
                }

                vertices.push_back(vertex);
            }

            // Adjust indices and add to global list
            for (uint32_t index : primitiveIndices) {
                indices.push_back(vertexOffset + index);
            }

            // Process material
            Face face;
            // Set default values
            face.albedo = Vec3{ 0.8f, 0.8f, 0.8f };
            face.emission = Vec3{ 0.0f, 0.0f, 0.0f };
            face.diffuseTextureID = -1;
            face.material_type = 0;
            face.roughness = 1.0f;
            face.ior = 1.5f;

            if (primitive.material >= 0) {
                const auto& material = model.materials[primitive.material];
                const auto& pbr = material.pbrMetallicRoughness;

                // Albedo
                if (pbr.baseColorFactor.size() >= 3) {
                    face.albedo = Vec3{
                        static_cast<float>(pbr.baseColorFactor[0]),
                        static_cast<float>(pbr.baseColorFactor[1]),
                        static_cast<float>(pbr.baseColorFactor[2])
                    };
                }

                // Emission
                if (material.emissiveFactor.size() >= 3) {
                    face.emission = Vec3{
                        static_cast<float>(material.emissiveFactor[0]),
                        static_cast<float>(material.emissiveFactor[1]),
                        static_cast<float>(material.emissiveFactor[2])
                    };
                }

                // Diffuse texture
                if (pbr.baseColorTexture.index >= 0) {
                    const auto& texture = model.textures[pbr.baseColorTexture.index];
                    const auto& image = model.images[texture.source];

                    std::string uri = image.uri;
                    std::filesystem::path texturePath(uri);
                    std::string absolutePath;

                    if (texturePath.is_absolute()) {
                        absolutePath = uri;
                    }
                    else {
                        absolutePath = (std::filesystem::path(modelDir) / uri).string();
                    }

                    if (textureIndexMap.find(absolutePath) == textureIndexMap.end()) {
                        textureIndexMap[absolutePath] = static_cast<int>(textureFiles.size());
                        textureFiles.push_back(absolutePath);
                    }
                    face.diffuseTextureID = textureIndexMap[absolutePath];
                }

                // Roughness
                face.roughness = static_cast<float>(pbr.roughnessFactor);

                // IOR from extension
                if (material.extensions.find("KHR_materials_ior") != material.extensions.end()) {
                    const auto& iorExtension = material.extensions.at("KHR_materials_ior");
                    if (iorExtension.Has("ior")) {
                        face.ior = static_cast<float>(iorExtension.Get("ior").Get<double>());
                    }
                }
            }
            faces.push_back(face);

            vertexOffset += vertexCount;
        }
    }

    // Process child nodes
    for (int childIndex : node.children) {
        processNode(
            model, model.nodes[childIndex], worldMatrix,
            vertices, indices, faces, textureIndexMap, textureFiles,
            modelDir, vertexOffset
        );
    }
}

void loadFromFile(
    std::vector<Vertex>& vertices,
    std::vector<uint32_t>& indices,
    std::vector<Face>& faces,
    std::vector<std::string>& textureFiles,
    const std::string& modelPath) {

    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, modelPath);
    if (!warn.empty()) printf("Warn: %s\n", warn.c_str());
    if (!err.empty()) printf("Err: %s\n", err.c_str());
    if (!ret) throw std::runtime_error("Failed to load glTF model");

    // Extract the directory from the model path
    std::filesystem::path modelFilePath(modelPath);
    std::string modelDir = modelFilePath.parent_path().string();

    std::unordered_map<std::string, int> textureIndexMap;
    uint32_t vertexOffset = 0;

    // Start processing from the scene nodes
    const auto& scene = model.scenes[model.defaultScene];
    Mat4 identityMatrix = Mat4::identity();

    for (int nodeIndex : scene.nodes) {
        processNode(
            model, model.nodes[nodeIndex], identityMatrix,
            vertices, indices, faces, textureIndexMap, textureFiles,
            modelDir, vertexOffset
        );
    }
}

std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open file: " + filename);
    }

    size_t fileSize = file.tellg();
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}