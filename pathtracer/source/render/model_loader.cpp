#include "model_loader.h"
#include "math/vec2.h"
#include "math/vec3.h"
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
    // If a full 4x4 matrix is present, use fromGLTF which already handles column->row conversion
    if (!node.matrix.empty()) {
        return Mat4::fromGLTF(node.matrix.data());
    }

    Mat4 translation = Mat4::identity();
    Mat4 rotation = Mat4::identity();
    Mat4 scale = Mat4::identity();

    if (!node.translation.empty()) {
        translation = Mat4::translate(Vec3(
            static_cast<float>(node.translation[0]),
            static_cast<float>(node.translation[1]),
            static_cast<float>(node.translation[2])
        ));
    }

    if (!node.rotation.empty()) {
        rotation = Mat4::fromQuaternion(
            static_cast<float>(node.rotation[0]),
            static_cast<float>(node.rotation[1]),
            static_cast<float>(node.rotation[2]),
            static_cast<float>(node.rotation[3])
        );
    }

    if (!node.scale.empty()) {
        scale = Mat4::scale(Vec3(
            static_cast<float>(node.scale[0]),
            static_cast<float>(node.scale[1]),
            static_cast<float>(node.scale[2])
        ));
    }

    return  translation * rotation * scale;
}

// Recursive function to process nodes
void processNode(
    const tinygltf::Model& model,
    const tinygltf::Node& node,
    const Mat4& parentMatrix,
    std::vector<Vertex>& vertices,
    std::vector<uint32_t>& indices,
    std::vector<Material>& materials,
    std::vector<uint32_t>& faceMaterialIndices,
    std::map<int, uint32_t>& gltfMaterialMap,
    std::unordered_map<std::string, int>& textureIndexMap,
    std::vector<std::string>& textureFiles,
    const std::string& modelDir,
    uint32_t& vertexOffset)
{
    // Node transform
    Mat4 nodeMatrix = getNodeMatrix(node);
    Mat4 worldMatrix = parentMatrix * nodeMatrix;

    Mat3 inverseTranspose = worldMatrix.toMat3().inverse(); // for normals

    // Process mesh if present
    if (node.mesh >= 0) {
        const auto& mesh = model.meshes[node.mesh];

        for (const auto& primitive : mesh.primitives) {
            if (primitive.mode != TINYGLTF_MODE_TRIANGLES) continue;

            // --- Indices ---
            std::vector<uint32_t> primitiveIndices;
            if (primitive.indices >= 0) {
                const auto& accessor = model.accessors[primitive.indices];
                const auto& bufferView = model.bufferViews[accessor.bufferView];
                const auto& buffer = model.buffers[bufferView.buffer];
                const uint8_t* dataStart = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;

                primitiveIndices.resize(accessor.count);
                for (size_t i = 0; i < accessor.count; i++) {
                    if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                        primitiveIndices[i] = reinterpret_cast<const uint16_t*>(dataStart)[i];
                    }
                    else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                        primitiveIndices[i] = reinterpret_cast<const uint32_t*>(dataStart)[i];
                    }
                }
            }

            // --- Attributes ---
            auto readAttribute = [&](const std::string& name, std::vector<Vec3>& out) {
                if (!primitive.attributes.count(name)) return;
                const auto& accessor = model.accessors.at(primitive.attributes.at(name));
                const auto& bufferView = model.bufferViews[accessor.bufferView];
                const auto& buffer = model.buffers[bufferView.buffer];
                const uint8_t* dataStart = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
                size_t stride = bufferView.byteStride ? bufferView.byteStride : sizeof(float) * 3;
                out.resize(accessor.count);
                for (size_t i = 0; i < accessor.count; i++) {
                    const float* src = reinterpret_cast<const float*>(dataStart + i * stride);
                    out[i] = Vec3(src[0], src[1], src[2]);
                }
            };

            std::vector<Vec3> positions, normals, tangents;
            readAttribute("POSITION", positions);
            readAttribute("NORMAL", normals);
            readAttribute("TANGENT", tangents);

            // --- Texcoords (Vec2) ---
            std::vector<Vec2> texcoords;
            if (primitive.attributes.count("TEXCOORD_0")) {
                const auto& accessor = model.accessors.at(primitive.attributes.at("TEXCOORD_0"));
                const auto& bufferView = model.bufferViews[accessor.bufferView];
                const auto& buffer = model.buffers[bufferView.buffer];
                const uint8_t* dataStart = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
                size_t stride = bufferView.byteStride ? bufferView.byteStride : sizeof(float) * 2;
                texcoords.resize(accessor.count);
                for (size_t i = 0; i < accessor.count; i++) {
                    const float* src = reinterpret_cast<const float*>(dataStart + i * stride);
                    texcoords[i] = Vec2(src[0], src[1]);
                }
            }

            // --- Create vertices ---
            for (size_t i = 0; i < positions.size(); ++i) {
                Vertex v{};
                Vec3 p = positions[i];
                Vec3 n = normals.empty() ? Vec3{ 0,1,0 } : normals[i];
                Vec3 t = tangents.empty() ? Vec3{ 1,0,0 } : tangents[i];
                Vec2 uv = texcoords.empty() ? Vec2{ 0,0 } : texcoords[i];

                // Apply world transform
                Vec3 worldPos = worldMatrix.transformPoint(p);
                Vec3 worldNormal = inverseTranspose * n;
                Vec3 worldTangent = worldMatrix.toMat3() * t;

                v.position = worldPos;
                v.normal = worldNormal;
                v.tangent = worldTangent;
                v.texCoord[0] = uv.x;
                v.texCoord[1] = uv.y;

                vertices.push_back(v);
            }

            // --- Indices with vertexOffset ---
            for (uint32_t idx : primitiveIndices)
                indices.push_back(vertexOffset + idx);

            // --- Material processing ---
            Material baseMaterial{};
            baseMaterial.albedo = { 0.8f, 0.8f, 0.8f };
            baseMaterial.emission = { 0.0f, 0.0f, 0.0f };
            baseMaterial.diffuseTextureID = -1;
            baseMaterial.metalRoughTextureID = -1;
            baseMaterial.normalTextureID = -1;
            baseMaterial.roughness = 1.0f;
            baseMaterial.metallic = 0.0f;
            baseMaterial.ior = 1.5f;
            baseMaterial.alpha = 1.0f;
            baseMaterial.material_type = 0; // MAT_LAMBERTIAN

            uint32_t materialIndex = 0;
            if (primitive.material >= 0) {
                if (gltfMaterialMap.count(primitive.material)) {
                    materialIndex = gltfMaterialMap[primitive.material];
                }
                else {
                    const auto& mat = model.materials[primitive.material];
                    const auto& pbr = mat.pbrMetallicRoughness;

                    if (pbr.baseColorFactor.size() >= 3)
                        baseMaterial.albedo = { (float)pbr.baseColorFactor[0], (float)pbr.baseColorFactor[1], (float)pbr.baseColorFactor[2] };
                    if (pbr.baseColorFactor.size() == 4) baseMaterial.alpha = (float)pbr.baseColorFactor[3];

                    if (mat.emissiveFactor.size() == 3)
                        baseMaterial.emission = { (float)mat.emissiveFactor[0], (float)mat.emissiveFactor[1], (float)mat.emissiveFactor[2] };

                    // Textures
                    auto processTexture = [&](const tinygltf::TextureInfo& texInfo, int& texID) {
                        if (texInfo.index < 0) return;
                        const auto& tex = model.textures[texInfo.index];
                        const auto& img = model.images[tex.source];
                        std::string path = (std::filesystem::path(modelDir) / img.uri).string();
                        if (!textureIndexMap.count(path)) {
                            textureIndexMap[path] = (int)textureFiles.size();
                            textureFiles.push_back(path);
                        }
                        texID = textureIndexMap[path];
                    };

                    // overload for NormalTextureInfo
                    auto processNormalTexture = [&](const tinygltf::NormalTextureInfo& texInfo, int& texID) {
                        if (texInfo.index < 0) return;
                        const auto& tex = model.textures[texInfo.index];
                        const auto& img = model.images[tex.source];
                        std::string path = (std::filesystem::path(modelDir) / img.uri).string();
                        if (!textureIndexMap.count(path)) {
                            textureIndexMap[path] = (int)textureFiles.size();
                            textureFiles.push_back(path);
                        }
                        texID = textureIndexMap[path];
                        };

                    processTexture(pbr.baseColorTexture, baseMaterial.diffuseTextureID);
                    processTexture(pbr.metallicRoughnessTexture, baseMaterial.metalRoughTextureID);
                    processNormalTexture(mat.normalTexture, baseMaterial.normalTextureID);

                    if (mat.extensions.count("KHR_materials_ior")) {
                        const auto& ext = mat.extensions.at("KHR_materials_ior");
                        if (ext.Has("ior")) baseMaterial.ior = (float)ext.Get("ior").Get<double>();
                    }

                    materialIndex = (uint32_t)materials.size();
                    materials.push_back(baseMaterial);
                    gltfMaterialMap[primitive.material] = materialIndex;
                }
            }

            // --- Face material indices ---
            size_t triCount = primitiveIndices.size() / 3;
            for (size_t t = 0; t < triCount; t++) faceMaterialIndices.push_back(materialIndex);

            vertexOffset += (uint32_t)positions.size();
        }
    }

    // Recurse children
    for (int child : node.children)
        processNode(model, model.nodes[child], worldMatrix,
            vertices, indices, materials, faceMaterialIndices, gltfMaterialMap,
            textureIndexMap, textureFiles, modelDir, vertexOffset);
}



void loadFromFile(
    std::vector<Vertex>& vertices,
    std::vector<uint32_t>& indices,
    std::vector<Material>& materials,
    std::vector<uint32_t>& faceMaterialIndices,
    std::vector<std::string>& textureFiles,
    const std::string& modelPath)
{
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
    std::map<int, uint32_t> gltfMaterialMap;
    uint32_t vertexOffset = 0;

    // Start processing from the scene nodes
    const auto& scene = model.scenes[model.defaultScene];
    Mat4 identityMatrix = Mat4::identity();

    for (int nodeIndex : scene.nodes) {
        processNode(
            model, model.nodes[nodeIndex], identityMatrix,
            vertices, indices, materials, faceMaterialIndices, gltfMaterialMap,
            textureIndexMap, textureFiles, modelDir, vertexOffset
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