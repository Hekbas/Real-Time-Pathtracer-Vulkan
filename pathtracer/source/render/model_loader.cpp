#include "model_loader.h"
#include <fstream>
#include <stdexcept>
#include <iostream>
#include <unordered_map>
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

Vec3 calcNormal(Vec3 v0, Vec3 v1, Vec3 v2) {
    Vec3 edge1 = v1 - v0;
    Vec3 edge2 = v2 - v0;
    return normalize(cross(edge1, edge2));
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
        throw std::runtime_error("failed to open file: " + filename);
    }

    size_t fileSize = file.tellg();
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}