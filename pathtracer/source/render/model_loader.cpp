#include "model_loader.h"
#include <fstream>
#include <stdexcept>
#include <iostream>
#include <unordered_map>
#include <vector>
#include <string>
#include <map>

#define TINYGLTF_IMPLEMENTATION
//#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <tiny_gltf.h>

Vec3 calcNormal(Vec3 v0, Vec3 v1, Vec3 v2) {
    Vec3 edge1 = v1 - v0;
    Vec3 edge2 = v2 - v0;
    return normalize(cross(edge1, edge2));
}


// Basic base64 decoder for data: URIs
static std::vector<unsigned char> base64_decode(const std::string& in) {
    static const std::string chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[(unsigned char)chars[i]] = i;

    std::vector<unsigned char> out;
    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back((unsigned char)((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

static std::string guess_ext_from_mime(const std::string& mime) {
    if (mime == "image/png") return ".png";
    if (mime == "image/jpeg") return ".jpg";
    if (mime == "image/jpg") return ".jpg";
    if (mime == "image/bmp") return ".bmp";
    return ".bin";
}

static std::string write_image_file_from_encoded_bytes(const std::filesystem::path& outDir,
    const std::string& baseName,
    const std::vector<unsigned char>& bytes,
    const std::string& ext)
{
    std::filesystem::create_directories(outDir);
    std::string filename = (outDir / (baseName + ext)).string();
    std::ofstream ofs(filename, std::ios::binary);
    ofs.write(reinterpret_cast<const char*>(bytes.data()), (std::streamsize)bytes.size());
    ofs.close();
    return filename;
}

static std::string write_image_file_from_raw_pixels(const std::filesystem::path& outDir,
    const std::string& baseName,
    const tinygltf::Image& image) {
    // image.image contains raw pixel data (width*height*component)
    std::filesystem::create_directories(outDir);
    std::string filename = (outDir / (baseName + ".png")).string();
    // Use stb_image_write (included via STB_IMAGE_WRITE_IMPLEMENTATION in this file)
    // Note: stb_image_write.h is included transitively via tiny_gltf/stb defines; if not, make sure it's available.
    stbi_write_png(filename.c_str(),
        image.width,
        image.height,
        image.component,
        image.image.data(),
        image.width * image.component);
    return filename;
}

static void read_accessor_floats(const tinygltf::Model& model,
    const tinygltf::Accessor& accessor,
    int components,
    std::vector<float>& out) {
    out.resize(accessor.count * components);
    if (accessor.bufferView < 0) return;
    const tinygltf::BufferView& bv = model.bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buf = model.buffers[bv.buffer];
    size_t offset = (size_t)bv.byteOffset + (size_t)accessor.byteOffset;
    size_t stride = bv.byteStride ? bv.byteStride : (size_t)(components * sizeof(float));
    const unsigned char* data = buf.data.data() + offset;
    for (size_t i = 0; i < accessor.count; ++i) {
        const float* f = reinterpret_cast<const float*>(data + i * stride);
        for (int c = 0; c < components; ++c) {
            out[i * components + c] = f[c];
        }
    }
}

static void read_indices(const tinygltf::Model& model,
    const tinygltf::Accessor& accessor,
    std::vector<uint32_t>& out) {
    out.clear();
    if (accessor.bufferView < 0) return;
    const tinygltf::BufferView& bv = model.bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buf = model.buffers[bv.buffer];
    size_t offset = (size_t)bv.byteOffset + (size_t)accessor.byteOffset;
    const unsigned char* data = buf.data.data() + offset;

    if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
        const uint16_t* p = reinterpret_cast<const uint16_t*>(data);
        for (size_t i = 0; i < accessor.count; ++i) out.push_back((uint32_t)p[i]);
    }
    else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
        const uint32_t* p = reinterpret_cast<const uint32_t*>(data);
        for (size_t i = 0; i < accessor.count; ++i) out.push_back((uint32_t)p[i]);
    }
    else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
        for (size_t i = 0; i < accessor.count; ++i) out.push_back((uint32_t)p[i]);
    }
    else {
        // unsupported index type
        throw std::runtime_error("Unsupported index component type in glTF.");
    }
}

void loadFromFile(
    std::vector<Vertex>& outVertices,
    std::vector<uint32_t>& outIndices,
    std::vector<Material>& outMaterials,
    std::vector<std::string>& outTextureFiles,
    std::vector<uint32_t>& outMaterialIndexPerTriangle,
    const std::string& filename)
{
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err, warn;
    bool ret = loader.LoadASCIIFromFile(&model, &err, &warn, filename);
    if (!ret) {
        ret = loader.LoadBinaryFromFile(&model, &err, &warn, filename);
    }
    if (!warn.empty()) std::cerr << "gltf warn: " << warn << "\n";
    if (!err.empty()) std::cerr << "gltf err: " << err << "\n";
    if (!ret) throw std::runtime_error("Failed to load glTF: " + filename);

    std::filesystem::path modelDir = std::filesystem::path(filename).parent_path();

    // Build texture list (deduplicate)
    std::unordered_map<std::string, int> textureLookup;

    auto ensure_texture = [&](const std::string& path) -> int {
        auto it = textureLookup.find(path);
        if (it != textureLookup.end()) return it->second;
        int idx = (int)outTextureFiles.size();
        outTextureFiles.push_back(path);
        textureLookup[path] = idx;
        return idx;
    };

    // Export images to files (if needed) and build a vector of image->file path
    std::vector<std::string> imagePaths(model.images.size());
    for (size_t i = 0; i < model.images.size(); ++i) {
        const tinygltf::Image& img = model.images[i];
        std::string base = "gltf_img_" + std::to_string(i);
        if (!img.uri.empty()) {
            // external file or data URI
            if (img.uri.rfind("data:", 0) == 0) {
                // data URI like data:<mime>;base64,<data>
                size_t comma = img.uri.find(',');
                std::string meta = img.uri.substr(5, comma - 5);
                std::string dataPart = img.uri.substr(comma + 1);
                bool isBase64 = meta.find("base64") != std::string::npos;
                std::string mime = meta.substr(0, meta.find(';'));
                std::string ext = guess_ext_from_mime(mime);
                if (isBase64) {
                    std::vector<unsigned char> bytes = base64_decode(dataPart);
                    imagePaths[i] = write_image_file_from_encoded_bytes(modelDir, base, bytes, ext);
                }
                else {
                    // fallback: write raw bytes
                    std::vector<unsigned char> bytes(dataPart.begin(), dataPart.end());
                    imagePaths[i] = write_image_file_from_encoded_bytes(modelDir, base, bytes, ext);
                }
            }
            else {
                // normal external URI -> resolve path relative to model dir
                std::filesystem::path p(img.uri);
                if (p.is_relative()) p = modelDir / p;
                imagePaths[i] = p.string();
            }
        }
        else {
            // No uri: maybe decoded pixels are in img.image with width/height
            if (!img.image.empty() && img.width > 0 && img.height > 0) {
                imagePaths[i] = write_image_file_from_raw_pixels(modelDir, base, img);
            }
            else if (!img.image.empty()) {
                // try to write raw bytes as-is
                std::string ext = guess_ext_from_mime(img.mimeType);
                imagePaths[i] = write_image_file_from_encoded_bytes(modelDir, base, img.image, ext);
            }
            else {
                imagePaths[i] = ""; // nothing we can do
            }
        }
    }

    // Walk meshes/primitives, create vertices/indices and per-primitive material
    outVertices.clear();
    outIndices.clear();
    outMaterials.clear();

    for (const tinygltf::Mesh& mesh : model.meshes) {
        for (const tinygltf::Primitive& prim : mesh.primitives) {
            // only triangles supported here
            if (prim.mode != TINYGLTF_MODE_TRIANGLES) continue;

            // read positions
            if (prim.attributes.find("POSITION") == prim.attributes.end()) continue;
            const tinygltf::Accessor& posAccessor = model.accessors[prim.attributes.at("POSITION")];
            std::vector<float> positions;
            read_accessor_floats(model, posAccessor, 3, positions);
            size_t vertCount = posAccessor.count;

            // optional attributes
            std::vector<float> normals;
            if (prim.attributes.find("NORMAL") != prim.attributes.end()) {
                read_accessor_floats(model, model.accessors[prim.attributes.at("NORMAL")], 3, normals);
            }
            std::vector<float> uvs;
            if (prim.attributes.find("TEXCOORD_0") != prim.attributes.end()) {
                read_accessor_floats(model, model.accessors[prim.attributes.at("TEXCOORD_0")], 2, uvs);
            }
            std::vector<float> tangents;
            if (prim.attributes.find("TANGENT") != prim.attributes.end()) {
                read_accessor_floats(model, model.accessors[prim.attributes.at("TANGENT")], 4, tangents);
            }

            // indices
            std::vector<uint32_t> primIndices;
            if (prim.indices >= 0) {
                read_indices(model, model.accessors[prim.indices], primIndices);
            }
            else {
                // no indices -> build a default index list
                primIndices.resize(vertCount);
                for (size_t i = 0; i < vertCount; ++i) primIndices[i] = (uint32_t)i;
            }

            // create material entry for this primitive
            Material mat;
            if (prim.material >= 0 && prim.material < (int)model.materials.size()) {
                const tinygltf::Material& gmat = model.materials[prim.material];
                if (gmat.values.find("baseColorFactor") != gmat.values.end()) {
                    auto v = gmat.values.at("baseColorFactor").ColorFactor();
                    mat.baseColorFactor = { (float)v[0], (float)v[1], (float)v[2], (float)v[3] };
                }
                if (gmat.additionalValues.find("metallicFactor") != gmat.additionalValues.end())
                    mat.metallicFactor = (float)gmat.additionalValues.at("metallicFactor").Factor();
                if (gmat.additionalValues.find("roughnessFactor") != gmat.additionalValues.end())
                    mat.roughnessFactor = (float)gmat.additionalValues.at("roughnessFactor").Factor();

                // pbrMetallicRoughness textures:
                if (gmat.values.find("baseColorTexture") != gmat.values.end()) {
                    int texIndex = gmat.values.at("baseColorTexture").TextureIndex();
                    if (texIndex >= 0 && texIndex < (int)model.textures.size()) {
                        int srcImg = model.textures[texIndex].source;
                        if (srcImg >= 0 && srcImg < (int)imagePaths.size() && !imagePaths[srcImg].empty()) {
                            mat.baseColorTex = ensure_texture(imagePaths[srcImg]);
                        }
                    }
                }
                if (gmat.values.find("metallicRoughnessTexture") != gmat.values.end()) {
                    int texIndex = gmat.values.at("metallicRoughnessTexture").TextureIndex();
                    if (texIndex >= 0 && texIndex < (int)model.textures.size()) {
                        int srcImg = model.textures[texIndex].source;
                        if (srcImg >= 0 && srcImg < (int)imagePaths.size() && !imagePaths[srcImg].empty()) {
                            mat.metallicRoughnessTex = ensure_texture(imagePaths[srcImg]);
                        }
                    }
                }
                if (gmat.additionalValues.find("emissiveTexture") != gmat.additionalValues.end()) {
                    int texIndex = gmat.additionalValues.at("emissiveTexture").TextureIndex();
                    if (texIndex >= 0 && texIndex < (int)model.textures.size()) {
                        int srcImg = model.textures[texIndex].source;
                        if (srcImg >= 0 && srcImg < (int)imagePaths.size() && !imagePaths[srcImg].empty()) {
                            mat.emissiveTex = ensure_texture(imagePaths[srcImg]);
                        }
                    }
                }
                if (gmat.additionalValues.find("normalTexture") != gmat.additionalValues.end()) {
                    int texIndex = gmat.additionalValues.at("normalTexture").TextureIndex();
                    if (texIndex >= 0 && texIndex < (int)model.textures.size()) {
                        int srcImg = model.textures[texIndex].source;
                        if (srcImg >= 0 && srcImg < (int)imagePaths.size() && !imagePaths[srcImg].empty()) {
                            mat.normalTex = ensure_texture(imagePaths[srcImg]);
                        }
                    }
                }

                mat.doubleSided = gmat.doubleSided;
                if (gmat.alphaMode == "MASK") mat.alphaMode = 1;
                else if (gmat.alphaMode == "BLEND") mat.alphaMode = 2;
                else mat.alphaMode = 0;
            }

            // append vertices; remember vertex offset for indices
            uint32_t baseVertex = static_cast<uint32_t>(outVertices.size());
            for (size_t v = 0; v < vertCount; ++v) {
                Vertex outV;
                outV.pos = { positions[3 * v + 0], positions[3 * v + 1], positions[3 * v + 2] };
                if (!normals.empty()) outV.normal = { normals[3 * v + 0], normals[3 * v + 1], normals[3 * v + 2] };
                else outV.normal = { 0.0f, 0.0f, 1.0f };

                if (!tangents.empty()) outV.tangent = { tangents[4 * v + 0], tangents[4 * v + 1], tangents[4 * v + 2] };
                else outV.tangent = { 1.0f, 0.0f, 0.0f };

                // Fixed UV assignment
                if (!uvs.empty()) {
                    outV.uv[0] = uvs[2 * v + 0];
                    outV.uv[1] = uvs[2 * v + 1];
                }
                else {
                    outV.uv[0] = 0.0f;
                    outV.uv[1] = 0.0f;
                }

                outVertices.push_back(outV);
            }

            // push material first and then fill indices + per-triangle material index
            int materialIndex = static_cast<int>(outMaterials.size());
            outMaterials.push_back(mat); // material for this primitive

            // append indices (offset by baseVertex) and add material index per triangle
            for (size_t i = 0; i < primIndices.size(); ++i) {
                outIndices.push_back(baseVertex + primIndices[i]);

                // each 3 indices form a triangle -> push material index once per triangle
                if ((i % 3) == 2) {
                    outMaterialIndexPerTriangle.push_back(static_cast<uint32_t>(materialIndex));
                }
            }
        }
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