#include "engine/render/pipeline.hpp"

namespace csc::render {

PipelineHandle pipeline_catalog_register(PipelineCatalog& cat, const PipelineDesc& desc)
{
    if (cat.count >= kMaxPipelines) {
        return PipelineHandle{};
    }
    const u32 index = cat.count++;
    PipelineEntry& entry = cat.entries[index];
    entry       = {};
    entry.desc  = desc;
    entry.alive = true;
    return PipelineHandle{index};
}

MaterialHandle material_catalog_register(MaterialCatalog& cat, const MaterialDesc& desc)
{
    if (cat.count >= kMaxMaterials) {
        return MaterialHandle{};
    }
    const u32 index = cat.count++;
    MaterialEntry& entry = cat.entries[index];
    entry       = {};
    entry.desc  = desc;
    entry.alive = true;
    return MaterialHandle{index};
}

void pipeline_catalog_destroy_gpu(PipelineCatalog& cat, VkDevice device)
{
    if (device == VK_NULL_HANDLE) {
        for (u32 i = 0; i < cat.count; ++i) {
            cat.entries[i].pipeline = VK_NULL_HANDLE;
            cat.entries[i].layout   = VK_NULL_HANDLE;
        }
        return;
    }

    for (u32 i = 0; i < cat.count; ++i) {
        PipelineEntry& e = cat.entries[i];
        if (e.pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, e.pipeline, nullptr);
            e.pipeline = VK_NULL_HANDLE;
        }
        if (e.layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, e.layout, nullptr);
            e.layout = VK_NULL_HANDLE;
        }
    }
}

}  // namespace csc::render
