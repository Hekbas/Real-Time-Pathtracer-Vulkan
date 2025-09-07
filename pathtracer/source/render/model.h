//#pragma once
//
//#include "core/context.h"
//#include "core/buffer.h"
//#include "core/texture.h"
//#include "render/model_loader.h"
////#include "render/material.h"
//
//#include <vector>
//#include <string>
//
//class Model {
//public:
//    Model(Context& context, const std::string& modelPath);
//    ~Model();
//
//    Model(const Model&) = delete;
//    Model& operator=(const Model&) = delete;
//    Model(Model&& other) noexcept;
//    Model& operator=(Model&& other) noexcept;
//
//    void draw(vk::CommandBuffer commandBuffer) const;
//
//    const std::vector<Vertex>& getVertices() const { return vertices; }
//    const std::vector<uint32_t>& getIndices() const { return indices; }
//    const std::vector<Face>& getFaces() const { return faces; }
//    //const std::vector<Material>& getMaterials() const { return materials; }
//    const std::vector<Texture>& getTextures() const { return textures; }
//    const Buffer& getVertexBuffer() const { return vertexBuffer; }
//    const Buffer& getIndexBuffer() const { return indexBuffer; }
//    const Buffer& getMaterialBuffer() const { return materialBuffer; }
//
//private:
//    void loadModel(const std::string& modelPath);
//    void loadTextures();
//    void createBuffers();
//    void createMaterials();
//
//    Context& context;
//    std::vector<Vertex> vertices;
//    std::vector<uint32_t> indices;
//    std::vector<Face> faces;
//    std::vector<std::string> textureFiles;
//    std::vector<Texture> textures;
//    //std::vector<Material> materials;
//
//    Buffer vertexBuffer;
//    Buffer indexBuffer;
//    Buffer materialBuffer;
//};