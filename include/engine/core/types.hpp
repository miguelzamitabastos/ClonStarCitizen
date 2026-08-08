#pragma once

#include <cstddef>
#include <cstdint>

namespace csc {

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i32 = std::int32_t;
using i64 = std::int64_t;
using f32 = float;
using f64 = double;

using EntityId = u32;

inline constexpr EntityId kInvalidEntity = 0xFFFFFFFFu;
inline constexpr std::size_t kCacheLineBytes = 64;

}  // namespace csc
