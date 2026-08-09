#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define TINYGLTF_NO_EXTERNAL_IMAGE
#include <tiny_gltf.h>

#include "engine/assets/mesh_loader.hpp"

#include "engine/log/log.hpp"

#include <cstdio>
#include <cstring>
#include <string>

namespace csc::assets {
namespace {

void set_error(char* buf, std::size_t cap, const char* msg)
{
    if (buf == nullptr || cap == 0) {
        return;
    }
    std::snprintf(buf, cap, "%s", msg != nullptr ? msg : "unknown error");
}

[[nodiscard]] const tinygltf::Accessor* get_accessor(
    const tinygltf::Model& model, int accessor_index)
{
    if (accessor_index < 0 || accessor_index >= static_cast<int>(model.accessors.size())) {
        return nullptr;
    }
    return &model.accessors[static_cast<std::size_t>(accessor_index)];
}

[[nodiscard]] const u8* accessor_bytes(
    const tinygltf::Model& model, const tinygltf::Accessor& accessor, std::size_t* out_stride)
{
    if (accessor.bufferView < 0
        || accessor.bufferView >= static_cast<int>(model.bufferViews.size())) {
        return nullptr;
    }
    const tinygltf::BufferView& view = model.bufferViews[static_cast<std::size_t>(accessor.bufferView)];
    if (view.buffer < 0 || view.buffer >= static_cast<int>(model.buffers.size())) {
        return nullptr;
    }
    const tinygltf::Buffer& buffer = model.buffers[static_cast<std::size_t>(view.buffer)];
    const std::size_t offset = static_cast<std::size_t>(view.byteOffset + accessor.byteOffset);
    if (offset >= buffer.data.size()) {
        return nullptr;
    }

    std::size_t stride = static_cast<std::size_t>(view.byteStride);
    if (stride == 0) {
        if (accessor.type == TINYGLTF_TYPE_VEC3 && accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
            stride = 12;
        } else if (accessor.type == TINYGLTF_TYPE_VEC2
                   && accessor.componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
            stride = 8;
        } else if (accessor.type == TINYGLTF_TYPE_SCALAR
                   && accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
            stride = 2;
        } else if (accessor.type == TINYGLTF_TYPE_SCALAR
                   && accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
            stride = 4;
        } else {
            stride = 12;
        }
    }
    if (out_stride != nullptr) {
        *out_stride = stride;
    }
    return buffer.data.data() + offset;
}

[[nodiscard]] bool extract_primitive(
    memory::Arena& arena,
    const tinygltf::Model& model,
    const tinygltf::Primitive& prim,
    MeshCpu& out,
    char* error_buf,
    std::size_t error_cap)
{
    if (prim.mode != TINYGLTF_MODE_TRIANGLES && prim.mode != -1) {
        set_error(error_buf, error_cap, "Only TRIANGLES primitives supported");
        return false;
    }

    const auto pos_it = prim.attributes.find("POSITION");
    if (pos_it == prim.attributes.end()) {
        set_error(error_buf, error_cap, "Missing POSITION attribute");
        return false;
    }

    const tinygltf::Accessor* pos_acc = get_accessor(model, pos_it->second);
    if (pos_acc == nullptr || pos_acc->count == 0) {
        set_error(error_buf, error_cap, "Invalid POSITION accessor");
        return false;
    }

    const u32 vertex_count = static_cast<u32>(pos_acc->count);
    MeshVertex* verts = memory::arena_alloc_array<MeshVertex>(arena, vertex_count);
    if (verts == nullptr) {
        set_error(error_buf, error_cap, "Asset arena OOM for vertices");
        return false;
    }

    std::size_t pos_stride = 0;
    const u8* pos_bytes = accessor_bytes(model, *pos_acc, &pos_stride);
    if (pos_bytes == nullptr) {
        set_error(error_buf, error_cap, "Failed to read POSITION buffer");
        return false;
    }

    const tinygltf::Accessor* nrm_acc = nullptr;
    const u8* nrm_bytes = nullptr;
    std::size_t nrm_stride = 0;
    const auto nrm_it = prim.attributes.find("NORMAL");
    if (nrm_it != prim.attributes.end()) {
        nrm_acc = get_accessor(model, nrm_it->second);
        if (nrm_acc != nullptr) {
            nrm_bytes = accessor_bytes(model, *nrm_acc, &nrm_stride);
        }
    }

    const tinygltf::Accessor* uv_acc = nullptr;
    const u8* uv_bytes = nullptr;
    std::size_t uv_stride = 0;
    const auto uv_it = prim.attributes.find("TEXCOORD_0");
    if (uv_it != prim.attributes.end()) {
        uv_acc = get_accessor(model, uv_it->second);
        if (uv_acc != nullptr) {
            uv_bytes = accessor_bytes(model, *uv_acc, &uv_stride);
        }
    }

    const tinygltf::Accessor* col_acc = nullptr;
    const u8* col_bytes = nullptr;
    std::size_t col_stride = 0;
    const auto col_it = prim.attributes.find("COLOR_0");
    if (col_it != prim.attributes.end()) {
        col_acc = get_accessor(model, col_it->second);
        if (col_acc != nullptr) {
            col_bytes = accessor_bytes(model, *col_acc, &col_stride);
        }
    }

    for (u32 i = 0; i < vertex_count; ++i) {
        MeshVertex& v = verts[i];
        const f32* p = reinterpret_cast<const f32*>(pos_bytes + i * pos_stride);
        v.pos[0] = p[0];
        v.pos[1] = p[1];
        v.pos[2] = p[2];

        if (nrm_bytes != nullptr) {
            const f32* n = reinterpret_cast<const f32*>(nrm_bytes + i * nrm_stride);
            v.normal[0] = n[0];
            v.normal[1] = n[1];
            v.normal[2] = n[2];
        } else {
            v.normal[0] = 0.f;
            v.normal[1] = 1.f;
            v.normal[2] = 0.f;
        }

        if (col_bytes != nullptr && col_acc != nullptr
            && col_acc->type == TINYGLTF_TYPE_VEC3
            && col_acc->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
            const f32* c = reinterpret_cast<const f32*>(col_bytes + i * col_stride);
            v.color[0] = c[0];
            v.color[1] = c[1];
            v.color[2] = c[2];
        } else if (col_bytes != nullptr && col_acc != nullptr
                   && col_acc->type == TINYGLTF_TYPE_VEC4
                   && col_acc->componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
            const f32* c = reinterpret_cast<const f32*>(col_bytes + i * col_stride);
            v.color[0] = c[0];
            v.color[1] = c[1];
            v.color[2] = c[2];
        } else {
            // Soft face tint by position so untextured cubes are readable.
            v.color[0] = 0.55f + 0.35f * (v.pos[0] * 0.5f + 0.5f);
            v.color[1] = 0.55f + 0.35f * (v.pos[1] * 0.5f + 0.5f);
            v.color[2] = 0.55f + 0.35f * (v.pos[2] * 0.5f + 0.5f);
        }

        if (uv_bytes != nullptr) {
            const f32* t = reinterpret_cast<const f32*>(uv_bytes + i * uv_stride);
            v.uv[0] = t[0];
            v.uv[1] = t[1];
        } else {
            v.uv[0] = 0.f;
            v.uv[1] = 0.f;
        }
    }

    u32 index_count = 0;
    u32* indices = nullptr;

    if (prim.indices >= 0) {
        const tinygltf::Accessor* idx_acc = get_accessor(model, prim.indices);
        if (idx_acc == nullptr || idx_acc->count == 0) {
            set_error(error_buf, error_cap, "Invalid index accessor");
            return false;
        }
        index_count = static_cast<u32>(idx_acc->count);
        indices = memory::arena_alloc_array<u32>(arena, index_count);
        if (indices == nullptr) {
            set_error(error_buf, error_cap, "Asset arena OOM for indices");
            return false;
        }

        std::size_t idx_stride = 0;
        const u8* idx_bytes = accessor_bytes(model, *idx_acc, &idx_stride);
        if (idx_bytes == nullptr) {
            set_error(error_buf, error_cap, "Failed to read index buffer");
            return false;
        }

        for (u32 i = 0; i < index_count; ++i) {
            if (idx_acc->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                const u16 v = *reinterpret_cast<const u16*>(idx_bytes + i * idx_stride);
                indices[i] = static_cast<u32>(v);
            } else if (idx_acc->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                indices[i] = *reinterpret_cast<const u32*>(idx_bytes + i * idx_stride);
            } else if (idx_acc->componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                indices[i] = static_cast<u32>(idx_bytes[i * idx_stride]);
            } else {
                set_error(error_buf, error_cap, "Unsupported index component type");
                return false;
            }
        }
    } else {
        index_count = vertex_count;
        indices = memory::arena_alloc_array<u32>(arena, index_count);
        if (indices == nullptr) {
            set_error(error_buf, error_cap, "Asset arena OOM for non-indexed indices");
            return false;
        }
        for (u32 i = 0; i < index_count; ++i) {
            indices[i] = i;
        }
    }

    out.vertices     = verts;
    out.indices      = indices;
    out.vertex_count = vertex_count;
    out.index_count  = index_count;
    return true;
}

}  // namespace

bool mesh_load_gltf_into_arena(
    memory::Arena& arena,
    const char* path,
    MeshCpu& out_mesh,
    char* error_buf,
    std::size_t error_cap)
{
    out_mesh = {};
    if (path == nullptr || path[0] == '\0') {
        set_error(error_buf, error_cap, "Empty glTF path");
        return false;
    }

    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;

    const bool ok = loader.LoadASCIIFromFile(&model, &err, &warn, path);
    if (!warn.empty()) {
        log::log_warn(log::LogCategory::Assets, "glTF warn (%s): %s", path, warn.c_str());
    }
    if (!ok) {
        set_error(error_buf, error_cap, err.empty() ? "LoadASCIIFromFile failed" : err.c_str());
        return false;
    }
    if (model.meshes.empty() || model.meshes[0].primitives.empty()) {
        set_error(error_buf, error_cap, "glTF has no mesh primitives");
        return false;
    }

    // First mesh, first primitive — enough for the unit-cube demo.
    return extract_primitive(arena, model, model.meshes[0].primitives[0], out_mesh, error_buf, error_cap);
}

bool mesh_loader_start(MeshLoader& loader, memory::Arena& arena, const char* path)
{
    if (path == nullptr || path[0] == '\0') {
        return false;
    }

    const MeshLoadStatus st = mesh_load_status(loader.slot);
    if (st == MeshLoadStatus::Loading) {
        return false;
    }

    mesh_loader_join(loader);

    loader.arena = &arena;
    loader.slot.cpu = {};
    loader.slot.error[0] = '\0';
    std::snprintf(loader.slot.path, kMeshPathMax, "%s", path);
    loader.slot.status.store(static_cast<u32>(MeshLoadStatus::Loading), std::memory_order_release);
    loader.joined = false;

    loader.worker = std::thread([&loader]() {
        char err[kMeshErrorMax]{};
        MeshCpu cpu{};
        const bool ok = mesh_load_gltf_into_arena(
            *loader.arena, loader.slot.path, cpu, err, sizeof(err));
        if (!ok) {
            std::snprintf(loader.slot.error, kMeshErrorMax, "%s", err);
            log::log_error(
                log::LogCategory::Assets,
                "Async glTF load failed (%s): %s",
                loader.slot.path,
                loader.slot.error);
            loader.slot.status.store(
                static_cast<u32>(MeshLoadStatus::Failed), std::memory_order_release);
            return;
        }
        loader.slot.cpu = cpu;
        log::log_info(
            log::LogCategory::Assets,
            "Async glTF ready (%s): verts=%u indices=%u",
            loader.slot.path,
            cpu.vertex_count,
            cpu.index_count);
        loader.slot.status.store(
            static_cast<u32>(MeshLoadStatus::Ready), std::memory_order_release);
    });

    log::log_info(log::LogCategory::Assets, "Started async glTF load: %s", path);
    return true;
}

void mesh_loader_join(MeshLoader& loader)
{
    if (!loader.joined && loader.worker.joinable()) {
        loader.worker.join();
    }
    loader.joined = true;
}

void mesh_loader_shutdown(MeshLoader& loader)
{
    mesh_loader_join(loader);
    loader.slot.status.store(static_cast<u32>(MeshLoadStatus::Idle), std::memory_order_release);
    loader.slot.cpu = {};
    loader.arena = nullptr;
}

}  // namespace csc::assets
