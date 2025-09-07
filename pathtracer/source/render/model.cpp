//#include "model.h"
//
//#include <iostream>
//#include <stdexcept>
//
//
//Model::Model(Context& context, const std::string& modelPath)
//    : context(context) {
//    loadModel(modelPath);
//    createMaterials();
//    createBuffers();
//    loadTextures();
//}
//
//// Move constructor
//Model::Model(Model&& other) noexcept
//    : context(other.context),
//    vertices(std::move(other.vertices)),
//    indices(std::move(other.indices)),
//    faces(std::move(other.faces)),
//    textureFiles(std::move(other.textureFiles)),
//    textures(std::move(other.textures)),
//    materials(std::move(other.materials)),
//    vertexBuffer(std::move(other.vertexBuffer)),
//    indexBuffer(std::move(other.indexBuffer)),
//    materialBuffer(std::move(other.materialBuffer)) {
//}
//
//// Move assignment operator
//Model& Model::operator=(Model&& other) noexcept {
//    if (this != &other) {
//        vertices = std::move(other.vertices);
//        indices = std::move(other.indices);
//        faces = std::move(other.faces);
//        textureFiles = std::move(other.textureFiles);
//        textures = std::move(other.textures);
//        materials = std::move(other.materials);
//        vertexBuffer = std::move(other.vertexBuffer);
//        indexBuffer = std::move(other.indexBuffer);
//        materialBuffer = std::move(other.materialBuffer);
//    }
//    return *this;
//}
//
//Model::~Model() {
//    // Buffers and textures will be automatically cleaned up by their destructors
//}
//
//void Model::draw(vk::CommandBuffer commandBuffer) const {
//    if (vertices.empty()) return;
//
//    vk::Buffer vertexBuffers[] = { *vertexBuffer.buffer };
//    vk::DeviceSize offsets[] = { 0 };
//    commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, offsets);
//
//    if (!indices.empty()) {
//        commandBuffer.bindIndexBuffer(*indexBuffer.buffer, 0, vk::IndexType::eUint32);
//        commandBuffer.drawIndexed(static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);
//    }
//    else {
//        commandBuffer.draw(static_cast<uint32_t>(vertices.size()), 1, 0, 0);
//    }
//}
//
//void Model::loadModel(const std::string& modelPath) {
//    try {
//        loadFromFile(vertices, indices, faces, textureFiles, modelPath);
//        std::cout << "Loaded model: " << modelPath << std::endl;
//        std::cout << "Vertices: " << vertices.size() << std::endl;
//        std::cout << "Indices: " << indices.size() << std::endl;
//        std::cout << "Faces: " << faces.size() << std::endl;
//        std::cout << "Textures: " << textureFiles.size() << std::endl;
//    }
//    catch (const std::exception& e) {
//        std::cerr << "Failed to load model: " << e.what() << std::endl;
//        throw;
//    }
//}
//
//void Model::loadTextures() {
//    for (const auto& textureFile : textureFiles) {
//        try {
//            textures.push_back(createTexture(context, textureFile));
//            std::cout << "Loaded texture: " << textureFile << std::endl;
//        }
//        catch (const std::exception& e) {
//            std::cerr << "Failed to load texture: " << textureFile << " - " << e.what() << std::endl;
//            // Create a default texture if loading fails
//            std::vector<uint8_t> defaultTexData(4 * 4 * 4, 255); // 4x4 white texture
//            textures.push_back(createTextureFromData(
//                context, 4, 4, vk::Format::eR8G8B8A8Unorm, defaultTexData.data()
//            ));
//        }
//    }
//}
//
//void Model::createBuffers() {
//    // Create vertex buffer
//    if (!vertices.empty()) {
//        vertexBuffer = Buffer(
//            context,
//            Buffer::Type::AccelInput,
//            vertices.size() * sizeof(Vertex),
//            vertices.data()
//        );
//    }
//
//    // Create index buffer
//    if (!indices.empty()) {
//        indexBuffer = Buffer(
//            context,
//            Buffer::Type::AccelInput,
//            indices.size() * sizeof(uint32_t),
//            indices.data()
//        );
//    }
//
//    // Create material buffer
//    if (!materials.empty()) {
//        // Create packed materials for GPU
//        std::vector<Material::Packed> packedMaterials;
//        packedMaterials.reserve(materials.size());
//
//        for (const auto& mat : materials) {
//            packedMaterials.push_back(mat.getPacked());
//        }
//
//        materialBuffer = Buffer(
//            context,
//            Buffer::Type::Storage,
//            packedMaterials.size() * sizeof(Material::Packed),
//            packedMaterials.data()
//        );
//    }
//}
//
//void Model::createMaterials() {
//    materials.resize(faces.size());
//
//    for (size_t i = 0; i < faces.size(); i++) {
//        const auto& face = faces[i];
//
//        // Create material based on face properties
//        Material mat;
//
//        if (face.emission[0] > 0.0f || face.emission[1] > 0.0f || face.emission[2] > 0.0f) {
//            // Emissive material
//            mat.type = MaterialType::Emissive;
//            mat.emission = Vec3(face.emission[0], face.emission[1], face.emission[2]);
//        }
//        else {
//            // Default to Lambertian
//            mat.type = MaterialType::Lambertian;
//            mat.albedo = Vec3(face.diffuse[0], face.diffuse[1], face.diffuse[2]);
//        }
//
//        mat.textureId = face.diffuseTextureID;
//        materials[i] = mat;
//    }
//}