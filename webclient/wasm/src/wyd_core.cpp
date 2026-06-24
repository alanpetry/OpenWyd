#include <cstdint>
#include <cstddef>
#include <cstring>

extern "C" {

struct WydMshHeader {
  std::uint32_t parent_id;
  std::uint32_t mesh_id;
  std::uint32_t fvf;
  std::uint32_t vertex_size;
  std::uint32_t face_influence;
  std::uint32_t palette_count;
  std::uint32_t vertex_count;
  std::uint32_t index_count;
};

struct WydTrnOverview {
  std::int32_t tile_count;
  std::int32_t height_min;
  std::int32_t height_max;
  float height_avg;
  std::int32_t offset_x;
  std::int32_t offset_y;
  std::int32_t map_name_len;
};

static std::uint32_t read_u32_le(const std::uint8_t* data) {
  return static_cast<std::uint32_t>(data[0])
    | (static_cast<std::uint32_t>(data[1]) << 8u)
    | (static_cast<std::uint32_t>(data[2]) << 16u)
    | (static_cast<std::uint32_t>(data[3]) << 24u);
}

int wyd_parse_msh_header(const std::uint8_t* data, int size, WydMshHeader* out_header) {
  if (data == nullptr || out_header == nullptr || size < 32) {
    return 0;
  }

  out_header->parent_id = read_u32_le(data + 0);
  out_header->mesh_id = read_u32_le(data + 4);
  out_header->fvf = read_u32_le(data + 8);
  out_header->vertex_size = read_u32_le(data + 12);
  out_header->face_influence = read_u32_le(data + 16);
  out_header->palette_count = read_u32_le(data + 20);
  out_header->vertex_count = read_u32_le(data + 24);
  out_header->index_count = read_u32_le(data + 28);
  return 1;
}

int wyd_parse_trn_overview(const std::uint8_t* data, int size, WydTrnOverview* out_info) {
  if (data == nullptr || out_info == nullptr || size < 4) {
    return 0;
  }

  std::memset(out_info, 0, sizeof(WydTrnOverview));
  const int name_len = static_cast<int>(data[0]);
  int offset = 1 + name_len;
  if (offset + 2 > size) {
    return 0;
  }

  out_info->map_name_len = name_len;
  out_info->offset_x = static_cast<int>(data[offset]);
  out_info->offset_y = static_cast<int>(data[offset + 1]);
  offset += 2;

  constexpr int kTileStride = 12;
  constexpr int kMaxTiles = 64 * 64;
  if (offset >= size) {
    return 0;
  }

  const int available_tiles = (size - offset) / kTileStride;
  const int tile_count = available_tiles < kMaxTiles ? available_tiles : kMaxTiles;
  if (tile_count <= 0) {
    return 0;
  }

  int hmin = 127;
  int hmax = -128;
  long long sum = 0;
  for (int i = 0; i < tile_count; i += 1) {
    const int base = offset + (i * kTileStride);
    const std::int8_t h = static_cast<std::int8_t>(data[base]);
    const int height = static_cast<int>(h);
    if (height < hmin) {
      hmin = height;
    }
    if (height > hmax) {
      hmax = height;
    }
    sum += height;
  }

  out_info->tile_count = tile_count;
  out_info->height_min = hmin;
  out_info->height_max = hmax;
  out_info->height_avg = static_cast<float>(sum) / static_cast<float>(tile_count);
  return 1;
}

}
