#include "denoiser.h"
#include "core/context.h"
#include "rendering/model_loader.h"
#include <stdexcept>

Denoiser createDenoiser(Context& context, vk::Extent2D extent) {
    Denoiser denoiser;

    // Create history images
    for (int i = 0; i < 2; i++) {
        denoiser.historyColor[i] = Image{ context, extent, vk::Format::eR32G32B32A32Sfloat,
            vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst };

        denoiser.historyMoments[i] = Image{ context, extent, vk::Format::eR32G32Sfloat,
            vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst };

        denoiser.historyNormal[i] = Image{ context, extent, vk::Format::eR32G32B32A32Sfloat,
            vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst };

        denoiser.historyDepth[i] = Image{ context, extent, vk::Format::eR32Sfloat,
            vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst };
    }

    // Create working images
    denoiser.intensity = Image{ context, extent, vk::Format::eR32Sfloat,
        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst };

    denoiser.variance = Image{ context, extent, vk::Format::eR32Sfloat,
        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst };

    denoiser.filtered = Image{ context, extent, vk::Format::eR32G32B32A32Sfloat,
        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst };

    // Create settings buffer
    DenoiserSettings defaultSettings;
    denoiser.settingsBuffer = Buffer{ context, Buffer::Type::Storage, sizeof(DenoiserSettings), &defaultSettings };

    // Create descriptor set layout
    // Note: history bindings (5..8) use descriptorCount = 2 (ping-pong history)
    std::vector<vk::DescriptorSetLayoutBinding> bindings = {
        { 0,  vk::DescriptorType::eStorageImage,  1, vk::ShaderStageFlagBits::eCompute },   // Input image
        { 1,  vk::DescriptorType::eStorageImage,  1, vk::ShaderStageFlagBits::eCompute },   // GBuffer position
        { 2,  vk::DescriptorType::eStorageImage,  1, vk::ShaderStageFlagBits::eCompute },   // GBuffer normal
        { 3,  vk::DescriptorType::eStorageImage,  1, vk::ShaderStageFlagBits::eCompute },   // GBuffer albedo
        { 4,  vk::DescriptorType::eStorageImage,  1, vk::ShaderStageFlagBits::eCompute },   // GBuffer motion
        { 5,  vk::DescriptorType::eStorageImage,  2, vk::ShaderStageFlagBits::eCompute },   // History color
        { 6,  vk::DescriptorType::eStorageImage,  2, vk::ShaderStageFlagBits::eCompute },   // History moments
        { 7,  vk::DescriptorType::eStorageImage,  2, vk::ShaderStageFlagBits::eCompute },   // History normal
        { 8,  vk::DescriptorType::eStorageImage,  2, vk::ShaderStageFlagBits::eCompute },   // History depth
        { 9,  vk::DescriptorType::eStorageImage,  1, vk::ShaderStageFlagBits::eCompute },   // Output image
        { 10, vk::DescriptorType::eStorageImage,  1, vk::ShaderStageFlagBits::eCompute },   // Intensity
        { 11, vk::DescriptorType::eStorageImage,  1, vk::ShaderStageFlagBits::eCompute },   // Variance
        { 12, vk::DescriptorType::eStorageImage,  1, vk::ShaderStageFlagBits::eCompute },   // Filtered
        { 13, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eCompute },   // Settings
    };

    vk::DescriptorSetLayoutCreateInfo descSetLayoutInfo;
    descSetLayoutInfo.setBindings(bindings);
    denoiser.descSetLayout = context.device->createDescriptorSetLayoutUnique(descSetLayoutInfo);

    // Create pipeline layout (use vector overload)
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
    std::vector<vk::DescriptorSetLayout> layoutVec = { *denoiser.descSetLayout };
    pipelineLayoutInfo.setSetLayouts(layoutVec);
    denoiser.pipelineLayout = context.device->createPipelineLayoutUnique(pipelineLayoutInfo);

    // Create compute pipeline
    const std::vector<char> computeCode = readFile("source/shaders/svgf.comp.spv");
    vk::ShaderModuleCreateInfo shaderModuleInfo;
    shaderModuleInfo.setCodeSize(computeCode.size());
    shaderModuleInfo.setPCode(reinterpret_cast<const uint32_t*>(computeCode.data()));
    vk::UniqueShaderModule computeModule = context.device->createShaderModuleUnique(shaderModuleInfo);

    vk::PipelineShaderStageCreateInfo computeStageInfo;
    computeStageInfo.setStage(vk::ShaderStageFlagBits::eCompute);
    computeStageInfo.setModule(*computeModule);
    computeStageInfo.setPName("main");

    vk::ComputePipelineCreateInfo pipelineInfo;
    pipelineInfo.setStage(computeStageInfo);
    pipelineInfo.setLayout(*denoiser.pipelineLayout);

    denoiser.pipeline = context.device->createComputePipelineUnique(nullptr, pipelineInfo).value;

    // Create descriptor set
    denoiser.descSet = context.allocateDescSet(*denoiser.descSetLayout);

    // ---- IMPORTANT: update all descriptors referenced by svgf.comp ----
    // Build DescriptorImageInfo structs (single and arrays)
    vk::DescriptorImageInfo inputImgInfo = denoiser.filtered.descImageInfo; // placeholder; replace with actual input image when available
    // NOTE: The actual input image (raw raytrace output) should be passed from main.
    // We'll fill all denoiser owned images here:
    std::array<vk::DescriptorImageInfo, 2> historyColorInfos = {
        denoiser.historyColor[0].descImageInfo,
        denoiser.historyColor[1].descImageInfo
    };
    std::array<vk::DescriptorImageInfo, 2> historyMomentsInfos = {
        denoiser.historyMoments[0].descImageInfo,
        denoiser.historyMoments[1].descImageInfo
    };
    std::array<vk::DescriptorImageInfo, 2> historyNormalInfos = {
        denoiser.historyNormal[0].descImageInfo,
        denoiser.historyNormal[1].descImageInfo
    };
    std::array<vk::DescriptorImageInfo, 2> historyDepthInfos = {
        denoiser.historyDepth[0].descImageInfo,
        denoiser.historyDepth[1].descImageInfo
    };

    // single-image infos
    vk::DescriptorImageInfo outputInfo = denoiser.filtered.descImageInfo;
    vk::DescriptorImageInfo intensityInfo = denoiser.intensity.descImageInfo;
    vk::DescriptorImageInfo varianceInfo = denoiser.variance.descImageInfo;
    vk::DescriptorImageInfo filteredInfo = denoiser.filtered.descImageInfo;

    // Prepare write descriptors vector
    std::vector<vk::WriteDescriptorSet> writes;

    // Binding 5: historyColor (array of 2)
    vk::WriteDescriptorSet w5{};
    w5.setDstSet(*denoiser.descSet);
    w5.setDstBinding(5);
    w5.setDescriptorCount(static_cast<uint32_t>(historyColorInfos.size()));
    w5.setDescriptorType(vk::DescriptorType::eStorageImage);
    // pImageInfo must point to the first element of a contiguous array
    w5.setPImageInfo(historyColorInfos.data());
    writes.push_back(w5);

    // Binding 6: historyMoments (array of 2)
    vk::WriteDescriptorSet w6{};
    w6.setDstSet(*denoiser.descSet);
    w6.setDstBinding(6);
    w6.setDescriptorCount(static_cast<uint32_t>(historyMomentsInfos.size()));
    w6.setDescriptorType(vk::DescriptorType::eStorageImage);
    w6.setPImageInfo(historyMomentsInfos.data());
    writes.push_back(w6);

    // Binding 7: historyNormal (array of 2)
    vk::WriteDescriptorSet w7{};
    w7.setDstSet(*denoiser.descSet);
    w7.setDstBinding(7);
    w7.setDescriptorCount(static_cast<uint32_t>(historyNormalInfos.size()));
    w7.setDescriptorType(vk::DescriptorType::eStorageImage);
    w7.setPImageInfo(historyNormalInfos.data());
    writes.push_back(w7);

    // Binding 8: historyDepth (array of 2)
    vk::WriteDescriptorSet w8{};
    w8.setDstSet(*denoiser.descSet);
    w8.setDstBinding(8);
    w8.setDescriptorCount(static_cast<uint32_t>(historyDepthInfos.size()));
    w8.setDescriptorType(vk::DescriptorType::eStorageImage);
    w8.setPImageInfo(historyDepthInfos.data());
    writes.push_back(w8);

    // Binding 9: output image
    vk::WriteDescriptorSet w9{};
    w9.setDstSet(*denoiser.descSet);
    w9.setDstBinding(9);
    w9.setDescriptorCount(1);
    w9.setDescriptorType(vk::DescriptorType::eStorageImage);
    w9.setImageInfo(filteredInfo);
    writes.push_back(w9);

    // Binding 10: intensity
    vk::WriteDescriptorSet w10{};
    w10.setDstSet(*denoiser.descSet);
    w10.setDstBinding(10);
    w10.setDescriptorCount(1);
    w10.setDescriptorType(vk::DescriptorType::eStorageImage);
    w10.setImageInfo(intensityInfo);
    writes.push_back(w10);

    // Binding 11: variance
    vk::WriteDescriptorSet w11{};
    w11.setDstSet(*denoiser.descSet);
    w11.setDstBinding(11);
    w11.setDescriptorCount(1);
    w11.setDescriptorType(vk::DescriptorType::eStorageImage);
    w11.setImageInfo(varianceInfo);
    writes.push_back(w11);

    // Binding 12: filtered (if shader expects this separate from output)
    vk::WriteDescriptorSet w12{};
    w12.setDstSet(*denoiser.descSet);
    w12.setDstBinding(12);
    w12.setDescriptorCount(1);
    w12.setDescriptorType(vk::DescriptorType::eStorageImage);
    w12.setImageInfo(filteredInfo);
    writes.push_back(w12);

    // Binding 13: settings buffer
    vk::WriteDescriptorSet w13{};
    w13.setDstSet(*denoiser.descSet);
    w13.setDstBinding(13);
    w13.setDescriptorCount(1);
    w13.setDescriptorType(vk::DescriptorType::eUniformBuffer);
    w13.setBufferInfo(denoiser.settingsBuffer.descBufferInfo);
    writes.push_back(w13);

    // NOTE: Bindings 0..4 (input + gbuffer images) must be provided by the caller (main)
    // because they reference images owned/created elsewhere (raytraced output and GBuffer).
    // We leave them for main to update just before dispatch, since main has access to those resources.

    // Update descriptor sets with denoiser-owned resources
    context.device->updateDescriptorSets(writes, nullptr);

    return denoiser;
}