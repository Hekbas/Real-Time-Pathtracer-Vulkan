#include "model_loader.h"
#include <fstream>
#include <stdexcept>
#include <iostream>
#include <unordered_map>
#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#define MAT_LAMBERTIAN 0
#define MAT_METAL      1
#define MAT_DIELECTRIC 2

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
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    // Get the directory of the model file for loading materials/textures
    std::string modelDir = modelPath.substr(0, modelPath.find_last_of("/\\") + 1);

    // Load the OBJ file. The `true` argument tells it to load materials from the MTL file.
    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, modelPath.c_str(), modelDir.c_str(), true)) {
        throw std::runtime_error("TINYOBJLOADER: " + warn + err);
    }
    if (!warn.empty()) {
        std::cout << "TINYOBJLOADER WARNING: " << warn << std::endl;
    }

    // A map to keep track of loaded textures to avoid duplicates
    std::map<std::string, int> textureMap;

    // Loop over shapes
    for (const auto& shape : shapes) {
        // Loop over faces(triangles)
        size_t index_offset = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
            // We assume the model is triangulated
            int fv = 3;

            // This face's material properties
            Face face_props{};

            // Get the material ID for this face.
            int material_id = shape.mesh.material_ids[f];

            if (material_id != -1) {
                const auto& mat = materials[material_id];

                // 1. Set Albedo and Emission
                face_props.albedo = Vec3(mat.diffuse[0], mat.diffuse[1], mat.diffuse[2]);
                face_props.emission = Vec3(mat.emission[0], mat.emission[1], mat.emission[2]);

                // 2. Determine Material Type and Properties (The Heuristic)
                if (mat.transmittance[0] > 0.0f) { // Is it transparent?
                    face_props.material_type = MAT_DIELECTRIC;
                    face_props.ior = mat.ior; // Get index of refraction
                    face_props.roughness = 0.0f; // Dielectrics are smooth in this simple model
                }
                else if (mat.specular[0] > 0.5f && mat.diffuse[0] < 0.2f) { // Is it metallic?
                    face_props.material_type = MAT_METAL;
                    face_props.albedo = Vec3(mat.specular[0], mat.specular[1], mat.specular[2]); // Metals use specular color
                    // Map shininess (Ns) to roughness
                    face_props.roughness = std::max(0.0f, 1.0f - (mat.shininess / 1000.0f));
                    face_props.ior = 0.0f;
                }
                else { // Otherwise, it's diffuse
                    face_props.material_type = MAT_LAMBERTIAN;
                    face_props.roughness = 1.0f; // Lambertian is maximum roughness
                    face_props.ior = 0.0f;
                }

                // 3. Handle Textures
                if (!mat.diffuse_texname.empty()) {
                    std::string texPath = modelDir + mat.diffuse_texname;
                    if (textureMap.find(texPath) == textureMap.end()) {
                        // New texture, add it to our list
                        textureMap[texPath] = textureFiles.size();
                        textureFiles.push_back(texPath);
                    }
                    face_props.diffuseTextureID = textureMap[texPath];
                }
                else {
                    face_props.diffuseTextureID = -1;
                }

            }
            else {
                // No material assigned, use a default diffuse material
                face_props.material_type = MAT_LAMBERTIAN;
                face_props.albedo = Vec3(0.7f, 0.7f, 0.7f);
                face_props.emission = Vec3(0.0f, 0.0f, 0.0f);
                face_props.roughness = 1.0f;
                face_props.diffuseTextureID = -1;
                face_props.ior = 0.0f;
            }

            // This single 'Face' struct will apply to the whole triangle.
            // In Vulkan Ray Tracing, we access this via `gl_PrimitiveID`.
            faces.push_back(face_props);

            // Loop over vertices in the face
            for (size_t v = 0; v < fv; v++) {
                tinyobj::index_t idx = shape.mesh.indices[index_offset + v];
                Vertex vertex{};

                vertex.position = {
                    attrib.vertices[3 * idx.vertex_index + 0],
                    attrib.vertices[3 * idx.vertex_index + 1],
                    attrib.vertices[3 * idx.vertex_index + 2]
                };

                if (!attrib.normals.empty()) {
                    vertex.normal = {
                        attrib.normals[3 * idx.normal_index + 0],
                        attrib.normals[3 * idx.normal_index + 1],
                        attrib.normals[3 * idx.normal_index + 2]
                    };
                }

                if (!attrib.texcoords.empty()) {
                    vertex.texCoord[0] = attrib.texcoords[2 * idx.texcoord_index + 0];
                    vertex.texCoord[1] = 1.0f - attrib.texcoords[2 * idx.texcoord_index + 1];
                }

                vertices.push_back(vertex);
                indices.push_back(indices.size());
            }
            index_offset += fv;
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