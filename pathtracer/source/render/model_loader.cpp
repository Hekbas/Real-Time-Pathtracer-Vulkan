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
    uint32_t& vertexOffset)
{
    // Node transform
    Mat4 nodeMatrix = getNodeMatrix(node);
    Mat4 worldMatrix = parentMatrix * nodeMatrix;

    Mat3 worldMatrix3x3;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            worldMatrix3x3.m[i][j] = worldMatrix.m[i][j];
    Mat3 inverseTranspose = worldMatrix3x3.inverse();

    // Mesh
    if (node.mesh >= 0) {
        const auto& mesh = model.meshes[node.mesh];

        for (const auto& primitive : mesh.primitives) {
            if (primitive.mode != TINYGLTF_MODE_TRIANGLES) continue;

            // --- Indices ---
            const auto& indexAccessor = model.accessors[primitive.indices];
            const auto& indexBufferView = model.bufferViews[indexAccessor.bufferView];
            const auto& indexBuffer = model.buffers[indexBufferView.buffer];
            const uint8_t* indexData = indexBuffer.data.data() + indexBufferView.byteOffset + indexAccessor.byteOffset;
            uint32_t indexCount = indexAccessor.count;

            std::vector<uint32_t> primitiveIndices;
            primitiveIndices.reserve(indexCount);
            if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                const uint16_t* src = reinterpret_cast<const uint16_t*>(indexData);
                primitiveIndices.assign(src, src + indexCount);
            }
            else if (indexAccessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                const uint32_t* src = reinterpret_cast<const uint32_t*>(indexData);
                primitiveIndices.assign(src, src + indexCount);
            }

            // --- Attributes ---
            const float* positionPtr = nullptr;
            const float* normalPtr = nullptr;
            const float* texCoordPtr = nullptr;
            const float* tangentPtr = nullptr;
            size_t vertexCount = 0;

            if (primitive.attributes.count("POSITION")) {
                const auto& accessor = model.accessors.at(primitive.attributes.at("POSITION"));
                const auto& bufferView = model.bufferViews[accessor.bufferView];
                const auto& buffer = model.buffers[bufferView.buffer];
                positionPtr = reinterpret_cast<const float*>(buffer.data.data() + bufferView.byteOffset + accessor.byteOffset);
                vertexCount = accessor.count;
            }
            if (primitive.attributes.count("NORMAL")) {
                const auto& accessor = model.accessors.at(primitive.attributes.at("NORMAL"));
                const auto& bufferView = model.bufferViews[accessor.bufferView];
                const auto& buffer = model.buffers[bufferView.buffer];
                normalPtr = reinterpret_cast<const float*>(buffer.data.data() + bufferView.byteOffset + accessor.byteOffset);
            }
            if (primitive.attributes.count("TEXCOORD_0")) {
                const auto& accessor = model.accessors.at(primitive.attributes.at("TEXCOORD_0"));
                const auto& bufferView = model.bufferViews[accessor.bufferView];
                const auto& buffer = model.buffers[bufferView.buffer];
                texCoordPtr = reinterpret_cast<const float*>(buffer.data.data() + bufferView.byteOffset + accessor.byteOffset);
            }
            if (primitive.attributes.count("TANGENT")) {
                const auto& accessor = model.accessors.at(primitive.attributes.at("TANGENT"));
                const auto& bufferView = model.bufferViews[accessor.bufferView];
                const auto& buffer = model.buffers[bufferView.buffer];
                tangentPtr = reinterpret_cast<const float*>(buffer.data.data() + bufferView.byteOffset + accessor.byteOffset);
            }

            // --- Tangent computation if missing ---
            std::vector<Vec3> tempTangents;
            if (!tangentPtr && vertexCount > 0) {
                tempTangents.assign(vertexCount, Vec3{ 0,0,0 });
                for (size_t t = 0; t + 2 < primitiveIndices.size(); t += 3) {
                    uint32_t ia = primitiveIndices[t + 0];
                    uint32_t ib = primitiveIndices[t + 1];
                    uint32_t ic = primitiveIndices[t + 2];
                    Vec3 p0(positionPtr[3 * ia + 0], positionPtr[3 * ia + 1], positionPtr[3 * ia + 2]);
                    Vec3 p1(positionPtr[3 * ib + 0], positionPtr[3 * ib + 1], positionPtr[3 * ib + 2]);
                    Vec3 p2(positionPtr[3 * ic + 0], positionPtr[3 * ic + 1], positionPtr[3 * ic + 2]);
                    Vec2 uv0(texCoordPtr ? texCoordPtr[2 * ia + 0] : 0, texCoordPtr ? texCoordPtr[2 * ia + 1] : 0);
                    Vec2 uv1(texCoordPtr ? texCoordPtr[2 * ib + 0] : 0, texCoordPtr ? texCoordPtr[2 * ib + 1] : 0);
                    Vec2 uv2(texCoordPtr ? texCoordPtr[2 * ic + 0] : 0, texCoordPtr ? texCoordPtr[2 * ic + 1] : 0);
                    Vec3 e1 = p1 - p0;
                    Vec3 e2 = p2 - p0;
                    Vec2 duv1 = uv1 - uv0;
                    Vec2 duv2 = uv2 - uv0;
                    float r = duv1.x * duv2.y - duv1.y * duv2.x;
                    if (fabs(r) < 1e-8f) continue;
                    float invR = 1.0f / r;
                    Vec3 tangent = (e1 * duv2.y - e2 * duv1.y) * invR;
                    tempTangents[ia] += tangent;
                    tempTangents[ib] += tangent;
                    tempTangents[ic] += tangent;
                }
            }

            // --- Push vertices ---
            for (size_t i = 0; i < vertexCount; ++i) {
                Vertex v{};
                // pos
                Vec3 pos(positionPtr[3 * i + 0], positionPtr[3 * i + 1], positionPtr[3 * i + 2]);
                Vec3 worldPos = worldMatrix.transformPoint(pos);
                v.position.x = worldPos.x; v.position.y = worldPos.y; v.position.z = worldPos.z;
                // normal
                if (normalPtr) {
                    Vec3 n(normalPtr[3 * i + 0], normalPtr[3 * i + 1], normalPtr[3 * i + 2]);
                    n = inverseTranspose * n;
                    v.normal.x = n.x; v.normal.y = n.y; v.normal.z = n.z;
                }
                // texcoord
                if (texCoordPtr) {
                    v.texCoord[0] = texCoordPtr[2 * i + 0];
                    v.texCoord[1] = texCoordPtr[2 * i + 1];
                }
                // tangent
                if (tangentPtr) {
                    v.tangent.x = tangentPtr[4 * i + 0];
                    v.tangent.y = tangentPtr[4 * i + 1];
                    v.tangent.z = tangentPtr[4 * i + 2];
                }
                else if (!tempTangents.empty()) {
                    Vec3 T = tempTangents[i];
                    Vec3 N(v.normal.x, v.normal.y, v.normal.z);
                    if (T.length() > 1e-6f) T = normalize(T - N * dot(N, T));
                    else {
                        Vec3 up = fabs(N.y) < 0.999f ? Vec3{ 0,1,0 } : Vec3{ 1,0,0 };
                        T = normalize(cross(up, N));
                    }
                    v.tangent.x = T.x; v.tangent.y = T.y; v.tangent.z = T.z;
                }
                else {
                    v.tangent.x = 1; v.tangent.y = 0; v.tangent.z = 0;
                }
                vertices.push_back(v);
            }

            // --- Indices (global offset) ---
            for (uint32_t idx : primitiveIndices)
                indices.push_back(vertexOffset + idx);

            // --- Material baseFace ---
            Face baseFace{};
            baseFace.albedo.x = 0.8f; baseFace.albedo.y = 0.8f; baseFace.albedo.z = 0.8f;
            baseFace.emission.x = baseFace.emission.y = baseFace.emission.z = 0.0f;
            baseFace.diffuseTextureID = uint32_t(-1);
            baseFace.material_type = 0;
            baseFace.roughness = 1.0f;
            baseFace.ior = 1.5f;
            baseFace.metallic = 0.0f;
            baseFace.alpha = 1.0f;
            baseFace.area = 0.0f;
            baseFace.metalRoughTextureID = uint32_t(-1);
            baseFace.normalTextureID = uint32_t(-1);
			baseFace.pad0 = 0.0f;

            if (primitive.material >= 0) {
                const auto& material = model.materials[primitive.material];
                const auto& pbr = material.pbrMetallicRoughness;

                if (pbr.baseColorFactor.size() >= 3) {
                    baseFace.albedo.x = (float)pbr.baseColorFactor[0];
                    baseFace.albedo.y = (float)pbr.baseColorFactor[1];
                    baseFace.albedo.z = (float)pbr.baseColorFactor[2];
                }
                if (pbr.baseColorFactor.size() == 4)
                    baseFace.alpha = (float)pbr.baseColorFactor[3];

                if (material.emissiveFactor.size() == 3) {
                    baseFace.emission.x = (float)material.emissiveFactor[0];
                    baseFace.emission.y = (float)material.emissiveFactor[1];
                    baseFace.emission.z = (float)material.emissiveFactor[2];
                }

                // baseColorTexture
                if (pbr.baseColorTexture.index >= 0) {
                    const auto& tex = model.textures[pbr.baseColorTexture.index];
                    const auto& img = model.images[tex.source];
                    std::string path = (std::filesystem::path(modelDir) / img.uri).string();
                    if (!textureIndexMap.count(path)) {
                        textureIndexMap[path] = (int)textureFiles.size();
                        textureFiles.push_back(path);
                    }
                    baseFace.diffuseTextureID = textureIndexMap[path];
                }

                baseFace.roughness = (float)pbr.roughnessFactor;
                baseFace.metallic = (float)pbr.metallicFactor;

                // metallicRoughnessTexture
                if (pbr.metallicRoughnessTexture.index >= 0) {
                    const auto& tex = model.textures[pbr.metallicRoughnessTexture.index];
                    const auto& img = model.images[tex.source];
                    std::string path = (std::filesystem::path(modelDir) / img.uri).string();
                    if (!textureIndexMap.count(path)) {
                        textureIndexMap[path] = (int)textureFiles.size();
                        textureFiles.push_back(path);
                    }
                    baseFace.metalRoughTextureID = textureIndexMap[path];
                }

                // normalTexture
                if (material.normalTexture.index >= 0) {
                    const auto& tex = model.textures[material.normalTexture.index];
                    const auto& img = model.images[tex.source];
                    std::string path = (std::filesystem::path(modelDir) / img.uri).string();
                    if (!textureIndexMap.count(path)) {
                        textureIndexMap[path] = (int)textureFiles.size();
                        textureFiles.push_back(path);
                    }
                    baseFace.normalTextureID = textureIndexMap[path];
                }

                // IOR (extension)
                if (material.extensions.count("KHR_materials_ior")) {
                    const auto& ext = material.extensions.at("KHR_materials_ior");
                    if (ext.Has("ior"))
                        baseFace.ior = (float)ext.Get("ior").Get<double>();
                }
            }

            // --- Faces: one per triangle ---
            size_t triCount = primitiveIndices.size() / 3;
            for (size_t t = 0; t < triCount; t++) {
                uint32_t ia = vertexOffset + primitiveIndices[3 * t + 0];
                uint32_t ib = vertexOffset + primitiveIndices[3 * t + 1];
                uint32_t ic = vertexOffset + primitiveIndices[3 * t + 2];
                Vec3 p0(vertices[ia].position.x, vertices[ia].position.y, vertices[ia].position.z);
                Vec3 p1(vertices[ib].position.x, vertices[ib].position.y, vertices[ib].position.z);
                Vec3 p2(vertices[ic].position.x, vertices[ic].position.y, vertices[ic].position.z);
                float area = 0.5f * cross(p1 - p0, p2 - p0).length();
                baseFace.area = area;
                faces.push_back(baseFace);
            }

            vertexOffset += (uint32_t)vertexCount;
        }
    }

    for (int child : node.children)
        processNode(model, model.nodes[child], worldMatrix,
            vertices, indices, faces,
            textureIndexMap, textureFiles, modelDir, vertexOffset);
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