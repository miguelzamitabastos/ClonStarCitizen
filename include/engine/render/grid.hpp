#pragma once

#include "engine/core/types.hpp"
#include "engine/math/glm.hpp"

#include <cstddef>

namespace csc::render {

/// POD description of a ground-plane spatial reference grid (XZ, Y = constant).
/// Shared between Flecs (component) and the Vulkan upload path — no heap in the frame loop.
struct GroundGridDesc {
    f32 half_extent = 20.f;
    f32 cell_size   = 1.f;
    f32 y           = 0.f;
    glm::vec3 major_color{0.32f, 0.34f, 0.40f};
    glm::vec3 axis_x_color{0.75f, 0.28f, 0.28f};
    glm::vec3 axis_z_color{0.28f, 0.48f, 0.85f};
    bool visible = true;
};

/// Vertex layout matches triangle/grid shaders (pos + color).
struct GridVertex {
    f32 pos[3];
    f32 color[3];
};

/// Fixed capacity for pre-baked grid lines (init / level load only).
/// half_extent=50, cell=0.5 → ~201 lines/axis × 2 verts × 2 axes ≈ 804 verts.
inline constexpr u32 kMaxGridVertices = 2048u;

/// Bake an XZ line grid into a caller-owned fixed buffer. Returns false if capacity is too small.
/// No heap allocation — writes into `out_vertices[0..out_count)`.
[[nodiscard]] inline bool grid_bake_vertices(
    const GroundGridDesc& desc,
    GridVertex* out_vertices,
    u32 capacity,
    u32& out_count)
{
    out_count = 0;
    if (out_vertices == nullptr || capacity == 0) {
        return false;
    }
    if (!desc.visible || desc.cell_size <= 0.f || desc.half_extent <= 0.f) {
        return true;
    }

    const f32 half = desc.half_extent;
    const f32 step = desc.cell_size;
    const f32 y = desc.y;

    // Line count per axis: from -half to +half inclusive.
    const int line_count = static_cast<int>((2.f * half) / step) + 1;
    if (line_count < 2) {
        return true;
    }

    const u32 needed = static_cast<u32>(line_count) * 2u * 2u; // two axes, 2 verts/line
    if (needed > capacity) {
        return false;
    }

    auto emit_line = [&](f32 x0, f32 z0, f32 x1, f32 z1, const glm::vec3& color) {
        GridVertex& a = out_vertices[out_count++];
        a.pos[0] = x0;
        a.pos[1] = y;
        a.pos[2] = z0;
        a.color[0] = color.r;
        a.color[1] = color.g;
        a.color[2] = color.b;

        GridVertex& b = out_vertices[out_count++];
        b.pos[0] = x1;
        b.pos[1] = y;
        b.pos[2] = z1;
        b.color[0] = color.r;
        b.color[1] = color.g;
        b.color[2] = color.b;
    };

    for (int i = 0; i < line_count; ++i) {
        const f32 t = -half + static_cast<f32>(i) * step;
        // Lines parallel to X (constant Z) — axis Z=0 highlighted.
        const bool is_z_axis = (t > -1e-4f && t < 1e-4f);
        emit_line(-half, t, half, t, is_z_axis ? desc.axis_x_color : desc.major_color);
        // Lines parallel to Z (constant X) — axis X=0 highlighted.
        const bool is_x_axis = (t > -1e-4f && t < 1e-4f);
        emit_line(t, -half, t, half, is_x_axis ? desc.axis_z_color : desc.major_color);
    }

    return true;
}

}  // namespace csc::render
