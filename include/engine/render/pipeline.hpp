#pragma once

#include "engine/core/types.hpp"

#include <vulkan/vulkan.h>

namespace csc::render {

inline constexpr u32 kMaxPipelines = 16u;
inline constexpr u32 kMaxMaterials = 16u;
inline constexpr u32 kInvalidHandle = 0xFFFFFFFFu;

/// Opaque index into PipelineCatalog (game/systems bind by handle, not raw VkPipeline).
struct PipelineHandle {
    u32 index = kInvalidHandle;
};

[[nodiscard]] inline bool pipeline_handle_valid(PipelineHandle h)
{
    return h.index != kInvalidHandle && h.index < kMaxPipelines;
}

struct MaterialHandle {
    u32 index = kInvalidHandle;
};

[[nodiscard]] inline bool material_handle_valid(MaterialHandle h)
{
    return h.index != kInvalidHandle && h.index < kMaxMaterials;
}

/// Built-in shader pairs compiled via CMake (paths relative to CSC_SHADER_DIR).
enum class BuiltinShaderId : u32 {
    TriangleColored = 0, ///< triangle.vert/frag — pos+color, push-constant VP
    MeshInstanced   = 1, ///< mesh.vert/frag — UBO VP + instance mat4 + texture
    Count
};

/// Fixed vertex-input layouts known to the renderer.
enum class VertexLayoutId : u32 {
    PosColor            = 0, ///< binding 0: pos3 + color3 (grid / legacy triangle)
    MeshPosNormColorUv  = 1, ///< binding 0: mesh vert; binding 1: instance mat4
    Count
};

struct PipelineDesc {
    VkPrimitiveTopology topology       = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    BuiltinShaderId     shader_id      = BuiltinShaderId::TriangleColored;
    VertexLayoutId      vertex_layout  = VertexLayoutId::PosColor;
    bool                depth_test     = true;
    bool                depth_write    = true;
    bool                use_descriptors = false; ///< mesh path: set=0 UBO + sampler
};

struct PipelineEntry {
    PipelineDesc     desc{};
    VkPipeline       pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout   = VK_NULL_HANDLE;
    bool             alive    = false;
};

/// Fixed-capacity registry owned by the renderer (created at init, no heap growth).
struct PipelineCatalog {
    PipelineEntry entries[kMaxPipelines]{};
    u32           count = 0;
};

struct MaterialDesc {
    PipelineHandle pipeline{};
};

struct MaterialEntry {
    MaterialDesc desc{};
    bool         alive = false;
};

struct MaterialCatalog {
    MaterialEntry entries[kMaxMaterials]{};
    u32           count = 0;
};

[[nodiscard]] inline PipelineEntry* pipeline_catalog_get(PipelineCatalog& cat, PipelineHandle h)
{
    if (!pipeline_handle_valid(h) || h.index >= cat.count || !cat.entries[h.index].alive) {
        return nullptr;
    }
    return &cat.entries[h.index];
}

[[nodiscard]] inline const PipelineEntry* pipeline_catalog_get(
    const PipelineCatalog& cat, PipelineHandle h)
{
    if (!pipeline_handle_valid(h) || h.index >= cat.count || !cat.entries[h.index].alive) {
        return nullptr;
    }
    return &cat.entries[h.index];
}

[[nodiscard]] inline MaterialEntry* material_catalog_get(MaterialCatalog& cat, MaterialHandle h)
{
    if (!material_handle_valid(h) || h.index >= cat.count || !cat.entries[h.index].alive) {
        return nullptr;
    }
    return &cat.entries[h.index];
}

/// Reserve a catalog slot and store `desc` (Vk objects filled later by renderer).
[[nodiscard]] PipelineHandle pipeline_catalog_register(PipelineCatalog& cat, const PipelineDesc& desc);

[[nodiscard]] MaterialHandle material_catalog_register(MaterialCatalog& cat, const MaterialDesc& desc);

void pipeline_catalog_destroy_gpu(PipelineCatalog& cat, VkDevice device);

}  // namespace csc::render
