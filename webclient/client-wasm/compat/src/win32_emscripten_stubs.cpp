#include "windows.h"
#include "d3d9.h"
#include "d3dx9.h"
#include "dinput.h"
#include "dsound.h"
#include "io.h"
#include "iphlpapi.h"
#include "mmsystem.h"
#include "objbase.h"
#include "shellapi.h"
#include "WinInet.h"
#include "winsock2.h"

#include <emscripten/html5.h>
#include <GLES2/gl2.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <fnmatch.h>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

constexpr DWORD kFileAttributeDirectory = 0x00000010;
constexpr DWORD kFileAttributeNormal = 0x00000080;
constexpr int kDefaultWidth = 1024;
constexpr int kDefaultHeight = 768;

std::atomic<uintptr_t> g_handle_seed{0x1000};
std::atomic<DWORD> g_last_error{0};
std::chrono::steady_clock::time_point g_start_tick = std::chrono::steady_clock::now();
bool g_fake_time_enabled = false;
DWORD g_fake_time_ms = 0;
bool g_debug_demo_camera_offset_enabled = false;
float g_debug_demo_camera_offset_x = 0.0f;
float g_debug_demo_camera_offset_y = 0.0f;
float g_debug_demo_camera_offset_z = 0.0f;
float g_debug_demo_camera_offset_h = 0.0f;
float g_debug_demo_camera_offset_v = 0.0f;

struct FileHandle {
  int fd = -1;
};

struct FindHandle {
  std::vector<std::filesystem::path> entries;
  size_t index = 0;
};

struct InternetHandle {
  std::vector<unsigned char> data;
  size_t cursor = 0;
};

struct DibSection {
  int width = 0;
  int height = 0;
  int bits_per_pixel = 0;
  uint32_t* pixels = nullptr;
};

struct FontInfo {
  std::string face_name;
  int height = 14;
  int weight = 400;
  bool italic = false;
};

struct HdcState {
  HBITMAP bitmap = nullptr;
  HFONT font = nullptr;
  COLORREF text_color = 0x00FFFFFF;
  COLORREF bk_color = 0;
  int bk_mode = 2;
};

std::unordered_map<void*, DibSection> g_dib_sections;
std::unordered_map<void*, HdcState> g_hdc_states;
std::unordered_map<void*, FontInfo> g_font_info;

HdcState* HdcStateFor(HDC hdc) {
  auto it = g_hdc_states.find(reinterpret_cast<void*>(hdc));
  return (it == g_hdc_states.end()) ? nullptr : &it->second;
}

DibSection* SelectedDibFor(HDC hdc) {
  HdcState* state = HdcStateFor(hdc);
  if (!state || !state->bitmap) return nullptr;
  auto it = g_dib_sections.find(reinterpret_cast<void*>(state->bitmap));
  return (it == g_dib_sections.end()) ? nullptr : &it->second;
}

FontInfo* FontInfoFor(HFONT font) {
  if (!font) return nullptr;
  auto it = g_font_info.find(reinterpret_cast<void*>(font));
  return (it == g_font_info.end()) ? nullptr : &it->second;
}

// ---- stb_truetype font rasterizer ----
// Replaces Canvas 2D; renders glyphs in pure C++ from bundled Tahoma.ttf.

// Forward declare PutDibPixel (defined later in this file)
void PutDibPixel(DibSection* dib, int x, int y, uint32_t color);

std::vector<unsigned char> g_font_file_data;
stbtt_fontinfo g_font_stb;
bool g_font_stb_ok = false;

void EnsureFontRenderer() {
  if (g_font_stb_ok) return;
  g_font_stb_ok = true;

  FILE* ff = fopen("/Tahoma.ttf", "rb");
  if (!ff) return;
  fseek(ff, 0, SEEK_END);
  long sz = ftell(ff);
  fseek(ff, 0, SEEK_SET);
  g_font_file_data.resize(static_cast<size_t>(sz));
  fread(g_font_file_data.data(), 1, static_cast<size_t>(sz), ff);
  fclose(ff);

  if (!stbtt_InitFont(&g_font_stb, g_font_file_data.data(), 0))
    g_font_file_data.clear();
}

// Returns rendered width.  Writes alpha-blended glyphs into dib->pixels.
int RenderTextToDIB(DibSection* dib, int x, int y,
                    const char* text, int len,
                    FontInfo* finfo, uint32_t text_color) {
  if (!dib || !dib->pixels || !text || len <= 0) return 0;
  EnsureFontRenderer();
  if (g_font_file_data.empty()) return 0;

  float scale = stbtt_ScaleForPixelHeight(&g_font_stb,
      static_cast<float>(std::abs(finfo ? finfo->height : 14)));
  int ascent_f = 0, descent_f = 0, lg_f = 0;
  stbtt_GetFontVMetrics(&g_font_stb, &ascent_f, &descent_f, &lg_f);
  int baseline = static_cast<int>(std::round(ascent_f * scale));

  int pen_x = x;
  int pen_y = y + baseline;
  int max_pen = x;

  for (int i = 0; i < len; ++i) {
    unsigned char ch = static_cast<unsigned char>(text[i]);
    if (ch == '\n' || ch == '\r') continue;

    int gw = 0, gh = 0, xoff = 0, yoff = 0;
    unsigned char* glyph = stbtt_GetCodepointBitmap(
        &g_font_stb, 0, scale, ch, &gw, &gh, &xoff, &yoff);
    if (glyph) {
      int dx = pen_x + xoff;
      int dy = pen_y + yoff;
      uint8_t tr = static_cast<uint8_t>(text_color);
      uint8_t tg = static_cast<uint8_t>(text_color >> 8);
      uint8_t tb = static_cast<uint8_t>(text_color >> 16);
      for (int row = 0; row < gh; ++row) {
        for (int col = 0; col < gw; ++col) {
          uint8_t a = glyph[row * gw + col];
          if (a == 0) continue;
          if (a == 255) {
            PutDibPixel(dib, dx + col, dy + row, text_color);
          } else {
            uint8_t out_r = (uint8_t)((255u * a + tr * (255u - a)) / 255u);
            uint8_t out_g = (uint8_t)((255u * a + tg * (255u - a)) / 255u);
            uint8_t out_b = (uint8_t)((255u * a + tb * (255u - a)) / 255u);
            PutDibPixel(dib, dx + col, dy + row,
                         ((uint32_t)a << 24) | ((uint32_t)out_b << 16) |
                         ((uint32_t)out_g << 8) | out_r);
          }
        }
      }
      stbtt_FreeBitmap(glyph, nullptr);
    }

    int advance = 0;
    stbtt_GetCodepointHMetrics(&g_font_stb, ch, &advance, nullptr);
    pen_x += static_cast<int>(std::round(advance * scale));
    if (i + 1 < len) {
      pen_x += static_cast<int>(std::round(
          stbtt_GetCodepointKernAdvance(&g_font_stb, ch,
                                         static_cast<unsigned char>(text[i + 1])) *
          scale));
    }
    if (pen_x > max_pen) max_pen = pen_x;
  }
  return max_pen - x;
}

// Measure text width (same logic as RenderTextToDIB without drawing).
int MeasureTextWidth(const char* text, int len, FontInfo* finfo) {
  if (!text || len <= 0) return 0;
  EnsureFontRenderer();
  if (g_font_file_data.empty()) return len * 8;

  float scale = stbtt_ScaleForPixelHeight(&g_font_stb,
      static_cast<float>(std::abs(finfo ? finfo->height : 14)));
  int pen_x = 0;
  for (int i = 0; i < len; ++i) {
    unsigned char ch = static_cast<unsigned char>(text[i]);
    if (ch == '\n' || ch == '\r') continue;
    int advance = 0;
    stbtt_GetCodepointHMetrics(&g_font_stb, ch, &advance, nullptr);
    pen_x += static_cast<int>(std::round(advance * scale));
    if (i + 1 < len) {
      pen_x += static_cast<int>(std::round(
          stbtt_GetCodepointKernAdvance(&g_font_stb, ch,
                                         static_cast<unsigned char>(text[i + 1])) *
          scale));
    }
  }
  return pen_x;
}


std::array<uint8_t, 7> WydGlyphRows(unsigned char input) {
  const char ch = static_cast<char>(std::toupper(input));
  switch (ch) {
    case 'A': return {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    case 'B': return {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E};
    case 'C': return {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
    case 'D': return {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E};
    case 'E': return {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
    case 'F': return {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10};
    case 'G': return {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F};
    case 'H': return {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    case 'I': return {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E};
    case 'J': return {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C};
    case 'K': return {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
    case 'L': return {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
    case 'M': return {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11};
    case 'N': return {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
    case 'O': return {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    case 'P': return {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
    case 'Q': return {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D};
    case 'R': return {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
    case 'S': return {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E};
    case 'T': return {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    case 'U': return {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    case 'V': return {0x11, 0x11, 0x11, 0x11, 0x0A, 0x0A, 0x04};
    case 'W': return {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11};
    case 'X': return {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11};
    case 'Y': return {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04};
    case 'Z': return {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F};
    case '0': return {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
    case '1': return {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E};
    case '2': return {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F};
    case '3': return {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E};
    case '4': return {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
    case '5': return {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E};
    case '6': return {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E};
    case '7': return {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
    case '8': return {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E};
    case '9': return {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C};
    case '-': return {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
    case '_': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F};
    case '.': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C};
    case ',': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x08};
    case ':': return {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00};
    case '/': return {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10};
    case '(': return {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02};
    case ')': return {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08};
    case '%': return {0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13};
    case '+': return {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00};
    case '!': return {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04};
    case '?': return {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04};
    case ' ': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    default: return {0x0E, 0x11, 0x01, 0x06, 0x04, 0x00, 0x04};
  }
}

void PutDibPixel(DibSection* dib, int x, int y, uint32_t color) {
  if (!dib || !dib->pixels || x < 0 || y < 0 || x >= dib->width || y >= dib->height) return;
  dib->pixels[static_cast<size_t>(y) * static_cast<size_t>(dib->width) + static_cast<size_t>(x)] = color;
}

void DrawFallbackGlyph(DibSection* dib, int x, int y, unsigned char ch, uint32_t color) {
  const auto rows = WydGlyphRows(ch);
  for (int row = 0; row < 7; ++row) {
    for (int col = 0; col < 5; ++col) {
      if ((rows[row] & (1u << (4 - col))) != 0u) {
        PutDibPixel(dib, x + col, y + row + 2, color);
      }
    }
  }
}

std::mutex g_mutex;
std::unordered_set<FileHandle*> g_file_handles;
std::unordered_set<FindHandle*> g_find_handles;
std::unordered_set<InternetHandle*> g_internet_handles;

struct WindowState {
  LONG style = WS_OVERLAPPEDWINDOW;
  LONG user_data = 0;
  HMENU menu = nullptr;
  RECT rect{0, 0, kDefaultWidth, kDefaultHeight};
  std::string text;
};

std::unordered_map<HWND, WindowState> g_windows;
std::deque<MSG> g_msg_queue;
HWND g_focus = nullptr;
uint64_t g_tex_decode_success = 0;
uint64_t g_tex_decode_fail = 0;
uint64_t g_tex_upload_count = 0;
uint64_t g_tex_draw_count = 0;
uint64_t g_tex_alpha_promoted_opaque = 0;
uint64_t g_draw_fvf_xyzrhw = 0;
uint64_t g_draw_fvf_weighted = 0;
uint64_t g_draw_fvf_tex2plus = 0;
uint64_t g_stage1_generated_tci_draws = 0;
uint64_t g_stage1_textransform_draws = 0;
uint64_t g_stage1_tci0_draws = 0;
uint64_t g_stage1_tci1_draws = 0;
uint64_t g_stage1_tci_other_draws = 0;
uint64_t g_decl_draw_calls = 0;
uint64_t g_decl_skinned_draw_calls = 0;
uint64_t g_decl_vertices_total = 0;
uint64_t g_decl_vertices_in_clip = 0;
uint64_t g_decl_rgba_index_order_draws = 0;
uint64_t g_decl_bgra_index_order_draws = 0;
uint64_t g_invalid_indexed_draws = 0;
uint64_t g_invalid_indices_total = 0;
uint64_t g_fvf_3d_vertices_total = 0;
uint64_t g_fvf_3d_vertices_in_clip = 0;
uint64_t g_draw_alpha_test_enabled = 0;
uint64_t g_draw_alpha_test_disabled = 0;
uint64_t g_draw_blend_enabled = 0;
uint64_t g_draw_depth_test_disabled = 0;
uint64_t g_draw_depth_write_disabled = 0;
uint64_t g_depth_write_guard_forced_draws = 0;
uint64_t g_draw_order_serial = 0;
uint64_t g_draw_order_first_sky = 0;
uint64_t g_draw_order_first_skin = 0;
uint64_t g_draw_order_first_terrain594 = 0;
uint64_t g_draw_order_first_water578 = 0;
uint64_t g_draw_order_first_fvf530 = 0;
uint64_t g_draw_order_first_fvf322 = 0;
uint64_t g_draw_order_count_sky = 0;
uint64_t g_draw_order_count_skin = 0;
uint64_t g_draw_order_count_terrain594 = 0;
uint64_t g_draw_order_count_water578 = 0;
uint64_t g_draw_order_count_fvf530 = 0;
uint64_t g_draw_order_count_fvf322 = 0;
uint64_t g_draw_order_frame_first_sky = 0;
uint64_t g_draw_order_frame_first_skin = 0;
uint64_t g_draw_order_frame_first_terrain594 = 0;
uint64_t g_draw_order_frame_first_water578 = 0;
uint64_t g_draw_order_frame_first_fvf530 = 0;
uint64_t g_draw_order_frame_first_fvf322 = 0;
uint64_t g_draw_order_frame_count_sky = 0;
uint64_t g_draw_order_frame_count_skin = 0;
uint64_t g_draw_order_frame_count_terrain594 = 0;
uint64_t g_draw_order_frame_count_water578 = 0;
uint64_t g_draw_order_frame_count_fvf530 = 0;
uint64_t g_draw_order_frame_count_fvf322 = 0;
uint64_t g_draw_lighting_enabled = 0;
uint64_t g_draw_stage0_color_selectarg1 = 0;
uint64_t g_draw_stage0_color_modulate = 0;
uint64_t g_draw_stage0_alpha_selectarg1 = 0;
uint64_t g_draw_stage0_alpha_modulate = 0;
uint64_t g_draw_cull_none = 0;
uint64_t g_draw_cull_cw = 0;
uint64_t g_draw_cull_ccw = 0;
uint64_t g_cull_mirror_worldview_draws = 0;
uint64_t g_cull_frontface_flipped_draws = 0;
uint64_t g_material_set_calls = 0;
uint64_t g_light_set_calls = 0;
uint64_t g_light_enable_calls = 0;
uint64_t g_lighted_vertices = 0;
uint64_t g_draw_fog_enabled = 0;
uint64_t g_draw_wireframe = 0;
uint64_t g_debug_skip_fvf_draws = 0;
uint64_t g_fvf322_draw_primitive_up = 0;
uint64_t g_fvf322_draw_indexed_primitive_up = 0;
uint64_t g_fvf322_draw_indexed_primitive = 0;
uint64_t g_fvf322_with_stage0_texture = 0;
uint64_t g_fvf322_without_stage0_texture = 0;
uint64_t g_fvf322_screenlike_vertices = 0;
uint64_t g_fvf322_screenlike_draws = 0;
uint64_t g_fvf322_screenlike_replay_draws = 0;
uint64_t g_fvf322_auto_clipw_draws = 0;
uint64_t g_fvf322_auto_clipw_reject_draws = 0;
uint64_t g_fvf530_auto_clipw_draws = 0;
uint64_t g_fvf530_auto_clipw_reject_draws = 0;
uint64_t g_fvf530_draws = 0;
uint64_t g_fvf530_large_bounds_draws = 0;
uint64_t g_fvf530_large_bounds_skip_draws = 0;
uint64_t g_fvf530_unstable_w_draws = 0;
uint64_t g_fvf530_inside_vertices = 0;
uint64_t g_fvf530_vertices = 0;
std::vector<std::string> g_fvf530_large_bound_samples;
std::unordered_set<std::string> g_fvf530_large_bound_seen;
uint32_t g_fvf530_last_vertex_count = 0;
uint32_t g_fvf530_last_index_count = 0;
uint32_t g_fvf530_last_stable_w_vertices = 0;
uint32_t g_fvf530_last_inside_vertices = 0;
uint32_t g_fvf530_last_large_ndc_vertices = 0;
float g_fvf530_last_min_ndc_x = 0.0f;
float g_fvf530_last_max_ndc_x = 0.0f;
float g_fvf530_last_min_ndc_y = 0.0f;
float g_fvf530_last_max_ndc_y = 0.0f;
float g_fvf530_last_min_ndc_z = 0.0f;
float g_fvf530_last_max_ndc_z = 0.0f;
uint64_t g_fvf594_auto_clipw_draws = 0;
uint64_t g_fvf594_auto_clipw_reject_draws = 0;
uint64_t g_up_reset_stream0_calls = 0;
uint64_t g_up_reset_indices_calls = 0;
uint64_t g_clip_w_reject_draws = 0;
uint64_t g_clip_w_reject_triangles = 0;
uint64_t g_clip_w_keep_triangles = 0;
uint32_t g_debug_ffp_flags = 0;
uint32_t g_debug_skip_fvf = 0;
std::unordered_map<uint32_t, uint64_t> g_fvf_histogram;
std::unordered_map<uint32_t, uint64_t> g_fvf_depth_write_enabled_histogram;
std::unordered_map<uint32_t, uint64_t> g_fvf_depth_write_disabled_histogram;
constexpr size_t kDrawTraceMaxProbes = 16;
constexpr size_t kDrawTraceMaxTopSamples = 96;

struct DrawTraceProbe {
  bool enabled = false;
  float x = 0.0f;
  float y = 0.0f;
  bool depth_valid = false;
  float depth = 1.0f;
  uint64_t draw_serial = 0;
  std::string result;
  uint64_t hit_count = 0;
  uint64_t first_hit_draw_serial = 0;
  uint64_t nearest_hit_draw_serial = 0;
  uint64_t nearest_zwrite_draw_serial = 0;
  float nearest_hit_depth = 1.0f;
  float nearest_zwrite_depth = 1.0f;
  std::string first_hit_result;
  std::string nearest_hit_result;
  std::string nearest_zwrite_result;
};

struct DrawTraceTopSample {
  double score = 0.0;
  uint64_t draw_serial = 0;
  std::string text;
};

bool g_draw_trace_enabled = false;
uint64_t g_draw_trace_serial = 0;
std::string g_draw_trace_scope;
std::string g_draw_trace_label;
std::array<DrawTraceProbe, kDrawTraceMaxProbes> g_draw_trace_probes{};
std::vector<DrawTraceTopSample> g_draw_trace_top_samples;
std::vector<std::string> g_draw_trace_top_export_cache;

struct ShaderTelemetryEntry {
  uint64_t creates = 0;
  uint64_t binds = 0;
  uint64_t draws_used = 0;
  uint64_t draws_skipped = 0;
  uint32_t dword_count = 0;
  uint32_t version_token = 0;
};

std::unordered_map<uint64_t, ShaderTelemetryEntry> g_vs_telemetry;
std::unordered_map<uint64_t, ShaderTelemetryEntry> g_ps_telemetry;
uint64_t g_vs_bind_calls = 0;
uint64_t g_ps_bind_calls = 0;
uint64_t g_draws_with_vs = 0;
uint64_t g_draws_with_ps = 0;
uint64_t g_active_vs_hash = 0;
uint64_t g_active_ps_hash = 0;
uint64_t g_shader_file_open_attempts = 0;
uint64_t g_shader_file_open_success = 0;
uint64_t g_shader_file_open_fail = 0;
uint64_t g_shader_file_open_skinmesh = 0;
uint64_t g_shader_file_open_vseffect = 0;
uint64_t g_shader_file_open_pseffect = 0;
uint64_t g_asset_file_open_attempts = 0;
uint64_t g_asset_file_open_success = 0;
uint64_t g_asset_file_open_fail = 0;
uint64_t g_asset_file_open_fail_mesh = 0;
uint64_t g_asset_file_open_fail_env = 0;
uint64_t g_asset_file_open_fail_ui = 0;
uint64_t g_asset_file_open_fail_texture = 0;
uint64_t g_asset_file_open_fail_sound = 0;
std::vector<std::string> g_asset_file_open_fail_samples;
std::unordered_set<std::string> g_asset_file_open_fail_seen;
uint64_t g_asset_path_fallback_attempts = 0;
uint64_t g_asset_path_fallback_hits = 0;
uint64_t g_asset_path_fallback_or010_hits = 0;
std::vector<std::string> g_asset_path_fallback_samples;
std::unordered_set<std::string> g_asset_path_fallback_seen;
std::unordered_map<int, std::string> g_fd_debug_source_paths;
std::string g_last_closed_debug_source_path;
uint64_t g_gl_error_total = 0;
uint64_t g_gl_error_draw_calls = 0;
uint32_t g_gl_error_last = 0;
uint64_t g_debug_clear_calls = 0;
uint64_t g_debug_present_calls = 0;
uint64_t g_debug_begin_scene_calls = 0;
uint64_t g_debug_end_scene_calls = 0;
DWORD g_debug_last_clear_color = 0;
DWORD g_debug_last_clear_flags = 0;
float g_debug_last_clear_z = 1.0f;
DWORD g_debug_last_clear_time_ms = 0;
DWORD g_debug_last_present_time_ms = 0;
uint32_t g_debug_last_present_blend_enabled = 0;
uint32_t g_debug_last_present_depth_enabled = 0;
uint32_t g_debug_last_present_depth_write = 0;
uint32_t g_debug_last_present_alpha_test = 0;
uint32_t g_debug_last_present_src_blend = 0;
uint32_t g_debug_last_present_dst_blend = 0;
uint64_t g_texture_draws_sky = 0;
uint64_t g_texture_draws_water = 0;
uint64_t g_texture_draws_bright = 0;
uint64_t g_fvf322_bright_draws = 0;
constexpr size_t kFvf322ClassCount = 7;
std::array<uint64_t, kFvf322ClassCount> g_fvf322_class_draws{};
uint64_t g_fvf322_screenlike_replay_suppressed = 0;
uint64_t g_stage0_colorop8_draws = 0;
uint64_t g_stage0_colorop8_with_texture = 0;
uint64_t g_stage0_colorop8_without_texture = 0;
uint64_t g_stage0_colorop8_pathless_texture = 0;
uint32_t g_stage0_colorop8_last_fvf = 0;
uint32_t g_stage0_colorop8_last_width = 0;
uint32_t g_stage0_colorop8_last_height = 0;
uint32_t g_stage0_colorop8_last_path_len = 0;
uint64_t g_set_stage0_colorop8_calls = 0;
uint64_t g_set_stage0_colorop4_calls = 0;
uint32_t g_set_stage0_colorop_last_value = 0;
uint64_t g_set_texture_stage0_sky_calls = 0;
uint64_t g_set_texture_stage1_sky_calls = 0;
uint64_t g_draw_attempts_with_sky_texture = 0;
uint64_t g_draw_attempts_with_sky_texture_indexed = 0;
uint64_t g_draw_attempts_with_sky_texture_up = 0;
uint32_t g_draw_attempts_with_sky_last_fvf = 0;
uint64_t g_sky_clip_draws = 0;
uint32_t g_sky_clip_last_vertex_count = 0;
uint32_t g_sky_clip_last_index_count = 0;
uint32_t g_sky_clip_last_stable_w_vertices = 0;
uint32_t g_sky_clip_last_negative_w_vertices = 0;
uint32_t g_sky_clip_last_near_w_vertices = 0;
uint32_t g_sky_clip_last_inside_vertices = 0;
uint32_t g_sky_clip_last_large_ndc_vertices = 0;
uint32_t g_sky_clip_last_triangle_count = 0;
uint32_t g_sky_clip_last_triangles_all_stable_w = 0;
uint32_t g_sky_clip_last_triangles_any_unstable_w = 0;
uint32_t g_sky_clip_last_triangles_any_outside = 0;
float g_sky_clip_last_min_w = 0.0f;
float g_sky_clip_last_max_w = 0.0f;
float g_sky_clip_last_min_ndc_x = 0.0f;
float g_sky_clip_last_max_ndc_x = 0.0f;
float g_sky_clip_last_min_ndc_y = 0.0f;
float g_sky_clip_last_max_ndc_y = 0.0f;
float g_sky_clip_last_min_ndc_z = 0.0f;
float g_sky_clip_last_max_ndc_z = 0.0f;
bool g_mark_next_draw_sky = false;
std::unordered_map<std::string, uint64_t> g_clipw_empty_signature_histogram;
std::vector<std::string> g_clipw_empty_signature_export_cache;
uint64_t g_skin_suspicious_texture_draws = 0;
std::vector<std::string> g_skin_suspicious_texture_samples;
std::unordered_set<std::string> g_skin_suspicious_texture_seen;

constexpr uint32_t kDebugDisableAlphaTest = 1u << 0;
constexpr uint32_t kDebugDisableDepthTest = 1u << 1;
constexpr uint32_t kDebugDisableDepthWrite = 1u << 2;
constexpr uint32_t kDebugForceOpaqueTextureAlpha = 1u << 3;
constexpr uint32_t kDebugDisableCull = 1u << 4;
constexpr uint32_t kDebugDisableFog = 1u << 5;
constexpr uint32_t kDebugDisableBlend = 1u << 6;
constexpr uint32_t kDebugSwapTextureRB = 1u << 7;
constexpr uint32_t kDebugEnableClipWReject = 1u << 8;
constexpr uint32_t kDebugDisableStage1 = 1u << 9;
constexpr uint32_t kDebugDisableDepthWriteGuard = 1u << 10;
constexpr uint32_t kDebugUseRawClipZ = 1u << 11;
constexpr uint32_t kDebugDepthGuardOnly322 = 1u << 12;
constexpr uint32_t kDebugDepthGuardOnly594 = 1u << 13;
constexpr uint32_t kDebugDisableAutoClipWReject = 1u << 14;
constexpr uint32_t kDebugDisableFvf322ScreenlikeReplay = 1u << 15;
constexpr uint32_t kDebugClipTriangleRepair = 1u << 16;
constexpr uint32_t kDebugSkipSkyDraw = 1u << 17;
constexpr uint32_t kDebugForceFVF594White = 1u << 24;
constexpr uint32_t kDebugDisableFVF530LargeBoundsSkip = 1u << 20;
constexpr uint32_t kDebugEnableDrawTrace = 1u << 21;
constexpr uint32_t kDebugGuardSkinDepthWrite = 1u << 23;
constexpr uint32_t kDebugEnableLegacyDepthWriteGuard = 1u << 25;
constexpr uint32_t kDebugEnableSkyScreenQuad = 1u << 26;
constexpr uint32_t kDebugTerrainDepthTestOff = 1u << 27;

bool DrawTraceIsEnabled();
void ResetDrawOrderFrame();

void* NewOpaqueHandle() {
  uintptr_t v = g_handle_seed.fetch_add(0x10);
  return reinterpret_cast<void*>(v);
}

void SetLastErr(DWORD err) {
  g_last_error.store(err, std::memory_order_relaxed);
}

DWORD MillisecondsSinceStart() {
  if (g_fake_time_enabled) return g_fake_time_ms;
  auto now = std::chrono::steady_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_start_tick).count();
  if (ms < 0) return 0;
  return static_cast<DWORD>(ms);
}

bool IsNullOrEmpty(const char* s) {
  return !s || *s == '\0';
}

std::string NormalizeWinPath(const char* raw) {
  if (!raw) return {};
  std::string out(raw);
  std::replace(out.begin(), out.end(), '\\', '/');
  while (out.rfind("./", 0) == 0) out.erase(0, 2);
  return out;
}

std::string ToLowerCopy(const std::string& src) {
  std::string out = src;
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

void TrackShaderFileOpen(const char* raw_path, bool success) {
  if (!raw_path) return;
  const std::string normalized = ToLowerCopy(NormalizeWinPath(raw_path));
  if (normalized.find("/shader/") == std::string::npos || normalized.rfind(".bin") != normalized.size() - 4) return;
  g_shader_file_open_attempts += 1;
  if (success) g_shader_file_open_success += 1;
  else g_shader_file_open_fail += 1;
  if (normalized.find("skinmesh") != std::string::npos) g_shader_file_open_skinmesh += 1;
  if (normalized.find("vseffect") != std::string::npos) g_shader_file_open_vseffect += 1;
  if (normalized.find("pseffect") != std::string::npos) g_shader_file_open_pseffect += 1;
}

void TrackAssetFileOpen(const char* raw_path, bool success) {
  if (!raw_path) return;
  const std::string normalized = ToLowerCopy(NormalizeWinPath(raw_path));
  g_asset_file_open_attempts += 1;
  if (success) {
    g_asset_file_open_success += 1;
    return;
  }

  g_asset_file_open_fail += 1;
  if (g_asset_file_open_fail_samples.size() < 64u &&
      g_asset_file_open_fail_seen.insert(normalized).second) {
    g_asset_file_open_fail_samples.push_back(normalized);
  }
  if (normalized.find("/mesh/") != std::string::npos) g_asset_file_open_fail_mesh += 1;
  if (normalized.find("/env/") != std::string::npos) g_asset_file_open_fail_env += 1;
  if (normalized.find("/ui/") != std::string::npos) g_asset_file_open_fail_ui += 1;
  if (normalized.find("/sound/") != std::string::npos) g_asset_file_open_fail_sound += 1;
  if (normalized.rfind(".bmp") == normalized.size() - 4 ||
      normalized.rfind(".tga") == normalized.size() - 4 ||
      normalized.rfind(".wyt") == normalized.size() - 4 ||
      normalized.rfind(".wys") == normalized.size() - 4 ||
      normalized.rfind(".dds") == normalized.size() - 4) {
    g_asset_file_open_fail_texture += 1;
  }
}

std::filesystem::path ResolveCaseInsensitivePath(const char* raw) {
  auto resolve_single = [](const std::string& input) -> std::filesystem::path {
    std::filesystem::path direct(input);
    std::error_code ec;
    if (std::filesystem::exists(direct, ec) && !ec) return direct;

    std::filesystem::path cwd = std::filesystem::current_path(ec);
    if (ec) cwd = std::filesystem::path(".");

    std::filesystem::path current = direct.is_absolute() ? direct.root_path() : cwd;
    std::filesystem::path rel = direct.is_absolute() ? direct.lexically_relative(direct.root_path()) : direct;

    for (const auto& part : rel) {
      std::string needle = part.string();
      if (needle.empty() || needle == ".") continue;
      if (needle == "..") {
        current = current.parent_path();
        continue;
      }

      std::filesystem::path found;
      for (const auto& entry : std::filesystem::directory_iterator(current, ec)) {
        if (ec) break;
        if (ToLowerCopy(entry.path().filename().string()) == ToLowerCopy(needle)) {
          found = entry.path();
          break;
        }
      }

      if (!found.empty()) {
        current = found;
      } else {
        current /= needle;
      }
    }

    return current;
  };

  auto ends_with = [](const std::string& value, const char* suffix) -> bool {
    if (!suffix) return false;
    const size_t suffix_len = std::strlen(suffix);
    if (value.size() < suffix_len) return false;
    return value.compare(value.size() - suffix_len, suffix_len, suffix) == 0;
  };

  auto build_fallback_candidates = [&](const std::string& normalized) {
    std::vector<std::string> out;
    std::string key = ToLowerCopy(normalized);
    while (!key.empty() && key.front() == '/') key.erase(key.begin());
    if (!ends_with(key, ".msh")) return out;
    if (key.rfind("mesh/or010", 0) != 0 || key.size() < 17u) return out;

    const char family = key[10];
    const char var1 = key[11];
    const char var2 = key[12];
    if (!std::isdigit(static_cast<unsigned char>(var1)) ||
        !std::isdigit(static_cast<unsigned char>(var2))) {
      return out;
    }

    auto push_candidate = [&](char fallback_family) {
      if (fallback_family == family) return;
      std::string candidate = "mesh/or010";
      candidate.push_back(fallback_family);
      candidate.push_back(var1);
      candidate.push_back(var2);
      candidate += ".msh";
      out.push_back(candidate);
    };

    switch (family) {
      case '1': push_candidate('2'); break;
      case '2': push_candidate('1'); break;
      case '3':
      case '5':
        push_candidate('1');
        push_candidate('2');
        break;
      case '4':
      case '6':
        push_candidate('2');
        push_candidate('1');
        break;
      default:
        break;
    }

    return out;
  };

  auto track_fallback = [&](const std::string& requested, const std::string& resolved) {
    if (requested.empty() || resolved.empty() || requested == resolved) return;
    if (requested.rfind("mesh/or010", 0) == 0 && resolved.rfind("mesh/or010", 0) == 0) {
      g_asset_path_fallback_or010_hits += 1;
    }
    if (g_asset_path_fallback_samples.size() < 64u) {
      const std::string sample = requested + " => " + resolved;
      if (g_asset_path_fallback_seen.insert(sample).second) {
        g_asset_path_fallback_samples.push_back(sample);
      }
    }
  };

  std::string normalized = NormalizeWinPath(raw);
  if (normalized.empty()) return {};

  std::filesystem::path resolved = resolve_single(normalized);
  std::error_code ec;
  if (std::filesystem::exists(resolved, ec) && !ec) return resolved;

  const std::string normalized_lower = ToLowerCopy(normalized);
  const std::vector<std::string> fallback_candidates = build_fallback_candidates(normalized);
  if (!fallback_candidates.empty()) {
    g_asset_path_fallback_attempts += static_cast<uint64_t>(fallback_candidates.size());
  }

  for (const auto& candidate : fallback_candidates) {
    std::filesystem::path fallback = resolve_single(candidate);
    ec.clear();
    if (std::filesystem::exists(fallback, ec) && !ec) {
      g_asset_path_fallback_hits += 1;
      track_fallback(normalized_lower, ToLowerCopy(NormalizeWinPath(fallback.string().c_str())));
      return fallback;
    }
  }

  return resolved;
}

std::string ToNarrow(const wchar_t* src) {
  if (!src) return {};
  std::string out;
  while (*src) {
    wchar_t wc = *src++;
    out.push_back((wc >= 0 && wc <= 0x7F) ? static_cast<char>(wc) : '?');
  }
  return out;
}

void FillFindData(const std::filesystem::path& p, WIN32_FIND_DATAA* out) {
  if (!out) return;
  std::memset(out, 0, sizeof(*out));
  auto name = p.filename().string();
  std::strncpy(out->cFileName, name.c_str(), MAX_PATH - 1);
  std::error_code ec;
  auto st = std::filesystem::status(p, ec);
  if (!ec && std::filesystem::is_directory(st)) {
    out->dwFileAttributes = kFileAttributeDirectory;
  } else {
    out->dwFileAttributes = kFileAttributeNormal;
    if (!ec) {
      auto sz = std::filesystem::file_size(p, ec);
      if (!ec) {
        out->nFileSizeLow = static_cast<DWORD>(sz & 0xFFFFFFFFu);
        out->nFileSizeHigh = static_cast<DWORD>((sz >> 32) & 0xFFFFFFFFu);
      }
    }
  }
}

std::vector<std::filesystem::path> EnumeratePattern(const char* pattern) {
  std::vector<std::filesystem::path> out;
  if (IsNullOrEmpty(pattern)) return out;

  std::filesystem::path pat_path(pattern);
  std::filesystem::path dir = pat_path.parent_path();
  std::string file_pattern = pat_path.filename().string();

  if (dir.empty()) dir = std::filesystem::current_path();
  std::error_code ec;
  if (!std::filesystem::exists(dir, ec) || ec) return out;

  bool has_wildcard = file_pattern.find('*') != std::string::npos || file_pattern.find('?') != std::string::npos;
  if (!has_wildcard) {
    std::filesystem::path candidate = dir / file_pattern;
    if (std::filesystem::exists(candidate, ec) && !ec) out.push_back(candidate);
    return out;
  }

  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (ec) break;
    auto name = entry.path().filename().string();
    if (fnmatch(file_pattern.c_str(), name.c_str(), 0) == 0) out.push_back(entry.path());
  }
  return out;
}

class ComRefBase {
 public:
  virtual ~ComRefBase() = default;
  ULONG AddRefImpl() { return ++ref_count_; }
  ULONG ReleaseImpl() {
    ULONG v = --ref_count_;
    if (v == 0) delete this;
    return v;
  }
  HRESULT QueryInterfaceImpl(void** ppvObject) {
    if (!ppvObject) return E_POINTER;
    *ppvObject = this;
    AddRefImpl();
    return S_OK;
  }

 private:
  std::atomic<ULONG> ref_count_{1};
};

class DummyDirect3D9 final : public IDirect3D9, private ComRefBase {
 public:
  HRESULT QueryInterface(REFIID, void** ppvObject) override { return QueryInterfaceImpl(ppvObject); }
  ULONG AddRef() override { return AddRefImpl(); }
  ULONG Release() override { return ReleaseImpl(); }
};

class DummyDirect3DDevice9 final : public IDirect3DDevice9, private ComRefBase {
 public:
  HRESULT QueryInterface(REFIID, void** ppvObject) override { return QueryInterfaceImpl(ppvObject); }
  ULONG AddRef() override { return AddRefImpl(); }
  ULONG Release() override { return ReleaseImpl(); }
};

int BytesPerPixelForFormat(D3DFORMAT format) {
  switch (format) {
    case D3DFMT_R5G6B5:
    case D3DFMT_X1R5G5B5:
    case D3DFMT_A1R5G5B5:
    case D3DFMT_A4R4G4B4:
      return 2;
    default:
      return 4;
  }
}

class DummyDirect3DTexture9;

class DummyDirect3DSurface9 final : public IDirect3DSurface9, private ComRefBase {
 public:
  DummyDirect3DSurface9() = default;
  ~DummyDirect3DSurface9() override;

  HRESULT QueryInterface(REFIID, void** ppvObject) override { return QueryInterfaceImpl(ppvObject); }
  ULONG AddRef() override { return AddRefImpl(); }
  ULONG Release() override { return ReleaseImpl(); }

  DummyDirect3DTexture9* owner = nullptr;
  UINT width = 1;
  UINT height = 1;
  D3DFORMAT format = D3DFMT_A8R8G8B8;
  UINT pitch = 4;
  std::vector<uint8_t> stand_alone_data;
  bool locked = false;
};

class DummyDirect3DVertexBuffer9 final : public IDirect3DVertexBuffer9, private ComRefBase {
 public:
  DummyDirect3DVertexBuffer9(UINT byte_size, DWORD in_usage, DWORD in_fvf, D3DPOOL in_pool)
      : usage(in_usage), fvf(in_fvf), pool(in_pool), data(byte_size, 0) {}

  HRESULT QueryInterface(REFIID, void** ppvObject) override { return QueryInterfaceImpl(ppvObject); }
  ULONG AddRef() override { return AddRefImpl(); }
  ULONG Release() override { return ReleaseImpl(); }

  DWORD usage = 0;
  DWORD fvf = 0;
  D3DPOOL pool = D3DPOOL_MANAGED;
  std::vector<uint8_t> data;
  bool locked = false;
};

class DummyDirect3DIndexBuffer9 final : public IDirect3DIndexBuffer9, private ComRefBase {
 public:
  DummyDirect3DIndexBuffer9(UINT byte_size, DWORD in_usage, D3DFORMAT in_format, D3DPOOL in_pool)
      : usage(in_usage), format(in_format), pool(in_pool), data(byte_size, 0) {}

  HRESULT QueryInterface(REFIID, void** ppvObject) override { return QueryInterfaceImpl(ppvObject); }
  ULONG AddRef() override { return AddRefImpl(); }
  ULONG Release() override { return ReleaseImpl(); }

  DWORD usage = 0;
  D3DFORMAT format = D3DFMT_INDEX16;
  D3DPOOL pool = D3DPOOL_MANAGED;
  std::vector<uint8_t> data;
  bool locked = false;
};

class DummyDirect3DVertexDeclaration9 final : public IDirect3DVertexDeclaration9, private ComRefBase {
 public:
  DummyDirect3DVertexDeclaration9() = default;
  explicit DummyDirect3DVertexDeclaration9(const D3DVERTEXELEMENT9* declaration) {
    if (!declaration) return;
    constexpr size_t kMaxElements = 64;
    for (size_t i = 0; i < kMaxElements; ++i) {
      const D3DVERTEXELEMENT9 elem = declaration[i];
      elements.push_back(elem);
      if (elem.Stream == 0xFFu && elem.Type == 17u) break;
    }
  }

  HRESULT QueryInterface(REFIID, void** ppvObject) override { return QueryInterfaceImpl(ppvObject); }
  ULONG AddRef() override { return AddRefImpl(); }
  ULONG Release() override { return ReleaseImpl(); }

  std::vector<D3DVERTEXELEMENT9> elements;
};

uint64_t HashShaderBytecode(const std::vector<DWORD>& bytecode);

class DummyDirect3DVertexShader9 final : public IDirect3DVertexShader9, private ComRefBase {
 public:
  explicit DummyDirect3DVertexShader9(const DWORD* function_code) {
    if (!function_code) return;
    // Preserve bytecode blob for future translation/execution paths.
    const size_t max_dwords = 4096;
    for (size_t i = 0; i < max_dwords; ++i) {
      const DWORD token = function_code[i];
      bytecode.push_back(token);
      // DX9 shader end token.
      if (token == 0x0000FFFFu) break;
    }
    dword_count = static_cast<uint32_t>(bytecode.size());
    version_token = bytecode.empty() ? 0u : bytecode[0];
    hash = HashShaderBytecode(bytecode);
  }

  HRESULT QueryInterface(REFIID, void** ppvObject) override { return QueryInterfaceImpl(ppvObject); }
  ULONG AddRef() override { return AddRefImpl(); }
  ULONG Release() override { return ReleaseImpl(); }

  std::vector<DWORD> bytecode;
  uint64_t hash = 0;
  uint32_t dword_count = 0;
  uint32_t version_token = 0;
};

class DummyDirect3DPixelShader9 final : public IDirect3DPixelShader9, private ComRefBase {
 public:
  explicit DummyDirect3DPixelShader9(const DWORD* function_code) {
    if (!function_code) return;
    const size_t max_dwords = 4096;
    for (size_t i = 0; i < max_dwords; ++i) {
      const DWORD token = function_code[i];
      bytecode.push_back(token);
      if (token == 0x0000FFFFu) break;
    }
    dword_count = static_cast<uint32_t>(bytecode.size());
    version_token = bytecode.empty() ? 0u : bytecode[0];
    hash = HashShaderBytecode(bytecode);
  }

  HRESULT QueryInterface(REFIID, void** ppvObject) override { return QueryInterfaceImpl(ppvObject); }
  ULONG AddRef() override { return AddRefImpl(); }
  ULONG Release() override { return ReleaseImpl(); }

  std::vector<DWORD> bytecode;
  uint64_t hash = 0;
  uint32_t dword_count = 0;
  uint32_t version_token = 0;
};

class DummyDirect3DTexture9 final : public IDirect3DTexture9, private ComRefBase {
 public:
  DummyDirect3DTexture9(UINT in_width, UINT in_height, D3DFORMAT in_format, D3DPOOL in_pool)
      : width(std::max<UINT>(1, in_width)),
        height(std::max<UINT>(1, in_height)),
        format(in_format == D3DFMT_UNKNOWN ? D3DFMT_A8R8G8B8 : in_format),
        pool(in_pool) {
    bytes_per_pixel = BytesPerPixelForFormat(format);
    pitch = width * static_cast<UINT>(bytes_per_pixel);
    pixels.resize(static_cast<size_t>(pitch) * static_cast<size_t>(height), 0xFF);
  }

  ~DummyDirect3DTexture9() override {
    if (gl_texture != 0) {
      glDeleteTextures(1, &gl_texture);
      gl_texture = 0;
    }
  }

  HRESULT QueryInterface(REFIID, void** ppvObject) override { return QueryInterfaceImpl(ppvObject); }
  ULONG AddRef() override { return AddRefImpl(); }
  ULONG Release() override { return ReleaseImpl(); }

  UINT width = 1;
  UINT height = 1;
  D3DFORMAT format = D3DFMT_A8R8G8B8;
  D3DPOOL pool = D3DPOOL_MANAGED;
  int bytes_per_pixel = 4;
  UINT pitch = 4;
  std::vector<uint8_t> pixels;
  bool locked = false;
  GLuint gl_texture = 0;
  bool gl_dirty = true;
  std::string debug_source_path;
};

DummyDirect3DSurface9::~DummyDirect3DSurface9() {
  if (owner) {
    owner->Release();
    owner = nullptr;
  }
}

class DummyDirectSound8 final : public IDirectSound8, private ComRefBase {
 public:
  HRESULT QueryInterface(REFIID, void** ppvObject) override { return QueryInterfaceImpl(ppvObject); }
  ULONG AddRef() override { return AddRefImpl(); }
  ULONG Release() override { return ReleaseImpl(); }
};

class DummyDirectInputDevice8 final : public IDirectInputDevice8A, private ComRefBase {
 public:
  HRESULT QueryInterface(REFIID, void** ppvObject) override { return QueryInterfaceImpl(ppvObject); }
  ULONG AddRef() override { return AddRefImpl(); }
  ULONG Release() override { return ReleaseImpl(); }

  HRESULT GetCapabilities(DIDEVCAPS* lpDIDevCaps) override {
    if (lpDIDevCaps) {
      if (lpDIDevCaps->dwSize >= sizeof(DIDEVCAPS)) {
        lpDIDevCaps->dwAxes = 2;
        lpDIDevCaps->dwButtons = 3;
      }
    }
    return S_OK;
  }

  HRESULT Acquire() override { return S_OK; }
  HRESULT Unacquire() override { return S_OK; }

  HRESULT GetDeviceState(DWORD cbData, LPVOID lpvData) override {
    if (lpvData && cbData > 0) std::memset(lpvData, 0, cbData);
    return S_OK;
  }

  HRESULT SetDataFormat(const DIDATAFORMAT*) override { return S_OK; }
  HRESULT SetCooperativeLevel(HWND, DWORD) override { return S_OK; }
};

class DummyDirectInput8 final : public IDirectInput8A, private ComRefBase {
 public:
  HRESULT QueryInterface(REFIID, void** ppvObject) override { return QueryInterfaceImpl(ppvObject); }
  ULONG AddRef() override { return AddRefImpl(); }
  ULONG Release() override { return ReleaseImpl(); }

  HRESULT CreateDevice(REFGUID, IDirectInputDevice8A** lplpDirectInputDevice, IUnknown*) override {
    if (!lplpDirectInputDevice) return E_POINTER;
    *lplpDirectInputDevice = new DummyDirectInputDevice8();
    return S_OK;
  }
};

class DummyD3DXBuffer final : public ID3DXBuffer, private ComRefBase {
 public:
  explicit DummyD3DXBuffer(DWORD n) {
    size = n;
    if (n == 0) {
      data = nullptr;
      return;
    }
    data = std::calloc(1, n);
  }

  ~DummyD3DXBuffer() override {
    if (data) {
      std::free(data);
      data = nullptr;
      size = 0;
    }
  }

  HRESULT QueryInterface(REFIID, void** ppvObject) override { return QueryInterfaceImpl(ppvObject); }
  ULONG AddRef() override { return AddRefImpl(); }
  ULONG Release() override { return ReleaseImpl(); }
};

class DummyD3DXSprite final : public ID3DXSprite, private ComRefBase {
 public:
  explicit DummyD3DXSprite(IDirect3DDevice9* device) : device_(device) {
    if (device_) device_->AddRef();
  }
  ~DummyD3DXSprite() override {
    if (device_) {
      device_->Release();
      device_ = nullptr;
    }
  }

  HRESULT QueryInterface(REFIID, void** ppvObject) override { return QueryInterfaceImpl(ppvObject); }
  ULONG AddRef() override { return AddRefImpl(); }
  ULONG Release() override { return ReleaseImpl(); }

  HRESULT GetDevice(IDirect3DDevice9** pp_device) override {
    if (!pp_device) return E_POINTER;
    *pp_device = device_;
    if (device_) device_->AddRef();
    return device_ ? S_OK : D3DERR_INVALIDCALL;
  }

  HRESULT Begin(DWORD) override {
    if (!device_) return D3DERR_INVALIDCALL;
    if (!snapshot_valid_) {
      SaveDeviceState();
      snapshot_valid_ = true;
    }
    in_begin_ = true;
    return S_OK;
  }

  HRESULT Draw(
      IDirect3DTexture9* texture,
      const RECT* src_rect_opt,
      const D3DXVECTOR2* scaling_opt,
      const D3DXVECTOR2* rotation_center_opt,
      float rotation,
      const D3DXVECTOR2* translation_opt,
      D3DCOLOR color) override {
    if (!device_ || !texture) return D3DERR_INVALIDCALL;
    if (!in_begin_) return D3DERR_INVALIDCALL;

    D3DSURFACE_DESC desc{};
    if (FAILED(texture->GetLevelDesc(0, &desc))) return D3DERR_INVALIDCALL;
    const float tex_w = std::max(1.0f, static_cast<float>(desc.Width));
    const float tex_h = std::max(1.0f, static_cast<float>(desc.Height));

    RECT src{};
    if (src_rect_opt) src = *src_rect_opt;
    else {
      src.left = 0;
      src.top = 0;
      src.right = static_cast<LONG>(desc.Width);
      src.bottom = static_cast<LONG>(desc.Height);
    }
    if (src.right <= src.left || src.bottom <= src.top) return S_OK;

    const D3DXVECTOR2 scaling = scaling_opt ? *scaling_opt : D3DXVECTOR2(1.0f, 1.0f);
    const D3DXVECTOR2 rotation_center =
        rotation_center_opt ? *rotation_center_opt : D3DXVECTOR2(0.0f, 0.0f);
    const D3DXVECTOR2 translation =
        translation_opt ? *translation_opt : D3DXVECTOR2(0.0f, 0.0f);

    struct SpriteVertex {
      float x;
      float y;
      float z;
      float rhw;
      D3DCOLOR diffuse;
      float u;
      float v;
    };

    auto transform_point = [&](float sx, float sy, float* out_x, float* out_y) {
      const float dx = (sx - rotation_center.x) * scaling.x;
      const float dy = (sy - rotation_center.y) * scaling.y;
      const float cs = std::cos(rotation);
      const float sn = std::sin(rotation);
      const float rx = dx * cs - dy * sn;
      const float ry = dx * sn + dy * cs;
      *out_x = translation.x + rotation_center.x * scaling.x + rx;
      *out_y = translation.y + rotation_center.y * scaling.y + ry;
    };

    float x0 = 0.0f, y0 = 0.0f;
    float x1 = 0.0f, y1 = 0.0f;
    float x2 = 0.0f, y2 = 0.0f;
    float x3 = 0.0f, y3 = 0.0f;
    transform_point(static_cast<float>(src.left), static_cast<float>(src.top), &x0, &y0);
    transform_point(static_cast<float>(src.right), static_cast<float>(src.top), &x1, &y1);
    transform_point(static_cast<float>(src.right), static_cast<float>(src.bottom), &x2, &y2);
    transform_point(static_cast<float>(src.left), static_cast<float>(src.bottom), &x3, &y3);

    const float u0 = static_cast<float>(src.left) / tex_w;
    const float v0 = static_cast<float>(src.top) / tex_h;
    const float u1 = static_cast<float>(src.right) / tex_w;
    const float v1 = static_cast<float>(src.bottom) / tex_h;

    SpriteVertex verts[4]{};
    verts[0] = {x0, y0, 0.0f, 1.0f, color, u0, v0};
    verts[1] = {x1, y1, 0.0f, 1.0f, color, u1, v0};
    verts[2] = {x2, y2, 0.0f, 1.0f, color, u1, v1};
    verts[3] = {x3, y3, 0.0f, 1.0f, color, u0, v1};

    // D3DXSprite default path is a 2D overlay pass; force common sprite states.
    device_->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    device_->SetRenderState(D3DRS_ZWRITEENABLE, 0u);
    device_->SetRenderState(D3DRS_ALPHABLENDENABLE, 1u);
    device_->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device_->SetRenderState(D3DRS_ALPHATESTENABLE, 0u);
    device_->SetRenderState(D3DRS_LIGHTING, 0u);
    device_->SetVertexShader(nullptr);
    device_->SetPixelShader(nullptr);

    device_->SetTexture(0, texture);
    device_->SetTexture(1, nullptr);
    device_->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    device_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    device_->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    device_->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    device_->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    device_->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    device_->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    device_->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    device_->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0u);
    device_->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 1u);
    device_->SetFVF(324u);  // D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1
    return device_->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2u, verts, sizeof(SpriteVertex));
  }

  HRESULT DrawTransform(
      IDirect3DTexture9* texture,
      const RECT* src_rect,
      const D3DXMATRIX* transform,
      D3DCOLOR color) override {
    if (!transform) return D3DERR_INVALIDCALL;
    const D3DXVECTOR2 unit_scale(1.0f, 1.0f);
    D3DXVECTOR2 center(0.0f, 0.0f);
    D3DXVECTOR2 translation(transform->_41, transform->_42);
    return Draw(texture, src_rect, &unit_scale, &center, 0.0f, &translation, color);
  }

  HRESULT End() override {
    if (snapshot_valid_) {
      RestoreDeviceState();
      ReleaseSnapshotRefs();
      snapshot_valid_ = false;
    }
    in_begin_ = false;
    return S_OK;
  }

  HRESULT OnLostDevice() override { return S_OK; }
  HRESULT OnResetDevice() override { return S_OK; }

 private:
  struct SpriteStateSnapshot {
    DWORD current_fvf = 0;
    bool z_enable = true;
    bool z_write_enable = true;
    DWORD src_blend = D3DBLEND_SRCALPHA;
    DWORD dst_blend = D3DBLEND_INVSRCALPHA;
    bool blend_enabled = false;
    DWORD alpha_test_enable = 0;
    DWORD alpha_ref = 0;
    DWORD alpha_func = D3DCMP_ALWAYS;
    DWORD lighting_enable = 0;
    IDirect3DBaseTexture9* texture0 = nullptr;
    IDirect3DBaseTexture9* texture1 = nullptr;
    std::array<DWORD, 32> stage0{};
    std::array<DWORD, 32> stage1{};
    IDirect3DVertexShader9* vertex_shader = nullptr;
    IDirect3DPixelShader9* pixel_shader = nullptr;
  };

  void SaveDeviceState();
  void RestoreDeviceState();
  void ReleaseSnapshotRefs();

  IDirect3DDevice9* device_ = nullptr;
  bool in_begin_ = false;
  bool snapshot_valid_ = false;
  SpriteStateSnapshot saved_state_{};
};

float Clamp01(float t) {
  return std::max(0.0f, std::min(1.0f, t));
}

float FloatFromDWORD(DWORD value) {
  float out = 0.0f;
  std::memcpy(&out, &value, sizeof(out));
  return out;
}

D3DXVECTOR3 Normalize3(const D3DXVECTOR3& v) {
  float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
  if (len <= 1.0e-7f) return D3DXVECTOR3(0.0f, 0.0f, 0.0f);
  float inv = 1.0f / len;
  return D3DXVECTOR3(v.x * inv, v.y * inv, v.z * inv);
}

float Dot3(const D3DXVECTOR3& a, const D3DXVECTOR3& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

D3DXVECTOR3 Cross3(const D3DXVECTOR3& a, const D3DXVECTOR3& b) {
  return D3DXVECTOR3(
      a.y * b.z - a.z * b.y,
      a.z * b.x - a.x * b.z,
      a.x * b.y - a.y * b.x);
}

uint16_t ReadLE16(const uint8_t* p) {
  if (!p) return 0;
  return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

uint32_t ReadLE32(const uint8_t* p) {
  if (!p) return 0;
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

void Decode565(uint16_t c, uint8_t* r, uint8_t* g, uint8_t* b) {
  const uint8_t r5 = static_cast<uint8_t>((c >> 11) & 0x1Fu);
  const uint8_t g6 = static_cast<uint8_t>((c >> 5) & 0x3Fu);
  const uint8_t b5 = static_cast<uint8_t>(c & 0x1Fu);
  if (r) *r = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
  if (g) *g = static_cast<uint8_t>((g6 << 2) | (g6 >> 4));
  if (b) *b = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
}

bool DecodeTGAtoRGBA(const uint8_t* data, size_t size, uint32_t* out_w, uint32_t* out_h, std::vector<uint8_t>* out_rgba) {
  if (!data || size < 18 || !out_w || !out_h || !out_rgba) return false;

  const uint8_t id_len = data[0];
  const uint8_t color_map_type = data[1];
  const uint8_t image_type = data[2];
  if (color_map_type != 0) return false;
  if (image_type != 2 && image_type != 10) return false;

  const uint16_t width = ReadLE16(data + 12);
  const uint16_t height = ReadLE16(data + 14);
  const uint8_t bpp = data[16];
  if (width == 0 || height == 0) return false;
  if (bpp != 24 && bpp != 32) return false;

  const bool top_origin = (data[17] & 0x20u) != 0;
  const size_t px_bytes = static_cast<size_t>(bpp / 8);
  size_t cursor = 18u + static_cast<size_t>(id_len);
  if (cursor >= size) return false;

  out_rgba->assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0);

  auto write_pixel = [&](size_t sequential_index, uint8_t b, uint8_t g, uint8_t r, uint8_t a) {
    const uint32_t x = static_cast<uint32_t>(sequential_index % width);
    const uint32_t src_y = static_cast<uint32_t>(sequential_index / width);
    const uint32_t y = top_origin ? src_y : (static_cast<uint32_t>(height) - 1u - src_y);
    const size_t dst = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u;
    (*out_rgba)[dst + 0] = r;
    (*out_rgba)[dst + 1] = g;
    (*out_rgba)[dst + 2] = b;
    (*out_rgba)[dst + 3] = a;
  };

  const size_t total = static_cast<size_t>(width) * static_cast<size_t>(height);
  size_t written = 0;

  if (image_type == 2) {
    const size_t need = total * px_bytes;
    if (cursor + need > size) return false;
    for (size_t i = 0; i < total; ++i) {
      const uint8_t b = data[cursor + i * px_bytes + 0];
      const uint8_t g = data[cursor + i * px_bytes + 1];
      const uint8_t r = data[cursor + i * px_bytes + 2];
      const uint8_t a = (px_bytes == 4) ? data[cursor + i * px_bytes + 3] : 0xFFu;
      write_pixel(i, b, g, r, a);
    }
    *out_w = width;
    *out_h = height;
    return true;
  }

  while (written < total && cursor < size) {
    const uint8_t packet = data[cursor++];
    const size_t run = static_cast<size_t>((packet & 0x7Fu) + 1u);
    if ((packet & 0x80u) != 0u) {
      if (cursor + px_bytes > size) return false;
      const uint8_t b = data[cursor + 0];
      const uint8_t g = data[cursor + 1];
      const uint8_t r = data[cursor + 2];
      const uint8_t a = (px_bytes == 4) ? data[cursor + 3] : 0xFFu;
      cursor += px_bytes;
      for (size_t i = 0; i < run && written < total; ++i, ++written) {
        write_pixel(written, b, g, r, a);
      }
    } else {
      const size_t block = run * px_bytes;
      if (cursor + block > size) return false;
      for (size_t i = 0; i < run && written < total; ++i, ++written) {
        const size_t off = cursor + i * px_bytes;
        const uint8_t b = data[off + 0];
        const uint8_t g = data[off + 1];
        const uint8_t r = data[off + 2];
        const uint8_t a = (px_bytes == 4) ? data[off + 3] : 0xFFu;
        write_pixel(written, b, g, r, a);
      }
      cursor += block;
    }
  }

  if (written != total) return false;
  *out_w = width;
  *out_h = height;
  return true;
}

void DecodeDXTColorBlock(const uint8_t* block, uint8_t out_rgba[16][4], bool dxt1_alpha_mode) {
  const uint16_t c0 = ReadLE16(block + 0);
  const uint16_t c1 = ReadLE16(block + 2);
  const uint32_t bits = ReadLE32(block + 4);

  uint8_t palette[4][4]{};
  Decode565(c0, &palette[0][0], &palette[0][1], &palette[0][2]);
  palette[0][3] = 255;
  Decode565(c1, &palette[1][0], &palette[1][1], &palette[1][2]);
  palette[1][3] = 255;

  if (dxt1_alpha_mode && c0 <= c1) {
    palette[2][0] = static_cast<uint8_t>((palette[0][0] + palette[1][0]) / 2u);
    palette[2][1] = static_cast<uint8_t>((palette[0][1] + palette[1][1]) / 2u);
    palette[2][2] = static_cast<uint8_t>((palette[0][2] + palette[1][2]) / 2u);
    palette[2][3] = 255;
    palette[3][0] = 0;
    palette[3][1] = 0;
    palette[3][2] = 0;
    palette[3][3] = 0;
  } else {
    palette[2][0] = static_cast<uint8_t>((2u * palette[0][0] + palette[1][0]) / 3u);
    palette[2][1] = static_cast<uint8_t>((2u * palette[0][1] + palette[1][1]) / 3u);
    palette[2][2] = static_cast<uint8_t>((2u * palette[0][2] + palette[1][2]) / 3u);
    palette[2][3] = 255;
    palette[3][0] = static_cast<uint8_t>((palette[0][0] + 2u * palette[1][0]) / 3u);
    palette[3][1] = static_cast<uint8_t>((palette[0][1] + 2u * palette[1][1]) / 3u);
    palette[3][2] = static_cast<uint8_t>((palette[0][2] + 2u * palette[1][2]) / 3u);
    palette[3][3] = 255;
  }

  for (int i = 0; i < 16; ++i) {
    const uint32_t idx = (bits >> (i * 2)) & 0x3u;
    out_rgba[i][0] = palette[idx][0];
    out_rgba[i][1] = palette[idx][1];
    out_rgba[i][2] = palette[idx][2];
    out_rgba[i][3] = palette[idx][3];
  }
}

bool DecodeDDSDXTtoRGBA(const uint8_t* data, size_t size, uint32_t* out_w, uint32_t* out_h, std::vector<uint8_t>* out_rgba) {
  if (!data || size < 128 || !out_w || !out_h || !out_rgba) return false;
  if (std::memcmp(data, "DDS ", 4) != 0) return false;

  const uint32_t height = ReadLE32(data + 12);
  const uint32_t width = ReadLE32(data + 16);
  if (width == 0 || height == 0 || width > 8192 || height > 8192) return false;

  const uint32_t fourcc = ReadLE32(data + 84);
  const uint32_t kDXT1 = static_cast<uint32_t>('D') |
                         (static_cast<uint32_t>('X') << 8) |
                         (static_cast<uint32_t>('T') << 16) |
                         (static_cast<uint32_t>('1') << 24);
  const uint32_t kDXT3 = static_cast<uint32_t>('D') |
                         (static_cast<uint32_t>('X') << 8) |
                         (static_cast<uint32_t>('T') << 16) |
                         (static_cast<uint32_t>('3') << 24);
  if (fourcc != kDXT1 && fourcc != kDXT3) return false;

  const bool is_dxt1 = (fourcc == kDXT1);
  const size_t block_bytes = is_dxt1 ? 8u : 16u;
  const uint32_t bx_count = (width + 3u) / 4u;
  const uint32_t by_count = (height + 3u) / 4u;
  const size_t required = 128u + static_cast<size_t>(bx_count) * static_cast<size_t>(by_count) * block_bytes;
  if (required > size) return false;

  out_rgba->assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0);
  const uint8_t* src = data + 128;

  for (uint32_t by = 0; by < by_count; ++by) {
    for (uint32_t bx = 0; bx < bx_count; ++bx) {
      uint8_t px[16][4]{};
      if (is_dxt1) {
        DecodeDXTColorBlock(src, px, true);
      } else {
        uint8_t alpha[16]{};
        for (int i = 0; i < 16; ++i) {
          const uint8_t nibble = static_cast<uint8_t>((src[i / 2] >> ((i % 2) * 4)) & 0xFu);
          alpha[i] = static_cast<uint8_t>((nibble << 4) | nibble);
        }
        DecodeDXTColorBlock(src + 8, px, false);
        for (int i = 0; i < 16; ++i) px[i][3] = alpha[i];
      }

      for (uint32_t py = 0; py < 4; ++py) {
        for (uint32_t pxi = 0; pxi < 4; ++pxi) {
          const uint32_t x = bx * 4u + pxi;
          const uint32_t y = by * 4u + py;
          if (x >= width || y >= height) continue;
          const size_t dst = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 4u;
          const int block_i = static_cast<int>(py * 4u + pxi);
          (*out_rgba)[dst + 0] = px[block_i][0];
          (*out_rgba)[dst + 1] = px[block_i][1];
          (*out_rgba)[dst + 2] = px[block_i][2];
          (*out_rgba)[dst + 3] = px[block_i][3];
        }
      }

      src += block_bytes;
    }
  }

  *out_w = width;
  *out_h = height;
  return true;
}

bool DecodeTextureToRGBA(const void* src_data, size_t src_size, uint32_t* out_w, uint32_t* out_h, std::vector<uint8_t>* out_rgba) {
  if (!src_data || src_size == 0) return false;
  const auto* bytes = static_cast<const uint8_t*>(src_data);
  if (DecodeTGAtoRGBA(bytes, src_size, out_w, out_h, out_rgba)) return true;
  if (DecodeDDSDXTtoRGBA(bytes, src_size, out_w, out_h, out_rgba)) return true;
  return false;
}

bool TextureAlphaAllZero(const std::vector<uint8_t>& rgba) {
  if (rgba.empty()) return false;
  for (size_t i = 3; i < rgba.size(); i += 4) {
    if (rgba[i] != 0u) return false;
  }
  return true;
}

void PackRGBAForTexture(
    const std::vector<uint8_t>& rgba,
    uint32_t src_w,
    uint32_t src_h,
    DummyDirect3DTexture9* texture) {
  if (!texture || src_w == 0 || src_h == 0 || rgba.size() < static_cast<size_t>(src_w) * static_cast<size_t>(src_h) * 4u) {
    return;
  }

  texture->pixels.assign(static_cast<size_t>(texture->pitch) * static_cast<size_t>(texture->height), 0);

  for (UINT y = 0; y < texture->height; ++y) {
    const uint32_t src_y = static_cast<uint32_t>((static_cast<uint64_t>(y) * src_h) / std::max<uint32_t>(1u, texture->height));
    uint8_t* row = texture->pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(texture->pitch);
    for (UINT x = 0; x < texture->width; ++x) {
      const uint32_t src_x = static_cast<uint32_t>((static_cast<uint64_t>(x) * src_w) / std::max<uint32_t>(1u, texture->width));
      const size_t src_i = (static_cast<size_t>(src_y) * static_cast<size_t>(src_w) + static_cast<size_t>(src_x)) * 4u;
      const uint8_t r = rgba[src_i + 0];
      const uint8_t g = rgba[src_i + 1];
      const uint8_t b = rgba[src_i + 2];
      const uint8_t a = rgba[src_i + 3];
      uint8_t* dst = row + static_cast<size_t>(x) * static_cast<size_t>(texture->bytes_per_pixel);

      switch (texture->format) {
        case D3DFMT_R5G6B5: {
          const uint16_t v = static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
          std::memcpy(dst, &v, sizeof(v));
          break;
        }
        case D3DFMT_X1R5G5B5:
        case D3DFMT_A1R5G5B5: {
          uint16_t v = static_cast<uint16_t>(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
          if (texture->format == D3DFMT_A1R5G5B5 && a >= 128) v |= 0x8000u;
          if (texture->format == D3DFMT_X1R5G5B5) v |= 0x8000u;
          std::memcpy(dst, &v, sizeof(v));
          break;
        }
        case D3DFMT_A4R4G4B4: {
          const uint16_t v = static_cast<uint16_t>(((a >> 4) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
          std::memcpy(dst, &v, sizeof(v));
          break;
        }
        case D3DFMT_A8B8G8R8:
          dst[0] = r;
          dst[1] = g;
          dst[2] = b;
          dst[3] = a;
          break;
        case D3DFMT_X8R8G8B8:
          dst[0] = b;
          dst[1] = g;
          dst[2] = r;
          dst[3] = 0xFFu;
          break;
        case D3DFMT_A8R8G8B8:
        default:
          dst[0] = b;
          dst[1] = g;
          dst[2] = r;
          dst[3] = a;
          break;
      }
    }
  }

  texture->gl_dirty = true;
}

void ApplyColorKeyRGBA(std::vector<uint8_t>* rgba, D3DCOLOR color_key) {
  if (!rgba || rgba->empty() || color_key == 0) return;
  const uint8_t key_r = static_cast<uint8_t>((color_key >> 16) & 0xFFu);
  const uint8_t key_g = static_cast<uint8_t>((color_key >> 8) & 0xFFu);
  const uint8_t key_b = static_cast<uint8_t>(color_key & 0xFFu);

  for (size_t i = 0; i + 3 < rgba->size(); i += 4) {
    if ((*rgba)[i + 0] == key_r &&
        (*rgba)[i + 1] == key_g &&
        (*rgba)[i + 2] == key_b) {
      (*rgba)[i + 3] = 0;
    }
  }
}

}  // namespace

extern "C" {

int MessageBoxA(HWND, LPCSTR lpText, LPCSTR lpCaption, UINT) {
  std::fprintf(stderr, "[MessageBoxA] %s: %s\n", lpCaption ? lpCaption : "msg", lpText ? lpText : "");
  return 1;
}

int MessageBoxW(HWND, LPCWSTR lpText, LPCWSTR lpCaption, UINT) {
  auto text = ToNarrow(lpText);
  auto cap = ToNarrow(lpCaption);
  std::fprintf(stderr, "[MessageBoxW] %s: %s\n", cap.c_str(), text.c_str());
  return 1;
}

DWORD GetTickCount(void) { return MillisecondsSinceStart(); }
DWORD timeGetTime(void) { return MillisecondsSinceStart(); }

void wyd_debug_set_fake_time(DWORD ms) {
  g_fake_time_ms = ms;
  g_fake_time_enabled = true;
}

void wyd_debug_advance_fake_time(DWORD delta_ms) {
  if (!g_fake_time_enabled) {
    g_fake_time_ms = MillisecondsSinceStart();
    g_fake_time_enabled = true;
  }
  g_fake_time_ms += delta_ms;
}

void wyd_debug_clear_fake_time() {
  g_fake_time_enabled = false;
  g_fake_time_ms = 0;
}

DWORD wyd_debug_get_time() {
  return MillisecondsSinceStart();
}

void wyd_debug_set_demo_camera_offset(float dx, float dy, float dz, float dh, float dv) {
  g_debug_demo_camera_offset_x = dx;
  g_debug_demo_camera_offset_y = dy;
  g_debug_demo_camera_offset_z = dz;
  g_debug_demo_camera_offset_h = dh;
  g_debug_demo_camera_offset_v = dv;
  g_debug_demo_camera_offset_enabled = true;
}

void wyd_debug_clear_demo_camera_offset() {
  g_debug_demo_camera_offset_x = 0.0f;
  g_debug_demo_camera_offset_y = 0.0f;
  g_debug_demo_camera_offset_z = 0.0f;
  g_debug_demo_camera_offset_h = 0.0f;
  g_debug_demo_camera_offset_v = 0.0f;
  g_debug_demo_camera_offset_enabled = false;
}

uint32_t wyd_debug_demo_camera_offset_enabled() {
  return g_debug_demo_camera_offset_enabled ? 1u : 0u;
}

float wyd_debug_demo_camera_offset_x() {
  return g_debug_demo_camera_offset_x;
}

float wyd_debug_demo_camera_offset_y() {
  return g_debug_demo_camera_offset_y;
}

float wyd_debug_demo_camera_offset_z() {
  return g_debug_demo_camera_offset_z;
}

float wyd_debug_demo_camera_offset_h() {
  return g_debug_demo_camera_offset_h;
}

float wyd_debug_demo_camera_offset_v() {
  return g_debug_demo_camera_offset_v;
}

DWORD GetLastError(void) { return g_last_error.load(std::memory_order_relaxed); }

void GetLocalTime(SYSTEMTIME* lpSystemTime) {
  if (!lpSystemTime) return;
  std::time_t now = std::time(nullptr);
  std::tm local_tm{};
  localtime_r(&now, &local_tm);
  lpSystemTime->wYear = static_cast<WORD>(local_tm.tm_year + 1900);
  lpSystemTime->wMonth = static_cast<WORD>(local_tm.tm_mon + 1);
  lpSystemTime->wDay = static_cast<WORD>(local_tm.tm_mday);
  lpSystemTime->wDayOfWeek = static_cast<WORD>(local_tm.tm_wday);
  lpSystemTime->wHour = static_cast<WORD>(local_tm.tm_hour);
  lpSystemTime->wMinute = static_cast<WORD>(local_tm.tm_min);
  lpSystemTime->wSecond = static_cast<WORD>(local_tm.tm_sec);
  lpSystemTime->wMilliseconds = 0;
}

void Sleep(DWORD dwMilliseconds) {
  usleep(static_cast<useconds_t>(dwMilliseconds) * 1000u);
}

HANDLE CreateFileA(LPCSTR lpFileName,
                   DWORD dwDesiredAccess,
                   DWORD,
                   LPSECURITY_ATTRIBUTES,
                   DWORD dwCreationDisposition,
                   DWORD,
                   HANDLE) {
  if (IsNullOrEmpty(lpFileName)) {
    SetLastErr(ERROR_FILE_NOT_FOUND);
    return INVALID_HANDLE_VALUE;
  }

  int flags = 0;
  bool can_read = (dwDesiredAccess & GENERIC_READ) != 0;
  bool can_write = (dwDesiredAccess & GENERIC_WRITE) != 0;
  if (can_read && can_write) {
    flags |= O_RDWR;
  } else if (can_write) {
    flags |= O_WRONLY;
  } else {
    flags |= O_RDONLY;
  }

  if (dwCreationDisposition == CREATE_ALWAYS) {
    flags |= O_CREAT | O_TRUNC;
  }

  auto resolved = ResolveCaseInsensitivePath(lpFileName);
  int fd = open(resolved.string().c_str(), flags, 0644);
  if (fd < 0) {
    fd = open(NormalizeWinPath(lpFileName).c_str(), flags, 0644);
  }
  TrackAssetFileOpen(lpFileName, fd >= 0);
  if (fd < 0) {
    SetLastErr(ERROR_FILE_NOT_FOUND);
    return INVALID_HANDLE_VALUE;
  }

  auto* fh = new FileHandle();
  fh->fd = fd;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_file_handles.insert(fh);
  }
  return static_cast<HANDLE>(fh);
}

BOOL ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead, LPDWORD lpNumberOfBytesRead, OVERLAPPED*) {
  if (!hFile || !lpBuffer) return FALSE;
  auto* fh = static_cast<FileHandle*>(hFile);
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file_handles.find(fh) == g_file_handles.end()) return FALSE;
  }

  ssize_t n = read(fh->fd, lpBuffer, nNumberOfBytesToRead);
  if (n < 0) {
    if (lpNumberOfBytesRead) *lpNumberOfBytesRead = 0;
    return FALSE;
  }
  if (lpNumberOfBytesRead) *lpNumberOfBytesRead = static_cast<DWORD>(n);
  return TRUE;
}

BOOL WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite, LPDWORD lpNumberOfBytesWritten, OVERLAPPED*) {
  if (!hFile || !lpBuffer) return FALSE;
  auto* fh = static_cast<FileHandle*>(hFile);
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file_handles.find(fh) == g_file_handles.end()) return FALSE;
  }

  ssize_t n = write(fh->fd, lpBuffer, nNumberOfBytesToWrite);
  if (n < 0) {
    if (lpNumberOfBytesWritten) *lpNumberOfBytesWritten = 0;
    return FALSE;
  }
  if (lpNumberOfBytesWritten) *lpNumberOfBytesWritten = static_cast<DWORD>(n);
  return TRUE;
}

BOOL CloseHandle(HANDLE hObject) {
  if (!hObject) return TRUE;
  auto* fh = static_cast<FileHandle*>(hObject);
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_file_handles.find(fh);
    if (it != g_file_handles.end()) {
      close(fh->fd);
      g_file_handles.erase(it);
      delete fh;
      return TRUE;
    }
  }
  return TRUE;
}

BOOL DeleteFileA(LPCSTR lpFileName) {
  if (IsNullOrEmpty(lpFileName)) return FALSE;
  auto resolved = ResolveCaseInsensitivePath(lpFileName);
  if (unlink(resolved.string().c_str()) == 0) return TRUE;
  auto normalized = NormalizeWinPath(lpFileName);
  return (unlink(normalized.c_str()) == 0) ? TRUE : FALSE;
}

BOOL DeleteFileW(LPCWSTR lpFileName) {
  auto path = ToNarrow(lpFileName);
  return DeleteFileA(path.c_str());
}

BOOL SetFileAttributesA(LPCSTR, DWORD) { return TRUE; }

DWORD GetFileAttributesA(LPCSTR lpFileName) {
  if (IsNullOrEmpty(lpFileName)) return INVALID_FILE_ATTRIBUTES;
  struct stat st {};
  auto resolved = ResolveCaseInsensitivePath(lpFileName);
  if (stat(resolved.string().c_str(), &st) != 0) return INVALID_FILE_ATTRIBUTES;
  if (S_ISDIR(st.st_mode)) return kFileAttributeDirectory;
  return kFileAttributeNormal;
}

BOOL CopyFileA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName, BOOL bFailIfExists) {
  if (IsNullOrEmpty(lpExistingFileName) || IsNullOrEmpty(lpNewFileName)) return FALSE;
  if (bFailIfExists && std::filesystem::exists(lpNewFileName)) return FALSE;

  auto resolved_src = ResolveCaseInsensitivePath(lpExistingFileName);
  auto normalized_dst = NormalizeWinPath(lpNewFileName);

  std::ifstream src(resolved_src, std::ios::binary);
  if (!src) return FALSE;
  std::ofstream dst(normalized_dst, std::ios::binary | std::ios::trunc);
  if (!dst) return FALSE;
  dst << src.rdbuf();
  return dst.good() ? TRUE : FALSE;
}

DWORD SetFilePointer(HANDLE hFile, LONG lDistanceToMove, LPLONG lpDistanceToMoveHigh, DWORD dwMoveMethod) {
  if (!hFile) return 0xFFFFFFFFu;
  auto* fh = static_cast<FileHandle*>(hFile);
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file_handles.find(fh) == g_file_handles.end()) return 0xFFFFFFFFu;
  }

  off_t offset = static_cast<off_t>(lDistanceToMove);
  if (lpDistanceToMoveHigh) {
    offset |= (static_cast<off_t>(*lpDistanceToMoveHigh) << 32);
  }

  int whence = SEEK_SET;
  if (dwMoveMethod == FILE_CURRENT) whence = SEEK_CUR;
  else if (dwMoveMethod == FILE_END) whence = SEEK_END;

  off_t pos = lseek(fh->fd, offset, whence);
  if (pos < 0) return 0xFFFFFFFFu;
  return static_cast<DWORD>(pos & 0xFFFFFFFFu);
}

BOOL SetFilePointerEx(HANDLE hFile, LARGE_INTEGER liDistanceToMove, LARGE_INTEGER* lpNewFilePointer, DWORD dwMoveMethod) {
  if (!hFile) return FALSE;
  auto* fh = static_cast<FileHandle*>(hFile);
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file_handles.find(fh) == g_file_handles.end()) return FALSE;
  }

  int whence = SEEK_SET;
  if (dwMoveMethod == FILE_CURRENT) whence = SEEK_CUR;
  else if (dwMoveMethod == FILE_END) whence = SEEK_END;

  off_t pos = lseek(fh->fd, static_cast<off_t>(liDistanceToMove.QuadPart), whence);
  if (pos < 0) return FALSE;
  if (lpNewFilePointer) lpNewFilePointer->QuadPart = pos;
  return TRUE;
}

BOOL GetFileSizeEx(HANDLE hFile, LARGE_INTEGER* lpFileSize) {
  if (!hFile || !lpFileSize) return FALSE;
  auto* fh = static_cast<FileHandle*>(hFile);
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file_handles.find(fh) == g_file_handles.end()) return FALSE;
  }

  struct stat st {};
  if (fstat(fh->fd, &st) != 0) return FALSE;
  lpFileSize->QuadPart = static_cast<long long>(st.st_size);
  return TRUE;
}

DWORD GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh) {
  LARGE_INTEGER li{};
  if (!GetFileSizeEx(hFile, &li)) return 0xFFFFFFFFu;
  if (lpFileSizeHigh) *lpFileSizeHigh = static_cast<DWORD>((li.QuadPart >> 32) & 0xFFFFFFFFu);
  return static_cast<DWORD>(li.QuadPart & 0xFFFFFFFFu);
}

BOOL SetCurrentDirectoryA(LPCSTR lpPathName) {
  if (IsNullOrEmpty(lpPathName)) return FALSE;
  return (chdir(lpPathName) == 0) ? TRUE : FALSE;
}

BOOL SetCurrentDirectoryW(LPCWSTR lpPathName) {
  auto path = ToNarrow(lpPathName);
  return SetCurrentDirectoryA(path.c_str());
}

DWORD GetCurrentDirectoryA(DWORD nBufferLength, LPSTR lpBuffer) {
  if (!lpBuffer || nBufferLength == 0) return 0;
  char* cwd = getcwd(lpBuffer, nBufferLength);
  if (!cwd) return 0;
  return static_cast<DWORD>(std::strlen(lpBuffer));
}

DWORD GetCurrentDirectoryW(DWORD nBufferLength, LPWSTR lpBuffer) {
  if (!lpBuffer || nBufferLength == 0) return 0;
  char tmp[4096]{};
  if (!getcwd(tmp, sizeof(tmp))) return 0;
  size_t len = std::strlen(tmp);
  if (len + 1 > static_cast<size_t>(nBufferLength)) return 0;
  for (size_t i = 0; i < len; ++i) lpBuffer[i] = static_cast<wchar_t>(tmp[i]);
  lpBuffer[len] = 0;
  return static_cast<DWORD>(len);
}

HANDLE FindFirstFileA(LPCSTR lpFileName, WIN32_FIND_DATAA* lpFindFileData) {
  auto entries = EnumeratePattern(lpFileName);
  if (entries.empty()) return INVALID_HANDLE_VALUE;

  auto* fh = new FindHandle();
  fh->entries = std::move(entries);
  fh->index = 0;
  FillFindData(fh->entries[0], lpFindFileData);

  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_find_handles.insert(fh);
  }
  return static_cast<HANDLE>(fh);
}

BOOL FindNextFileA(HANDLE hFindFile, WIN32_FIND_DATAA* lpFindFileData) {
  if (!hFindFile) return FALSE;
  auto* fh = static_cast<FindHandle*>(hFindFile);
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_find_handles.find(fh) == g_find_handles.end()) return FALSE;
  }

  ++fh->index;
  if (fh->index >= fh->entries.size()) return FALSE;
  FillFindData(fh->entries[fh->index], lpFindFileData);
  return TRUE;
}

BOOL FindClose(HANDLE hFindFile) {
  if (!hFindFile) return TRUE;
  auto* fh = static_cast<FindHandle*>(hFindFile);
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_find_handles.find(fh);
    if (it != g_find_handles.end()) {
      g_find_handles.erase(it);
      delete fh;
      return TRUE;
    }
  }
  return TRUE;
}

DWORD GetModuleFileNameA(HMODULE, LPSTR lpFilename, DWORD nSize) {
  const char* fake = "WYD.exe";
  if (!lpFilename || nSize == 0) return 0;
  std::strncpy(lpFilename, fake, nSize - 1);
  lpFilename[nSize - 1] = '\0';
  return static_cast<DWORD>(std::strlen(lpFilename));
}

DWORD GetFullPathNameA(LPCSTR lpFileName, DWORD nBufferLength, LPSTR lpBuffer, LPSTR* lpFilePart) {
  if (IsNullOrEmpty(lpFileName) || !lpBuffer || nBufferLength == 0) return 0;
  std::filesystem::path abs = std::filesystem::absolute(lpFileName);
  auto str = abs.string();
  if (str.size() + 1 > nBufferLength) return static_cast<DWORD>(str.size() + 1);
  std::strncpy(lpBuffer, str.c_str(), nBufferLength - 1);
  lpBuffer[nBufferLength - 1] = '\0';
  if (lpFilePart) {
    auto pos = str.find_last_of('/');
    *lpFilePart = (pos == std::string::npos) ? lpBuffer : (lpBuffer + pos + 1);
  }
  return static_cast<DWORD>(std::strlen(lpBuffer));
}

HMODULE GetModuleHandleA(LPCSTR) { return reinterpret_cast<HMODULE>(1); }
HMODULE LoadLibraryA(LPCSTR) { return reinterpret_cast<HMODULE>(1); }
BOOL FreeLibrary(HMODULE) { return TRUE; }
FARPROC GetProcAddress(HMODULE, LPCSTR) { return nullptr; }

HRSRC FindResourceA(HMODULE, LPCSTR, LPCSTR) { return nullptr; }
HGLOBAL LoadResource(HMODULE, HRSRC) { return nullptr; }
DWORD SizeofResource(HMODULE, HRSRC) { return 0; }
LPVOID LockResource(HGLOBAL) { return nullptr; }

BOOL QueryPerformanceCounter(LARGE_INTEGER* lpPerformanceCount) {
  if (!lpPerformanceCount) return FALSE;
  auto now = std::chrono::steady_clock::now().time_since_epoch();
  auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
  lpPerformanceCount->QuadPart = ns;
  return TRUE;
}

BOOL QueryPerformanceFrequency(LARGE_INTEGER* lpFrequency) {
  if (!lpFrequency) return FALSE;
  lpFrequency->QuadPart = 1000000000LL;
  return TRUE;
}

BOOL GetMessageA(MSG* lpMsg, HWND, UINT, UINT) {
  if (!lpMsg) return FALSE;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_msg_queue.empty()) return FALSE;
  *lpMsg = g_msg_queue.front();
  g_msg_queue.pop_front();
  return (lpMsg->message == WM_QUIT) ? FALSE : TRUE;
}

BOOL PeekMessageA(MSG* lpMsg, HWND, UINT, UINT, UINT wRemoveMsg) {
  if (!lpMsg) return FALSE;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_msg_queue.empty()) return FALSE;
  *lpMsg = g_msg_queue.front();
  if (wRemoveMsg & PM_REMOVE) g_msg_queue.pop_front();
  return TRUE;
}

BOOL TranslateMessage(const MSG*) { return TRUE; }
LRESULT DispatchMessageA(const MSG* lpMsg) {
  if (!lpMsg) return 0;
  return DefWindowProcA(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam);
}

void PostQuitMessage(int nExitCode) {
  MSG msg{};
  msg.message = WM_QUIT;
  msg.wParam = static_cast<WPARAM>(nExitCode);
  std::lock_guard<std::mutex> lock(g_mutex);
  g_msg_queue.push_back(msg);
}

HDC BeginPaint(HWND, PAINTSTRUCT* lpPaint) {
  if (lpPaint) std::memset(lpPaint, 0, sizeof(*lpPaint));
  HDC hdc = reinterpret_cast<HDC>(NewOpaqueHandle());
  g_hdc_states[reinterpret_cast<void*>(hdc)] = HdcState{};
  return hdc;
}

BOOL EndPaint(HWND, const PAINTSTRUCT*) { return TRUE; }
BOOL FillRect(HDC hdc, const RECT* rect, HBRUSH) {
  DibSection* dib = SelectedDibFor(hdc);
  if (!dib || !dib->pixels) return TRUE;
  const int left = rect ? std::max<LONG>(0, rect->left) : 0;
  const int top = rect ? std::max<LONG>(0, rect->top) : 0;
  const int right = rect ? std::min<LONG>(dib->width, rect->right) : dib->width;
  const int bottom = rect ? std::min<LONG>(dib->height, rect->bottom) : dib->height;
  if (right <= left || bottom <= top) return TRUE;
  for (int y = top; y < bottom; ++y) {
    std::fill_n(
        dib->pixels + static_cast<size_t>(y) * static_cast<size_t>(dib->width) + static_cast<size_t>(left),
        right - left,
        0u);
  }
  return TRUE;
}

BOOL SetRect(RECT* lprc, int xLeft, int yTop, int xRight, int yBottom) {
  if (!lprc) return FALSE;
  lprc->left = xLeft;
  lprc->top = yTop;
  lprc->right = xRight;
  lprc->bottom = yBottom;
  return TRUE;
}

BOOL IntersectRect(RECT* lprcDst, const RECT* lprcSrc1, const RECT* lprcSrc2) {
  if (!lprcDst || !lprcSrc1 || !lprcSrc2) return FALSE;
  lprcDst->left = std::max(lprcSrc1->left, lprcSrc2->left);
  lprcDst->top = std::max(lprcSrc1->top, lprcSrc2->top);
  lprcDst->right = std::min(lprcSrc1->right, lprcSrc2->right);
  lprcDst->bottom = std::min(lprcSrc1->bottom, lprcSrc2->bottom);
  return (lprcDst->left < lprcDst->right && lprcDst->top < lprcDst->bottom) ? TRUE : FALSE;
}

BOOL AdjustWindowRect(RECT*, DWORD, BOOL) { return TRUE; }
HCURSOR SetCursor(HCURSOR hCursor) { return hCursor; }
HGDIOBJ GetStockObject(int) { return reinterpret_cast<HGDIOBJ>(NewOpaqueHandle()); }
BOOL DeleteObject(HGDIOBJ object) {
  void* key = reinterpret_cast<void*>(object);
  auto dib = g_dib_sections.find(key);
  if (dib != g_dib_sections.end()) {
    std::free(dib->second.pixels);
    g_dib_sections.erase(dib);
    for (auto& item : g_hdc_states) {
      if (reinterpret_cast<void*>(item.second.bitmap) == key) item.second.bitmap = nullptr;
    }
  }
  return TRUE;
}
HBITMAP LoadBitmapA(HINSTANCE, LPCSTR) { return reinterpret_cast<HBITMAP>(NewOpaqueHandle()); }
HCURSOR LoadCursorA(HINSTANCE, LPCSTR) { return reinterpret_cast<HCURSOR>(NewOpaqueHandle()); }
HICON LoadIconA(HINSTANCE, LPCSTR) { return reinterpret_cast<HICON>(NewOpaqueHandle()); }
HACCEL LoadAcceleratorsA(HINSTANCE, LPCSTR) { return reinterpret_cast<HACCEL>(NewOpaqueHandle()); }
int TranslateAcceleratorA(HWND, HACCEL, MSG*) { return 0; }
BOOL DestroyAcceleratorTable(HACCEL) { return TRUE; }

HDC GetDC(HWND) {
  HDC hdc = reinterpret_cast<HDC>(NewOpaqueHandle());
  g_hdc_states[reinterpret_cast<void*>(hdc)] = HdcState{};
  return hdc;
}
int ReleaseDC(HWND, HDC) { return 1; }
BOOL SetDeviceGammaRamp(HDC, LPVOID) { return TRUE; }
HDC CreateDCA(LPCSTR, LPCSTR, LPCSTR, const DEVMODEA*) {
  HDC hdc = reinterpret_cast<HDC>(NewOpaqueHandle());
  g_hdc_states[reinterpret_cast<void*>(hdc)] = HdcState{};
  return hdc;
}
HDC CreateCompatibleDC(HDC) {
  HDC hdc = reinterpret_cast<HDC>(NewOpaqueHandle());
  g_hdc_states[reinterpret_cast<void*>(hdc)] = HdcState{};
  return hdc;
}
BOOL DeleteDC(HDC hdc) {
  g_hdc_states.erase(reinterpret_cast<void*>(hdc));
  return TRUE;
}

int GetDeviceCaps(HDC, int index) {
  switch (index) {
    case HORZRES: return kDefaultWidth;
    case VERTRES: return kDefaultHeight;
    case BITSPIXEL: return 32;
    case VREFRESH: return 60;
    default: return 0;
  }
}

HGDIOBJ SelectObject(HDC hdc, HGDIOBJ h) {
  HdcState* state = HdcStateFor(hdc);
  if (!state) return h;
  void* key = reinterpret_cast<void*>(h);
  // Bitmap (DIB section)
  if (g_dib_sections.find(key) != g_dib_sections.end()) {
    HGDIOBJ old = reinterpret_cast<HGDIOBJ>(state->bitmap);
    state->bitmap = reinterpret_cast<HBITMAP>(h);
    return old ? old : h;
  }
  // Font
  if (g_font_info.find(key) != g_font_info.end()) {
    HGDIOBJ old = reinterpret_cast<HGDIOBJ>(state->font);
    state->font = reinterpret_cast<HFONT>(h);
    return old ? old : h;
  }
  return h;
}

BOOL StretchBlt(HDC, int, int, int, int, HDC, int, int, int, int, DWORD) { return TRUE; }

HBITMAP CreateDIBSection(HDC, const BITMAPINFO* pbmi, UINT, void** ppvBits, HANDLE, DWORD) {
  int width = kDefaultWidth;
  int height = kDefaultHeight;
  int bpp = 32;
  if (ppvBits) {
    size_t bytes = 0;
    if (pbmi) {
      width = std::max(1L, static_cast<long>(pbmi->bmiHeader.biWidth));
      height = std::max(1L, std::abs(pbmi->bmiHeader.biHeight));
      bpp = std::max(1, static_cast<int>(pbmi->bmiHeader.biBitCount));
      bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(bpp / 8);
    }
    if (bytes == 0) bytes = static_cast<size_t>(kDefaultWidth * kDefaultHeight * 4);
    *ppvBits = std::calloc(1, bytes);
  }
  HBITMAP bitmap = reinterpret_cast<HBITMAP>(NewOpaqueHandle());
  g_dib_sections[reinterpret_cast<void*>(bitmap)] =
      DibSection{width, height, bpp, ppvBits ? static_cast<uint32_t*>(*ppvBits) : nullptr};
  return bitmap;
}

HFONT CreateFontA(int nHeight, int nWidth, int nEscapement, int nOrientation,
                  int fnWeight, DWORD fdwItalic, DWORD, DWORD,
                  DWORD fdwCharSet, DWORD, DWORD,
                  DWORD, DWORD, LPCSTR lpszFace) {
  (void)nWidth; (void)nEscapement; (void)nOrientation;
  auto* h = NewOpaqueHandle();
  FontInfo info;
  info.face_name = (lpszFace && lpszFace[0]) ? lpszFace : "Tahoma";
  info.height = nHeight;
  info.weight = fnWeight;
  info.italic = (fdwItalic != 0);
  g_font_info[reinterpret_cast<void*>(h)] = info;
  return reinterpret_cast<HFONT>(h);
}

COLORREF SetTextColor(HDC hdc, COLORREF color) {
  HdcState* state = HdcStateFor(hdc);
  const COLORREF old = state ? state->text_color : color;
  if (state) state->text_color = color;
  return old;
}
COLORREF SetBkColor(HDC hdc, COLORREF color) {
  HdcState* state = HdcStateFor(hdc);
  const COLORREF old = state ? state->bk_color : color;
  if (state) state->bk_color = color;
  return old;
}
int SetBkMode(HDC hdc, int mode) {
  HdcState* state = HdcStateFor(hdc);
  const int old = state ? state->bk_mode : mode;
  if (state) state->bk_mode = mode;
  return old;
}
BOOL TextOutA(HDC hdc, int x, int y, LPCSTR text, int count) {
  if (!text) return FALSE;
  HdcState* state = HdcStateFor(hdc);
  DibSection* dib = SelectedDibFor(hdc);
  if (!state || !dib || !dib->pixels) return TRUE;
  int chars = count;
  if (chars < 0) chars = static_cast<int>(std::strlen(text));
  chars = std::max(0, chars);
  if (chars == 0) return TRUE;

  FontInfo* finfo = FontInfoFor(state->font);
  uint32_t text_color = 0xFF000000u | static_cast<uint32_t>(state->text_color & 0x00FFFFFFu);
  RenderTextToDIB(dib, x, y, text, chars, finfo, text_color);
  return TRUE;
}
BOOL GetTextExtentPoint32A(HDC hdc, LPCSTR lpString, int c, SIZE* pSize) {
  if (!pSize) return FALSE;
  int chars = c;
  if (chars < 0 && lpString) chars = static_cast<int>(std::strlen(lpString));
  if (chars < 0) chars = 0;
  if (chars == 0) {
    pSize->cx = 0;
    pSize->cy = 0;
    return TRUE;
  }
  HdcState* state = HdcStateFor(hdc);
  FontInfo* finfo = state ? FontInfoFor(state->font) : nullptr;
  pSize->cx = MeasureTextWidth(lpString, chars, finfo);
  int font_h = finfo ? std::abs(finfo->height) : 14;
  pSize->cy = font_h + 4;
  return TRUE;
}
ATOM RegisterClassA(const WNDCLASSA*) { return 1; }
ATOM RegisterClassExA(const WNDCLASSEXA*) { return 1; }
BOOL UnregisterClassA(LPCSTR, HINSTANCE) { return TRUE; }

HWND CreateWindowExA(DWORD,
                     LPCSTR,
                     LPCSTR lpWindowName,
                     DWORD dwStyle,
                     int X,
                     int Y,
                     int nWidth,
                     int nHeight,
                     HWND,
                     HMENU,
                     HINSTANCE,
                     LPVOID) {
  HWND hwnd = reinterpret_cast<HWND>(NewOpaqueHandle());
  WindowState st;
  st.style = static_cast<LONG>(dwStyle);
  st.rect.left = X;
  st.rect.top = Y;
  st.rect.right = X + ((nWidth > 0) ? nWidth : kDefaultWidth);
  st.rect.bottom = Y + ((nHeight > 0) ? nHeight : kDefaultHeight);
  if (lpWindowName) st.text = lpWindowName;

  std::lock_guard<std::mutex> lock(g_mutex);
  g_windows[hwnd] = st;
  if (!g_focus) g_focus = hwnd;
  return hwnd;
}

BOOL DestroyWindow(HWND hWnd) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_windows.erase(hWnd);
  if (g_focus == hWnd) g_focus = nullptr;
  return TRUE;
}

BOOL ShowWindow(HWND, int) { return TRUE; }
BOOL UpdateWindow(HWND) { return TRUE; }

BOOL SetWindowTextA(HWND hWnd, LPCSTR lpString) {
  std::lock_guard<std::mutex> lock(g_mutex);
  auto it = g_windows.find(hWnd);
  if (it == g_windows.end()) return FALSE;
  it->second.text = lpString ? lpString : "";
  return TRUE;
}

HWND SetFocus(HWND hWnd) {
  std::lock_guard<std::mutex> lock(g_mutex);
  HWND prev = g_focus;
  g_focus = hWnd;
  return prev;
}

HWND GetFocus(void) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_focus;
}

LRESULT DefWindowProcA(HWND, UINT, WPARAM, LPARAM) { return 0; }
BOOL CloseWindow(HWND) { return TRUE; }

HWND FindWindowA(LPCSTR, LPCSTR lpWindowName) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (IsNullOrEmpty(lpWindowName)) return nullptr;
  for (const auto& kv : g_windows) {
    if (kv.second.text == lpWindowName) return kv.first;
  }
  return nullptr;
}

LPSTR GetCommandLineA(void) {
  static char s_cmdline[] = "WYD.exe";
  return s_cmdline;
}

BOOL AllocConsole(void) { return TRUE; }

BOOL SetWindowPos(HWND hWnd, HWND, int X, int Y, int cx, int cy, UINT) {
  std::lock_guard<std::mutex> lock(g_mutex);
  auto it = g_windows.find(hWnd);
  if (it == g_windows.end()) return FALSE;
  it->second.rect.left = X;
  it->second.rect.top = Y;
  if (cx > 0) it->second.rect.right = X + cx;
  if (cy > 0) it->second.rect.bottom = Y + cy;
  return TRUE;
}

BOOL SetMenu(HWND hWnd, HMENU hMenu) {
  std::lock_guard<std::mutex> lock(g_mutex);
  auto it = g_windows.find(hWnd);
  if (it == g_windows.end()) return FALSE;
  it->second.menu = hMenu;
  return TRUE;
}

HMENU GetMenu(HWND hWnd) {
  std::lock_guard<std::mutex> lock(g_mutex);
  auto it = g_windows.find(hWnd);
  if (it == g_windows.end()) return nullptr;
  return it->second.menu;
}

BOOL GetClientRect(HWND hWnd, RECT* lpRect) {
  if (!lpRect) return FALSE;
  std::lock_guard<std::mutex> lock(g_mutex);
  auto it = g_windows.find(hWnd);
  if (it == g_windows.end()) return FALSE;
  RECT r = it->second.rect;
  lpRect->left = 0;
  lpRect->top = 0;
  lpRect->right = std::max(0L, r.right - r.left);
  lpRect->bottom = std::max(0L, r.bottom - r.top);
  return TRUE;
}

BOOL GetWindowRect(HWND hWnd, RECT* lpRect) {
  if (!lpRect) return FALSE;
  std::lock_guard<std::mutex> lock(g_mutex);
  auto it = g_windows.find(hWnd);
  if (it == g_windows.end()) return FALSE;
  *lpRect = it->second.rect;
  return TRUE;
}

BOOL ClientToScreen(HWND hWnd, POINT* lpPoint) {
  if (!lpPoint) return FALSE;
  std::lock_guard<std::mutex> lock(g_mutex);
  auto it = g_windows.find(hWnd);
  if (it == g_windows.end()) return FALSE;
  lpPoint->x += it->second.rect.left;
  lpPoint->y += it->second.rect.top;
  return TRUE;
}

BOOL ScreenToClient(HWND hWnd, POINT* lpPoint) {
  if (!lpPoint) return FALSE;
  std::lock_guard<std::mutex> lock(g_mutex);
  auto it = g_windows.find(hWnd);
  if (it == g_windows.end()) return FALSE;
  lpPoint->x -= it->second.rect.left;
  lpPoint->y -= it->second.rect.top;
  return TRUE;
}

BOOL PtInRect(const RECT* lprc, POINT pt) {
  if (!lprc) return FALSE;
  return (pt.x >= lprc->left && pt.x < lprc->right && pt.y >= lprc->top && pt.y < lprc->bottom) ? TRUE : FALSE;
}

short GetAsyncKeyState(int) { return 0; }
short GetKeyState(int) { return 0; }

int MultiByteToWideChar(UINT,
                        DWORD,
                        LPCSTR lpMultiByteStr,
                        int cbMultiByte,
                        LPWSTR lpWideCharStr,
                        int cchWideChar) {
  if (!lpMultiByteStr) return 0;
  int src_len = cbMultiByte;
  if (src_len < 0) src_len = static_cast<int>(std::strlen(lpMultiByteStr) + 1);
  if (!lpWideCharStr || cchWideChar == 0) return src_len;

  int n = std::min(src_len, cchWideChar);
  for (int i = 0; i < n; ++i) lpWideCharStr[i] = static_cast<unsigned char>(lpMultiByteStr[i]);
  if (n == cchWideChar && cchWideChar > 0) lpWideCharStr[cchWideChar - 1] = 0;
  return n;
}

int WideCharToMultiByte(UINT,
                        DWORD,
                        LPCWSTR lpWideCharStr,
                        int cchWideChar,
                        LPSTR lpMultiByteStr,
                        int cbMultiByte,
                        LPCSTR,
                        LPBOOL) {
  if (!lpWideCharStr) return 0;
  int src_len = cchWideChar;
  if (src_len < 0) {
    src_len = 0;
    while (lpWideCharStr[src_len]) ++src_len;
    ++src_len;
  }
  if (!lpMultiByteStr || cbMultiByte == 0) return src_len;

  int n = std::min(src_len, cbMultiByte);
  for (int i = 0; i < n; ++i) {
    wchar_t wc = lpWideCharStr[i];
    lpMultiByteStr[i] = (wc >= 0 && wc <= 0x7F) ? static_cast<char>(wc) : '?';
  }
  if (n == cbMultiByte && cbMultiByte > 0) lpMultiByteStr[cbMultiByte - 1] = '\0';
  return n;
}

LPSTR lstrcpyA(LPSTR dst, LPCSTR src) {
  if (!dst) return nullptr;
  if (!src) {
    dst[0] = '\0';
    return dst;
  }
  std::strcpy(dst, src);
  return dst;
}

LPSTR lstrcpynA(LPSTR dst, LPCSTR src, int max_len) {
  if (!dst || max_len <= 0) return dst;
  if (!src) {
    dst[0] = '\0';
    return dst;
  }
  std::strncpy(dst, src, static_cast<size_t>(max_len) - 1);
  dst[max_len - 1] = '\0';
  return dst;
}

LPSTR lstrcatA(LPSTR dst, LPCSTR src) {
  if (!dst) return nullptr;
  if (src) std::strcat(dst, src);
  return dst;
}

int lstrlenA(LPCSTR s) { return s ? static_cast<int>(std::strlen(s)) : 0; }
int lstrcmpA(LPCSTR a, LPCSTR b) { return std::strcmp(a ? a : "", b ? b : ""); }

int lstrcmpiA(LPCSTR a, LPCSTR b) {
  std::string sa = a ? a : "";
  std::string sb = b ? b : "";
  std::transform(sa.begin(), sa.end(), sa.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  std::transform(sb.begin(), sb.end(), sb.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return std::strcmp(sa.c_str(), sb.c_str());
}

LPSTR CharNextA(LPCSTR lpsz) {
  if (!lpsz || *lpsz == '\0') return const_cast<LPSTR>(lpsz);
  return const_cast<LPSTR>(lpsz + 1);
}

LPSTR CharPrevA(LPCSTR lpszStart, LPCSTR lpszCurrent) {
  if (!lpszStart || !lpszCurrent || lpszCurrent <= lpszStart) return const_cast<LPSTR>(lpszStart);
  return const_cast<LPSTR>(lpszCurrent - 1);
}

DWORD GetPrivateProfileStringA(LPCSTR,
                               LPCSTR,
                               LPCSTR lpDefault,
                               LPSTR lpReturnedString,
                               DWORD nSize,
                               LPCSTR) {
  if (!lpReturnedString || nSize == 0) return 0;
  const char* src = lpDefault ? lpDefault : "";
  std::strncpy(lpReturnedString, src, nSize - 1);
  lpReturnedString[nSize - 1] = '\0';
  return static_cast<DWORD>(std::strlen(lpReturnedString));
}

BOOL WritePrivateProfileStringA(LPCSTR, LPCSTR, LPCSTR, LPCSTR) { return TRUE; }

BOOL EnumDisplaySettingsA(LPCSTR, DWORD iModeNum, DEVMODEA* lpDevMode) {
  if (!lpDevMode) return FALSE;
  // Minimal but finite display-mode enumeration:
  // - mode 0: a single current/default mode
  // - mode > 0: no more modes
  if (iModeNum > 0) return FALSE;
  if (lpDevMode->dmSize < sizeof(DEVMODEA)) lpDevMode->dmSize = sizeof(DEVMODEA);
  lpDevMode->dmPelsWidth = kDefaultWidth;
  lpDevMode->dmPelsHeight = kDefaultHeight;
  lpDevMode->dmBitsPerPel = 32;
  lpDevMode->dmDisplayFrequency = 60;
  return TRUE;
}

LONG ChangeDisplaySettingsA(DEVMODEA*, DWORD) { return 0; }

LONG RegOpenKeyExA(HKEY, LPCSTR, DWORD, REGSAM, PHKEY phkResult) {
  if (phkResult) *phkResult = nullptr;
  return ERROR_FILE_NOT_FOUND;
}

LONG RegQueryValueExA(HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD) { return ERROR_FILE_NOT_FOUND; }
LONG RegSetValueExA(HKEY, LPCSTR, DWORD, DWORD, const BYTE*, DWORD) { return ERROR_SUCCESS; }
LONG RegCloseKey(HKEY) { return ERROR_SUCCESS; }

LRESULT SendMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
  return DefWindowProcA(hWnd, Msg, wParam, lParam);
}

LRESULT SendMessageW(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
  return DefWindowProcA(hWnd, Msg, wParam, lParam);
}

BOOL PostMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
  MSG msg{};
  msg.hwnd = hWnd;
  msg.message = Msg;
  msg.wParam = wParam;
  msg.lParam = lParam;
  msg.time = GetTickCount();
  std::lock_guard<std::mutex> lock(g_mutex);
  g_msg_queue.push_back(msg);
  return TRUE;
}

LONG GetWindowLongA(HWND hWnd, int nIndex) {
  std::lock_guard<std::mutex> lock(g_mutex);
  auto it = g_windows.find(hWnd);
  if (it == g_windows.end()) return 0;
  if (nIndex == GWL_STYLE) return it->second.style;
  if (nIndex == GWL_USERDATA) return it->second.user_data;
  return 0;
}

LONG SetWindowLongA(HWND hWnd, int nIndex, LONG dwNewLong) {
  std::lock_guard<std::mutex> lock(g_mutex);
  auto it = g_windows.find(hWnd);
  if (it == g_windows.end()) return 0;
  LONG old = 0;
  if (nIndex == GWL_STYLE) {
    old = it->second.style;
    it->second.style = dwNewLong;
  } else if (nIndex == GWL_USERDATA) {
    old = it->second.user_data;
    it->second.user_data = dwNewLong;
  }
  return old;
}

LONG_PTR GetWindowLongPtrA(HWND hWnd, int nIndex) {
  return static_cast<LONG_PTR>(GetWindowLongA(hWnd, nIndex));
}

LONG_PTR SetWindowLongPtrA(HWND hWnd, int nIndex, LONG_PTR dwNewLong) {
  return static_cast<LONG_PTR>(SetWindowLongA(hWnd, nIndex, static_cast<LONG>(dwNewLong)));
}

HGLOBAL GlobalAlloc(UINT, SIZE_T dwBytes) { return std::malloc(dwBytes); }
HGLOBAL GlobalFree(HGLOBAL hMem) {
  std::free(hMem);
  return nullptr;
}

HWND GetDlgItem(HWND, int) { return nullptr; }

int GetWindowTextA(HWND hWnd, LPSTR lpString, int nMaxCount) {
  if (!lpString || nMaxCount <= 0) return 0;
  std::lock_guard<std::mutex> lock(g_mutex);
  auto it = g_windows.find(hWnd);
  if (it == g_windows.end()) {
    lpString[0] = '\0';
    return 0;
  }
  std::strncpy(lpString, it->second.text.c_str(), static_cast<size_t>(nMaxCount) - 1);
  lpString[nMaxCount - 1] = '\0';
  return static_cast<int>(std::strlen(lpString));
}

int GetWindowTextW(HWND hWnd, LPWSTR lpString, int nMaxCount) {
  if (!lpString || nMaxCount <= 0) return 0;
  char tmp[1024]{};
  int n = GetWindowTextA(hWnd, tmp, sizeof(tmp));
  if (n <= 0) {
    lpString[0] = 0;
    return 0;
  }
  int out = std::min(n, nMaxCount - 1);
  for (int i = 0; i < out; ++i) lpString[i] = static_cast<wchar_t>(tmp[i]);
  lpString[out] = 0;
  return out;
}

HKL GetKeyboardLayout(DWORD) { return reinterpret_cast<HKL>(1); }

BOOL GetKeyboardLayoutNameA(LPSTR pwszKLID) {
  if (!pwszKLID) return FALSE;
  std::strcpy(pwszKLID, "00000409");
  return TRUE;
}

UINT ImmGetDescriptionA(HKL, LPSTR lpszDescription, UINT uBufLen) {
  const char* desc = "IME";
  if (!lpszDescription || uBufLen == 0) return static_cast<UINT>(std::strlen(desc));
  std::strncpy(lpszDescription, desc, uBufLen - 1);
  lpszDescription[uBufLen - 1] = '\0';
  return static_cast<UINT>(std::strlen(lpszDescription));
}

HIMC ImmCreateContext(void) { return reinterpret_cast<HIMC>(NewOpaqueHandle()); }
BOOL ImmDestroyContext(HIMC) { return TRUE; }
HIMC ImmGetContext(HWND) { return reinterpret_cast<HIMC>(1); }
BOOL ImmReleaseContext(HWND, HIMC) { return TRUE; }
HIMC ImmAssociateContext(HWND, HIMC hIMC) { return hIMC; }
HWND ImmGetDefaultIMEWnd(HWND hWnd) { return hWnd; }

BOOL ImmGetConversionStatus(HIMC, LPDWORD lpfdwConversion, LPDWORD lpfdwSentence) {
  if (lpfdwConversion) *lpfdwConversion = 0;
  if (lpfdwSentence) *lpfdwSentence = 0;
  return TRUE;
}

BOOL ImmSetConversionStatus(HIMC, DWORD, DWORD) { return TRUE; }
BOOL ImmSetOpenStatus(HIMC, BOOL) { return TRUE; }
BOOL ImmGetOpenStatus(HIMC) { return FALSE; }

void OutputDebugStringA(LPCSTR lpOutputString) {
  if (lpOutputString) std::fprintf(stderr, "[Debug] %s\n", lpOutputString);
}

HANDLE CreateThread(LPSECURITY_ATTRIBUTES,
                    SIZE_T,
                    LPTHREAD_START_ROUTINE lpStartAddress,
                    LPVOID lpParameter,
                    DWORD,
                    LPDWORD lpThreadId) {
  if (lpThreadId) *lpThreadId = 1;
  if (lpStartAddress) lpStartAddress(lpParameter);
  return reinterpret_cast<HANDLE>(NewOpaqueHandle());
}

BOOL VerifyVersionInfoA(LPOSVERSIONINFOEXA, DWORD, DWORDLONG) { return TRUE; }

void* ShellExecuteA(HWND, LPCSTR, LPCSTR, LPCSTR, LPCSTR, int) {
  return reinterpret_cast<void*>(33);
}

void* ShellExecuteW(HWND, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, int) {
  return reinterpret_cast<void*>(33);
}

HINTERNET InternetOpenA(LPCSTR, DWORD, LPCSTR, LPCSTR, DWORD) {
  auto* h = new InternetHandle();
  std::lock_guard<std::mutex> lock(g_mutex);
  g_internet_handles.insert(h);
  return static_cast<HINTERNET>(h);
}

HINTERNET InternetOpenUrlA(HINTERNET,
                           LPCSTR lpszUrl,
                           LPCSTR,
                           DWORD,
                           DWORD,
                           DWORD_PTR) {
  if (IsNullOrEmpty(lpszUrl)) return nullptr;

  std::string url(lpszUrl);
  std::string path;
  if (url.rfind("file://", 0) == 0) {
    path = url.substr(7);
  } else if (url.find("://") == std::string::npos) {
    path = url;
  }

  auto* h = new InternetHandle();
  if (!path.empty()) {
    auto resolved = ResolveCaseInsensitivePath(path.c_str());
    std::ifstream in(resolved, std::ios::binary);
    if (!in) {
      auto normalized = NormalizeWinPath(path.c_str());
      in.open(normalized, std::ios::binary);
    }
    if (in) {
      h->data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
  }

  std::lock_guard<std::mutex> lock(g_mutex);
  g_internet_handles.insert(h);
  return static_cast<HINTERNET>(h);
}

BOOL InternetReadFile(HINTERNET hFile, LPVOID lpBuffer, DWORD dwNumberOfBytesToRead, LPDWORD lpdwNumberOfBytesRead) {
  if (lpdwNumberOfBytesRead) *lpdwNumberOfBytesRead = 0;
  if (!hFile || !lpBuffer) return FALSE;
  auto* h = static_cast<InternetHandle*>(hFile);
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_internet_handles.find(h) == g_internet_handles.end()) return FALSE;
  }

  size_t remain = (h->cursor < h->data.size()) ? (h->data.size() - h->cursor) : 0;
  size_t take = std::min(static_cast<size_t>(dwNumberOfBytesToRead), remain);
  if (take > 0) {
    std::memcpy(lpBuffer, h->data.data() + h->cursor, take);
    h->cursor += take;
  }
  if (lpdwNumberOfBytesRead) *lpdwNumberOfBytesRead = static_cast<DWORD>(take);
  return TRUE;
}

BOOL InternetCloseHandle(HINTERNET hInternet) {
  if (!hInternet) return TRUE;
  auto* h = static_cast<InternetHandle*>(hInternet);
  std::lock_guard<std::mutex> lock(g_mutex);
  auto it = g_internet_handles.find(h);
  if (it != g_internet_handles.end()) {
    g_internet_handles.erase(it);
    delete h;
  }
  return TRUE;
}

DWORD GetAdaptersInfo(PIP_ADAPTER_INFO pAdapterInfo, void* pOutBufLen) {
  if (!pOutBufLen) return 1;
  auto* len = static_cast<ULONG*>(pOutBufLen);
  ULONG need = sizeof(IP_ADAPTER_INFO);

  if (!pAdapterInfo || *len < need) {
    *len = need;
    return 111;  // ERROR_BUFFER_OVERFLOW
  }

  std::memset(pAdapterInfo, 0, sizeof(IP_ADAPTER_INFO));
  std::strncpy(pAdapterInfo->AdapterName, "emscripten0", sizeof(pAdapterInfo->AdapterName) - 1);
  std::strncpy(pAdapterInfo->Description, "WASM Adapter", sizeof(pAdapterInfo->Description) - 1);
  pAdapterInfo->AddressLength = 6;
  pAdapterInfo->Address[0] = 0x02;
  pAdapterInfo->Address[1] = 0x00;
  pAdapterInfo->Address[2] = 0x00;
  pAdapterInfo->Address[3] = 0x00;
  pAdapterInfo->Address[4] = 0x00;
  pAdapterInfo->Address[5] = 0x01;
  pAdapterInfo->Next = nullptr;
  *len = need;
  return ERROR_SUCCESS;
}

HRESULT CoInitialize(LPVOID) { return S_OK; }
void CoUninitialize(void) {}

HRESULT CoCreateInstance(REFCLSID, IUnknown*, DWORD, REFIID, LPVOID* ppv) {
  if (ppv) *ppv = nullptr;
  return E_NOTIMPL;
}

HRESULT DirectSoundCreate8(const GUID*, LPDIRECTSOUND8* ppDS8, IUnknown*) {
  if (!ppDS8) return E_POINTER;
  *ppDS8 = new DummyDirectSound8();
  return S_OK;
}

HRESULT DirectInput8Create(HINSTANCE, DWORD, REFIID, void** ppvOut, IUnknown*) {
  if (!ppvOut) return E_POINTER;
  *ppvOut = new DummyDirectInput8();
  return S_OK;
}

IDirect3D9* Direct3DCreate9(UINT) {
  return new DummyDirect3D9();
}

IDirect3DDevice9* WydCreateDummyD3DDevice() {
  return new DummyDirect3DDevice9();
}

int WSAStartup(WORD wVersionRequested, WSAData* lpWSAData) {
  if (lpWSAData) {
    std::memset(lpWSAData, 0, sizeof(*lpWSAData));
    lpWSAData->wVersion = wVersionRequested;
    lpWSAData->wHighVersion = wVersionRequested;
  }
  return 0;
}

int WSACleanup(void) { return 0; }
int WSAGetLastError(void) { return errno; }
int WSAAsyncSelect(SOCKET, HWND, unsigned int, long) { return 0; }

int closesocket(SOCKET s) {
  return close(static_cast<int>(s));
}

extern "C" FILE* wyd_fopen_case_insensitive(const char* filename, const char* mode) {
  if (!filename || !mode) return nullptr;

  auto resolved = ResolveCaseInsensitivePath(filename);
  FILE* fp = std::fopen(resolved.string().c_str(), mode);
  if (fp) {
    TrackAssetFileOpen(filename, true);
    return fp;
  }

  auto normalized = NormalizeWinPath(filename);
  fp = std::fopen(normalized.c_str(), mode);
  if (fp) {
    TrackAssetFileOpen(filename, true);
    return fp;
  }

  auto normalized_resolved = ResolveCaseInsensitivePath(normalized.c_str());
  fp = std::fopen(normalized_resolved.string().c_str(), mode);
  TrackAssetFileOpen(filename, fp != nullptr);
  return fp;
}

int _open(const char* path, int oflag, ...) {
  mode_t mode = 0644;
  if (oflag & O_CREAT) {
    va_list ap;
    va_start(ap, oflag);
    mode = static_cast<mode_t>(va_arg(ap, int));
    va_end(ap);
  }

  auto remember_debug_source = [&](int fd) {
    if (fd < 0 || !path) return;
    g_fd_debug_source_paths[fd] = ToLowerCopy(NormalizeWinPath(path));
  };

  auto resolved = ResolveCaseInsensitivePath(path);
  int fd = open(resolved.string().c_str(), oflag, mode);
  if (fd >= 0) {
    TrackAssetFileOpen(path, true);
    TrackShaderFileOpen(path, true);
    remember_debug_source(fd);
    return fd;
  }

  auto normalized = NormalizeWinPath(path);
  fd = open(normalized.c_str(), oflag, mode);
  TrackAssetFileOpen(path, fd >= 0);
  TrackShaderFileOpen(path, fd >= 0);
  remember_debug_source(fd);
  return fd;
}

int _close(int fd) {
  auto it = g_fd_debug_source_paths.find(fd);
  if (it != g_fd_debug_source_paths.end()) {
    g_last_closed_debug_source_path = it->second;
    g_fd_debug_source_paths.erase(it);
  }
  return close(fd);
}
int _read(int fd, void* buffer, unsigned int count) { return static_cast<int>(read(fd, buffer, count)); }
int _write(int fd, const void* buffer, unsigned int count) { return static_cast<int>(write(fd, buffer, count)); }
long _lseek(int fd, long offset, int origin) { return static_cast<long>(lseek(fd, offset, origin)); }

int _filelength(int fd) {
  struct stat st {};
  if (fstat(fd, &st) != 0) return -1;
  return static_cast<int>(st.st_size);
}

int _stat64i32(const char* path, struct _stat64i32* out) {
  if (!path || !out) return -1;
  struct stat st {};
  auto resolved = ResolveCaseInsensitivePath(path);
  if (stat(resolved.string().c_str(), &st) != 0) {
    auto normalized = NormalizeWinPath(path);
    if (stat(normalized.c_str(), &st) != 0) return -1;
  }
  out->size = static_cast<long long>(st.st_size);
  out->mode = static_cast<long>(st.st_mode);
  out->mtime_value = static_cast<long>(st.st_mtime);
  return 0;
}

int _commit(int fd) { return fsync(fd); }

HMMIO mmioOpenA(LPSTR pszFileName, MMIOINFO*, DWORD fdwOpen) {
  const char* mode = (fdwOpen & MMIO_WRITE) ? "wb+" : "rb";
  auto resolved = ResolveCaseInsensitivePath(pszFileName);
  FILE* fp = std::fopen(resolved.string().c_str(), mode);
  if (!fp) {
    auto normalized = NormalizeWinPath(pszFileName);
    fp = std::fopen(normalized.c_str(), mode);
  }
  TrackAssetFileOpen(pszFileName, fp != nullptr);
  return reinterpret_cast<HMMIO>(fp);
}

MMRESULT mmioClose(HMMIO hmmio, UINT) {
  if (!hmmio) return 0;
  std::fclose(reinterpret_cast<FILE*>(hmmio));
  return 0;
}

MMRESULT mmioDescend(HMMIO, MMCKINFO*, const MMCKINFO*, UINT) { return 0; }
MMRESULT mmioAscend(HMMIO, MMCKINFO*, UINT) { return 0; }

LONG mmioRead(HMMIO hmmio, HPSTR pch, LONG cch) {
  if (!hmmio || !pch || cch <= 0) return -1;
  size_t n = std::fread(pch, 1, static_cast<size_t>(cch), reinterpret_cast<FILE*>(hmmio));
  return static_cast<LONG>(n);
}

LONG mmioWrite(HMMIO hmmio, const char* pch, LONG cch) {
  if (!hmmio || !pch || cch <= 0) return -1;
  size_t n = std::fwrite(pch, 1, static_cast<size_t>(cch), reinterpret_cast<FILE*>(hmmio));
  return static_cast<LONG>(n);
}

MMRESULT mmioCreateChunk(HMMIO, MMCKINFO*, UINT) { return 0; }
MMRESULT mmioGetInfo(HMMIO, MMIOINFO*, UINT) { return 0; }
MMRESULT mmioSetInfo(HMMIO, MMIOINFO*, UINT) { return 0; }
MMRESULT mmioAdvance(HMMIO, MMIOINFO*, UINT) { return 0; }

LONG mmioSeek(HMMIO hmmio, LONG lOffset, int iOrigin) {
  if (!hmmio) return -1;
  int whence = SEEK_SET;
  if (iOrigin == FILE_CURRENT) whence = SEEK_CUR;
  else if (iOrigin == FILE_END) whence = SEEK_END;
  if (std::fseek(reinterpret_cast<FILE*>(hmmio), lOffset, whence) != 0) return -1;
  long pos = std::ftell(reinterpret_cast<FILE*>(hmmio));
  return static_cast<LONG>(pos);
}

D3DXMATRIX* D3DXMatrixIdentity(D3DXMATRIX* out) {
  if (!out) return nullptr;
  std::memset(out, 0, sizeof(*out));
  out->_11 = out->_22 = out->_33 = out->_44 = 1.0f;
  return out;
}

D3DXMATRIX* D3DXMatrixMultiply(D3DXMATRIX* out, const D3DXMATRIX* a, const D3DXMATRIX* b) {
  if (!out || !a || !b) return nullptr;
  D3DXMATRIX r;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      r.m[i][j] = 0.0f;
      for (int k = 0; k < 4; ++k) {
        r.m[i][j] += a->m[i][k] * b->m[k][j];
      }
    }
  }
  *out = r;
  return out;
}

D3DXMATRIX* D3DXMatrixMultiplyTranspose(D3DXMATRIX* out, const D3DXMATRIX* a, const D3DXMATRIX* b) {
  if (!out || !a || !b) return nullptr;
  D3DXMATRIX tmp;
  D3DXMatrixMultiply(&tmp, a, b);
  return D3DXMatrixTranspose(out, &tmp);
}

D3DXMATRIX* D3DXMatrixTranslation(D3DXMATRIX* out, float x, float y, float z) {
  if (!out) return nullptr;
  D3DXMatrixIdentity(out);
  out->_41 = x;
  out->_42 = y;
  out->_43 = z;
  return out;
}

D3DXMATRIX* D3DXMatrixScaling(D3DXMATRIX* out, float sx, float sy, float sz) {
  if (!out) return nullptr;
  D3DXMatrixIdentity(out);
  out->_11 = sx;
  out->_22 = sy;
  out->_33 = sz;
  return out;
}

D3DXMATRIX* D3DXMatrixTranspose(D3DXMATRIX* out, const D3DXMATRIX* in) {
  if (!out || !in) return nullptr;
  D3DXMATRIX r;
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) {
      r.m[i][j] = in->m[j][i];
    }
  }
  *out = r;
  return out;
}

D3DXMATRIX* D3DXMatrixInverse(D3DXMATRIX* out, float* det, const D3DXMATRIX* in) {
  if (!out || !in) return nullptr;

  float a[4][8]{};
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) a[r][c] = in->m[r][c];
    a[r][4 + r] = 1.0f;
  }

  float determinant = 1.0f;
  int sign = 1;

  for (int col = 0; col < 4; ++col) {
    int pivot = col;
    for (int row = col + 1; row < 4; ++row) {
      if (std::fabs(a[row][col]) > std::fabs(a[pivot][col])) pivot = row;
    }

    if (std::fabs(a[pivot][col]) < 1.0e-8f) {
      if (det) *det = 0.0f;
      return nullptr;
    }

    if (pivot != col) {
      for (int c = 0; c < 8; ++c) std::swap(a[pivot][c], a[col][c]);
      sign = -sign;
    }

    float pivot_value = a[col][col];
    determinant *= pivot_value;

    for (int c = 0; c < 8; ++c) a[col][c] /= pivot_value;

    for (int row = 0; row < 4; ++row) {
      if (row == col) continue;
      float factor = a[row][col];
      for (int c = 0; c < 8; ++c) {
        a[row][c] -= factor * a[col][c];
      }
    }
  }

  determinant *= static_cast<float>(sign);
  if (det) *det = determinant;

  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) out->m[r][c] = a[r][4 + c];
  }
  return out;
}

D3DXMATRIX* D3DXMatrixLookAtLH(D3DXMATRIX* out,
                               const D3DXVECTOR3* eye,
                               const D3DXVECTOR3* at,
                               const D3DXVECTOR3* up) {
  if (!out || !eye || !at || !up) return nullptr;

  D3DXVECTOR3 z = Normalize3(*at - *eye);
  D3DXVECTOR3 x = Normalize3(Cross3(*up, z));
  D3DXVECTOR3 y = Cross3(z, x);

  out->_11 = x.x;
  out->_12 = y.x;
  out->_13 = z.x;
  out->_14 = 0.0f;

  out->_21 = x.y;
  out->_22 = y.y;
  out->_23 = z.y;
  out->_24 = 0.0f;

  out->_31 = x.z;
  out->_32 = y.z;
  out->_33 = z.z;
  out->_34 = 0.0f;

  out->_41 = -Dot3(x, *eye);
  out->_42 = -Dot3(y, *eye);
  out->_43 = -Dot3(z, *eye);
  out->_44 = 1.0f;
  return out;
}

D3DXMATRIX* D3DXMatrixPerspectiveFovLH(D3DXMATRIX* out, float fovy, float aspect, float zn, float zf) {
  if (!out || aspect == 0.0f || (zf - zn) == 0.0f) return nullptr;

  float y_scale = 1.0f / std::tan(fovy * 0.5f);
  float x_scale = y_scale / aspect;

  std::memset(out, 0, sizeof(*out));
  out->_11 = x_scale;
  out->_22 = y_scale;
  out->_33 = zf / (zf - zn);
  out->_34 = 1.0f;
  out->_43 = (-zn * zf) / (zf - zn);
  return out;
}

D3DXMATRIX* D3DXMatrixRotationX(D3DXMATRIX* out, float angle) {
  if (!out) return nullptr;
  float c = std::cos(angle);
  float s = std::sin(angle);
  D3DXMatrixIdentity(out);
  out->_22 = c;
  out->_23 = s;
  out->_32 = -s;
  out->_33 = c;
  return out;
}

D3DXMATRIX* D3DXMatrixRotationY(D3DXMATRIX* out, float angle) {
  if (!out) return nullptr;
  float c = std::cos(angle);
  float s = std::sin(angle);
  D3DXMatrixIdentity(out);
  out->_11 = c;
  out->_13 = -s;
  out->_31 = s;
  out->_33 = c;
  return out;
}

D3DXMATRIX* D3DXMatrixRotationZ(D3DXMATRIX* out, float angle) {
  if (!out) return nullptr;
  float c = std::cos(angle);
  float s = std::sin(angle);
  D3DXMatrixIdentity(out);
  out->_11 = c;
  out->_12 = s;
  out->_21 = -s;
  out->_22 = c;
  return out;
}

D3DXMATRIX* D3DXMatrixRotationAxis(D3DXMATRIX* out, const D3DXVECTOR3* axis, float angle) {
  if (!out || !axis) return nullptr;
  D3DXVECTOR3 a = Normalize3(*axis);
  float c = std::cos(angle);
  float s = std::sin(angle);
  float t = 1.0f - c;

  out->_11 = t * a.x * a.x + c;
  out->_12 = t * a.x * a.y + s * a.z;
  out->_13 = t * a.x * a.z - s * a.y;
  out->_14 = 0.0f;

  out->_21 = t * a.x * a.y - s * a.z;
  out->_22 = t * a.y * a.y + c;
  out->_23 = t * a.y * a.z + s * a.x;
  out->_24 = 0.0f;

  out->_31 = t * a.x * a.z + s * a.y;
  out->_32 = t * a.y * a.z - s * a.x;
  out->_33 = t * a.z * a.z + c;
  out->_34 = 0.0f;

  out->_41 = 0.0f;
  out->_42 = 0.0f;
  out->_43 = 0.0f;
  out->_44 = 1.0f;
  return out;
}

D3DXMATRIX* D3DXMatrixRotationYawPitchRoll(D3DXMATRIX* out, float yaw, float pitch, float roll) {
  if (!out) return nullptr;
  D3DXMATRIX my, mx, mz, tmp;
  D3DXMatrixRotationY(&my, yaw);
  D3DXMatrixRotationX(&mx, pitch);
  D3DXMatrixRotationZ(&mz, roll);
  D3DXMatrixMultiply(&tmp, &mx, &my);
  D3DXMatrixMultiply(out, &mz, &tmp);
  return out;
}

D3DXMATRIX* D3DXMatrixRotationQuaternion(D3DXMATRIX* out, const D3DXQUATERNION* q) {
  if (!out || !q) return nullptr;
  float xx = q->x * q->x;
  float yy = q->y * q->y;
  float zz = q->z * q->z;
  float xy = q->x * q->y;
  float xz = q->x * q->z;
  float yz = q->y * q->z;
  float wx = q->w * q->x;
  float wy = q->w * q->y;
  float wz = q->w * q->z;

  out->_11 = 1.0f - 2.0f * (yy + zz);
  out->_12 = 2.0f * (xy + wz);
  out->_13 = 2.0f * (xz - wy);
  out->_14 = 0.0f;

  out->_21 = 2.0f * (xy - wz);
  out->_22 = 1.0f - 2.0f * (xx + zz);
  out->_23 = 2.0f * (yz + wx);
  out->_24 = 0.0f;

  out->_31 = 2.0f * (xz + wy);
  out->_32 = 2.0f * (yz - wx);
  out->_33 = 1.0f - 2.0f * (xx + yy);
  out->_34 = 0.0f;

  out->_41 = 0.0f;
  out->_42 = 0.0f;
  out->_43 = 0.0f;
  out->_44 = 1.0f;
  return out;
}

D3DXVECTOR3* D3DXVec3TransformCoord(D3DXVECTOR3* out, const D3DXVECTOR3* v, const D3DXMATRIX* m) {
  if (!out || !v || !m) return nullptr;

  float x = v->x * m->_11 + v->y * m->_21 + v->z * m->_31 + m->_41;
  float y = v->x * m->_12 + v->y * m->_22 + v->z * m->_32 + m->_42;
  float z = v->x * m->_13 + v->y * m->_23 + v->z * m->_33 + m->_43;
  float w = v->x * m->_14 + v->y * m->_24 + v->z * m->_34 + m->_44;

  if (std::fabs(w) > 1.0e-7f) {
    float inv = 1.0f / w;
    out->x = x * inv;
    out->y = y * inv;
    out->z = z * inv;
  } else {
    out->x = x;
    out->y = y;
    out->z = z;
  }
  return out;
}

D3DXVECTOR4* D3DXVec3Transform(D3DXVECTOR4* out, const D3DXVECTOR3* v, const D3DXMATRIX* m) {
  if (!out || !v || !m) return nullptr;
  out->x = v->x * m->_11 + v->y * m->_21 + v->z * m->_31 + m->_41;
  out->y = v->x * m->_12 + v->y * m->_22 + v->z * m->_32 + m->_42;
  out->z = v->x * m->_13 + v->y * m->_23 + v->z * m->_33 + m->_43;
  out->w = v->x * m->_14 + v->y * m->_24 + v->z * m->_34 + m->_44;
  return out;
}

D3DXVECTOR3* D3DXVec3Normalize(D3DXVECTOR3* out, const D3DXVECTOR3* v) {
  if (!out || !v) return nullptr;
  *out = Normalize3(*v);
  return out;
}

D3DXVECTOR3* D3DXVec3Cross(D3DXVECTOR3* out, const D3DXVECTOR3* a, const D3DXVECTOR3* b) {
  if (!out || !a || !b) return nullptr;
  *out = Cross3(*a, *b);
  return out;
}

D3DXVECTOR3* D3DXVec3Lerp(D3DXVECTOR3* out, const D3DXVECTOR3* a, const D3DXVECTOR3* b, float t) {
  if (!out || !a || !b) return nullptr;
  t = Clamp01(t);
  out->x = a->x + (b->x - a->x) * t;
  out->y = a->y + (b->y - a->y) * t;
  out->z = a->z + (b->z - a->z) * t;
  return out;
}

float D3DXVec3Length(const D3DXVECTOR3* v) {
  if (!v) return 0.0f;
  return std::sqrt(v->x * v->x + v->y * v->y + v->z * v->z);
}

float D3DXVec3Dot(const D3DXVECTOR3* a, const D3DXVECTOR3* b) {
  if (!a || !b) return 0.0f;
  return Dot3(*a, *b);
}

D3DXVECTOR2* D3DXVec2Normalize(D3DXVECTOR2* out, const D3DXVECTOR2* in) {
  if (!out || !in) return nullptr;
  float len = std::sqrt(in->x * in->x + in->y * in->y);
  if (len <= 1.0e-7f) {
    out->x = out->y = 0.0f;
  } else {
    float inv = 1.0f / len;
    out->x = in->x * inv;
    out->y = in->y * inv;
  }
  return out;
}

float D3DXVec2Length(const D3DXVECTOR2* in) {
  if (!in) return 0.0f;
  return std::sqrt(in->x * in->x + in->y * in->y);
}

D3DXQUATERNION* D3DXQuaternionIdentity(D3DXQUATERNION* out) {
  if (!out) return nullptr;
  out->x = out->y = out->z = 0.0f;
  out->w = 1.0f;
  return out;
}

D3DXQUATERNION* D3DXQuaternionRotationMatrix(D3DXQUATERNION* out, const D3DXMATRIX* m) {
  if (!out || !m) return nullptr;

  float trace = m->_11 + m->_22 + m->_33;
  if (trace > 0.0f) {
    float s = std::sqrt(trace + 1.0f) * 2.0f;
    out->w = 0.25f * s;
    out->x = (m->_32 - m->_23) / s;
    out->y = (m->_13 - m->_31) / s;
    out->z = (m->_21 - m->_12) / s;
  } else if (m->_11 > m->_22 && m->_11 > m->_33) {
    float s = std::sqrt(1.0f + m->_11 - m->_22 - m->_33) * 2.0f;
    out->w = (m->_32 - m->_23) / s;
    out->x = 0.25f * s;
    out->y = (m->_12 + m->_21) / s;
    out->z = (m->_13 + m->_31) / s;
  } else if (m->_22 > m->_33) {
    float s = std::sqrt(1.0f + m->_22 - m->_11 - m->_33) * 2.0f;
    out->w = (m->_13 - m->_31) / s;
    out->x = (m->_12 + m->_21) / s;
    out->y = 0.25f * s;
    out->z = (m->_23 + m->_32) / s;
  } else {
    float s = std::sqrt(1.0f + m->_33 - m->_11 - m->_22) * 2.0f;
    out->w = (m->_21 - m->_12) / s;
    out->x = (m->_13 + m->_31) / s;
    out->y = (m->_23 + m->_32) / s;
    out->z = 0.25f * s;
  }
  return out;
}

D3DXQUATERNION* D3DXQuaternionSlerp(D3DXQUATERNION* out,
                                    const D3DXQUATERNION* q1,
                                    const D3DXQUATERNION* q2,
                                    float t) {
  if (!out || !q1 || !q2) return nullptr;
  t = Clamp01(t);

  float cos_theta = q1->x * q2->x + q1->y * q2->y + q1->z * q2->z + q1->w * q2->w;
  D3DXQUATERNION b = *q2;

  if (cos_theta < 0.0f) {
    cos_theta = -cos_theta;
    b.x = -b.x;
    b.y = -b.y;
    b.z = -b.z;
    b.w = -b.w;
  }

  if (cos_theta > 0.9995f) {
    out->x = q1->x + t * (b.x - q1->x);
    out->y = q1->y + t * (b.y - q1->y);
    out->z = q1->z + t * (b.z - q1->z);
    out->w = q1->w + t * (b.w - q1->w);
    float len = std::sqrt(out->x * out->x + out->y * out->y + out->z * out->z + out->w * out->w);
    if (len > 1.0e-7f) {
      float inv = 1.0f / len;
      out->x *= inv;
      out->y *= inv;
      out->z *= inv;
      out->w *= inv;
    }
    return out;
  }

  float theta = std::acos(cos_theta);
  float sin_theta = std::sin(theta);
  if (sin_theta <= 1.0e-7f) {
    *out = *q1;
    return out;
  }

  float w1 = std::sin((1.0f - t) * theta) / sin_theta;
  float w2 = std::sin(t * theta) / sin_theta;

  out->x = q1->x * w1 + b.x * w2;
  out->y = q1->y * w1 + b.y * w2;
  out->z = q1->z * w1 + b.z * w2;
  out->w = q1->w * w1 + b.w * w2;
  return out;
}

BOOL D3DXIntersectTri(const D3DXVECTOR3* p0,
                      const D3DXVECTOR3* p1,
                      const D3DXVECTOR3* p2,
                      const D3DXVECTOR3* rayPos,
                      const D3DXVECTOR3* rayDir,
                      float* u,
                      float* v,
                      float* dist) {
  if (!p0 || !p1 || !p2 || !rayPos || !rayDir) return FALSE;

  constexpr float eps = 1.0e-7f;
  D3DXVECTOR3 e1 = *p1 - *p0;
  D3DXVECTOR3 e2 = *p2 - *p0;
  D3DXVECTOR3 pvec = Cross3(*rayDir, e2);
  float det = Dot3(e1, pvec);

  if (std::fabs(det) < eps) return FALSE;
  float inv_det = 1.0f / det;

  D3DXVECTOR3 tvec = *rayPos - *p0;
  float tu = Dot3(tvec, pvec) * inv_det;
  if (tu < 0.0f || tu > 1.0f) return FALSE;

  D3DXVECTOR3 qvec = Cross3(tvec, e1);
  float tv = Dot3(*rayDir, qvec) * inv_det;
  if (tv < 0.0f || (tu + tv) > 1.0f) return FALSE;

  float td = Dot3(e2, qvec) * inv_det;
  if (td < 0.0f) return FALSE;

  if (u) *u = tu;
  if (v) *v = tv;
  if (dist) *dist = td;
  return TRUE;
}

D3DXVECTOR3* D3DXVec3Project(D3DXVECTOR3* out,
                             const D3DXVECTOR3* v,
                             const D3DVIEWPORT9* viewport,
                             const D3DXMATRIX* proj,
                             const D3DXMATRIX* view,
                             const D3DXMATRIX* world) {
  if (!out || !v || !viewport || !proj || !view || !world) return nullptr;

  D3DXMATRIX wv, wvp;
  D3DXMatrixMultiply(&wv, world, view);
  D3DXMatrixMultiply(&wvp, &wv, proj);

  D3DXVECTOR4 clip;
  D3DXVec3Transform(&clip, v, &wvp);
  if (std::fabs(clip.w) <= 1.0e-7f) {
    out->x = out->y = out->z = 0.0f;
    return out;
  }

  float inv_w = 1.0f / clip.w;
  float ndc_x = clip.x * inv_w;
  float ndc_y = clip.y * inv_w;
  float ndc_z = clip.z * inv_w;

  out->x = viewport->X + (1.0f + ndc_x) * static_cast<float>(viewport->Width) * 0.5f;
  out->y = viewport->Y + (1.0f - ndc_y) * static_cast<float>(viewport->Height) * 0.5f;
  out->z = viewport->MinZ + ndc_z * (viewport->MaxZ - viewport->MinZ);
  return out;
}

D3DXCOLOR* D3DXColorLerp(D3DXCOLOR* out, const D3DXCOLOR* c1, const D3DXCOLOR* c2, float s) {
  if (!out || !c1 || !c2) return nullptr;
  s = Clamp01(s);
  out->r = c1->r + (c2->r - c1->r) * s;
  out->g = c1->g + (c2->g - c1->g) * s;
  out->b = c1->b + (c2->b - c1->b) * s;
  out->a = c1->a + (c2->a - c1->a) * s;
  return out;
}

D3DXCOLOR* D3DXColorModulate(D3DXCOLOR* out, const D3DXCOLOR* c1, const D3DXCOLOR* c2) {
  if (!out || !c1 || !c2) return nullptr;
  out->r = c1->r * c2->r;
  out->g = c1->g * c2->g;
  out->b = c1->b * c2->b;
  out->a = c1->a * c2->a;
  return out;
}

HRESULT D3DXCreateTexture(IDirect3DDevice9*,
                          UINT Width,
                          UINT Height,
                          UINT,
                          DWORD,
                          D3DFORMAT Format,
                          D3DPOOL Pool,
                          IDirect3DTexture9** ppTexture) {
  if (!ppTexture) return E_POINTER;
  *ppTexture = nullptr;

  const UINT w = (Width == 0 || Width == static_cast<UINT>(-1)) ? 64u : Width;
  const UINT h = (Height == 0 || Height == static_cast<UINT>(-1)) ? 64u : Height;
  auto* tex = new DummyDirect3DTexture9(w, h, Format, Pool);
  if (!tex) return E_OUTOFMEMORY;
  *ppTexture = tex;
  return S_OK;
}

HRESULT D3DXCreateTextureFromFileInMemoryEx(IDirect3DDevice9*,
                                            LPCVOID pSrcData,
                                            UINT SrcDataSize,
                                            UINT Width,
                                            UINT Height,
                                            UINT,
                                            DWORD,
                                            D3DFORMAT Format,
                                            D3DPOOL Pool,
                                            DWORD,
                                            DWORD,
                                            D3DCOLOR ColorKey,
                                            void*,
                                            PALETTEENTRY*,
                                            IDirect3DTexture9** ppTexture) {
  if (!ppTexture) return E_POINTER;
  *ppTexture = nullptr;

  const bool default_w = (Width == 0 || Width == static_cast<UINT>(-1));
  const bool default_h = (Height == 0 || Height == static_cast<UINT>(-1));
  UINT decoded_w = 0;
  UINT decoded_h = 0;
  std::vector<uint8_t> decoded_rgba;

  if (pSrcData && SrcDataSize > 0) {
    if (DecodeTextureToRGBA(
        pSrcData,
        static_cast<size_t>(SrcDataSize),
        &decoded_w,
        &decoded_h,
        &decoded_rgba)) {
      g_tex_decode_success += 1;
    } else {
      g_tex_decode_fail += 1;
    }
  }

  const UINT w = default_w ? (decoded_w > 0 ? decoded_w : 64u) : Width;
  const UINT h = default_h ? (decoded_h > 0 ? decoded_h : 64u) : Height;
  bool force_opaque_format = false;
  if (!decoded_rgba.empty() && ColorKey == 0 && Format == D3DFMT_UNKNOWN && TextureAlphaAllZero(decoded_rgba)) {
    force_opaque_format = true;
    g_tex_alpha_promoted_opaque += 1;
  }
  const D3DFORMAT texture_format =
      (Format == D3DFMT_UNKNOWN) ? (force_opaque_format ? D3DFMT_X8R8G8B8 : D3DFMT_A8R8G8B8) : Format;

  auto* tex = new DummyDirect3DTexture9(w, h, texture_format, Pool);
  if (!tex) return E_OUTOFMEMORY;
  if (pSrcData && SrcDataSize > 0 && !g_last_closed_debug_source_path.empty()) {
    tex->debug_source_path = g_last_closed_debug_source_path;
    g_last_closed_debug_source_path.clear();
  }
  if (!decoded_rgba.empty() && decoded_w > 0 && decoded_h > 0) {
    ApplyColorKeyRGBA(&decoded_rgba, ColorKey);
    PackRGBAForTexture(decoded_rgba, decoded_w, decoded_h, tex);
  } else {
    tex->gl_dirty = true;
  }
  *ppTexture = tex;
  return S_OK;
}

HRESULT D3DXCreateBuffer(DWORD NumBytes, ID3DXBuffer** ppBuffer) {
  if (!ppBuffer) return E_POINTER;
  *ppBuffer = new DummyD3DXBuffer(NumBytes);
  if (!*ppBuffer) return E_OUTOFMEMORY;
  return S_OK;
}

HRESULT D3DXCreateSprite(IDirect3DDevice9* device, ID3DXSprite** ppSprite) {
  if (!ppSprite) return E_POINTER;
  *ppSprite = new DummyD3DXSprite(device);
  return (*ppSprite) ? S_OK : E_OUTOFMEMORY;
}

HRESULT D3DXSaveSurfaceToFile(LPCSTR,
                              D3DXIMAGE_FILEFORMAT,
                              IDirect3DSurface9*,
                              const PALETTEENTRY*,
                              const RECT*) {
  return E_NOTIMPL;
}

HRESULT D3DXCreateFont(...) {
  return E_NOTIMPL;
}

}  // extern "C"

namespace {

struct WasmD3D9State {
  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = 0;
  bool webgl2 = false;
  bool scene_active = false;
  D3DVIEWPORT9 viewport{0, 0, static_cast<DWORD>(kDefaultWidth), static_cast<DWORD>(kDefaultHeight), 0.0f, 1.0f};
  DWORD src_blend = D3DBLEND_SRCALPHA;
  DWORD dst_blend = D3DBLEND_INVSRCALPHA;
  bool blend_enabled = false;
  bool z_enable = true;
  bool z_write_enable = true;
  DWORD z_func = D3DCMP_LESSEQUAL;
  DWORD alpha_test_enable = 0;
  DWORD alpha_ref = 0;
  DWORD alpha_func = D3DCMP_ALWAYS;
  DWORD lighting_enable = 0;
  DWORD fill_mode = D3DFILL_SOLID;
  DWORD shade_mode = D3DSHADE_GOURAUD;
  DWORD specular_enable = 0;
  DWORD dither_enable = 1;
  DWORD stencil_enable = 0;
  DWORD clipping_enable = 1;
  DWORD multisample_antialias = 0;
  DWORD antialiased_line_enable = 0;
  DWORD vertex_blend = 0;
  DWORD indexed_vertex_blend_enable = 0;
  DWORD normalize_normals = 0;
  DWORD fog_enable = 0;
  DWORD fog_vertex_mode = D3DFOG_NONE;
  DWORD fog_table_mode = D3DFOG_NONE;
  D3DCOLOR fog_color = 0x00000000u;
  float fog_start = 0.0f;
  float fog_end = 10000.0f;
  float fog_density = 1.0f;
  DWORD range_fog_enable = 0;
  DWORD cull_mode = D3DCULL_CCW;
  D3DCOLOR ambient = 0xFFFFFFFFu;
  D3DCOLOR texture_factor = 0xFFFFFFFFu;
  DWORD color_vertex = 1u;
  DWORD diffuse_material_source = D3DMCS_COLOR1;
  DWORD ambient_material_source = D3DMCS_MATERIAL;
  DWORD emissive_material_source = D3DMCS_MATERIAL;
  DWORD specular_material_source = D3DMCS_COLOR2;
  D3DMATERIAL9 material{
      {1.0f, 1.0f, 1.0f, 1.0f},
      {1.0f, 1.0f, 1.0f, 1.0f},
      {0.0f, 0.0f, 0.0f, 1.0f},
      {0.0f, 0.0f, 0.0f, 1.0f},
      0.0f};
  std::array<D3DLIGHT9, 8> lights{};
  std::array<BOOL, 8> light_enabled{};
};

WasmD3D9State g_wasm_d3d9_state;

GLenum BlendFactorFromD3D(DWORD blend) {
  switch (blend) {
    case D3DBLEND_ZERO: return GL_ZERO;
    case D3DBLEND_ONE: return GL_ONE;
    case D3DBLEND_SRCCOLOR: return GL_SRC_COLOR;
    case D3DBLEND_INVSRCCOLOR: return GL_ONE_MINUS_SRC_COLOR;
    case D3DBLEND_SRCALPHA: return GL_SRC_ALPHA;
    case D3DBLEND_INVSRCALPHA: return GL_ONE_MINUS_SRC_ALPHA;
    case D3DBLEND_DESTALPHA: return GL_DST_ALPHA;
    case D3DBLEND_INVDESTALPHA: return GL_ONE_MINUS_DST_ALPHA;
    case D3DBLEND_DESTCOLOR: return GL_DST_COLOR;
    case D3DBLEND_INVDESTCOLOR: return GL_ONE_MINUS_DST_COLOR;
    default: return GL_ONE;
  }
}

void ApplyBlendState() {
  glBlendFunc(BlendFactorFromD3D(g_wasm_d3d9_state.src_blend), BlendFactorFromD3D(g_wasm_d3d9_state.dst_blend));
}

GLenum FrontFaceFromCullMode(DWORD cull_mode) {
  switch (cull_mode) {
    case D3DCULL_CW:
      return GL_CCW;
    case D3DCULL_CCW:
    default:
      return GL_CW;
  }
}

void ApplyCullState(bool mirrored_winding = false) {
  if ((g_debug_ffp_flags & kDebugDisableCull) != 0u) {
    glDisable(GL_CULL_FACE);
    return;
  }
  switch (g_wasm_d3d9_state.cull_mode) {
    case D3DCULL_NONE:
      glDisable(GL_CULL_FACE);
      return;
    case D3DCULL_CW:
    case D3DCULL_CCW:
    default: {
      GLenum front_face = FrontFaceFromCullMode(g_wasm_d3d9_state.cull_mode);
      if (mirrored_winding) {
        front_face = (front_face == GL_CCW) ? GL_CW : GL_CCW;
      }
      glEnable(GL_CULL_FACE);
      glCullFace(GL_BACK);
      glFrontFace(front_face);
      return;
    }
  }
}

GLenum DepthFuncFromD3D(DWORD func) {
  switch (func) {
    case D3DCMP_NEVER: return GL_NEVER;
    case D3DCMP_LESS: return GL_LESS;
    case D3DCMP_EQUAL: return GL_EQUAL;
    case D3DCMP_LESSEQUAL: return GL_LEQUAL;
    case D3DCMP_GREATER: return GL_GREATER;
    case D3DCMP_NOTEQUAL: return GL_NOTEQUAL;
    case D3DCMP_GREATEREQUAL: return GL_GEQUAL;
    case D3DCMP_ALWAYS: return GL_ALWAYS;
    default: return GL_LEQUAL;
  }
}

bool EnsureWasmContext() {
  if (g_wasm_d3d9_state.ctx > 0) {
    emscripten_webgl_make_context_current(g_wasm_d3d9_state.ctx);
    return true;
  }

  EmscriptenWebGLContextAttributes attrs;
  emscripten_webgl_init_context_attributes(&attrs);
  attrs.alpha = EM_FALSE;
  attrs.depth = EM_TRUE;
  attrs.stencil = EM_FALSE;
  attrs.antialias = EM_TRUE;
  attrs.premultipliedAlpha = EM_FALSE;
  attrs.preserveDrawingBuffer = EM_TRUE;
  attrs.majorVersion = 2;
  attrs.minorVersion = 0;

  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = emscripten_webgl_create_context("#canvas", &attrs);
  bool webgl2 = (ctx > 0);
  if (ctx <= 0) {
    attrs.majorVersion = 1;
    attrs.minorVersion = 0;
    ctx = emscripten_webgl_create_context("#canvas", &attrs);
    webgl2 = false;
  }
  if (ctx <= 0) return false;

  g_wasm_d3d9_state.ctx = ctx;
  g_wasm_d3d9_state.webgl2 = webgl2;
  if (emscripten_webgl_make_context_current(ctx) != EMSCRIPTEN_RESULT_SUCCESS) return false;

  int canvas_w = kDefaultWidth;
  int canvas_h = kDefaultHeight;
  emscripten_get_canvas_element_size("#canvas", &canvas_w, &canvas_h);
  if (canvas_w <= 0) canvas_w = kDefaultWidth;
  if (canvas_h <= 0) canvas_h = kDefaultHeight;

  g_wasm_d3d9_state.viewport.X = 0;
  g_wasm_d3d9_state.viewport.Y = 0;
  g_wasm_d3d9_state.viewport.Width = static_cast<DWORD>(canvas_w);
  g_wasm_d3d9_state.viewport.Height = static_cast<DWORD>(canvas_h);
  g_wasm_d3d9_state.viewport.MinZ = 0.0f;
  g_wasm_d3d9_state.viewport.MaxZ = 1.0f;

  glViewport(0, 0, canvas_w, canvas_h);
  ApplyCullState();
  if (g_wasm_d3d9_state.z_enable) glEnable(GL_DEPTH_TEST);
  else glDisable(GL_DEPTH_TEST);
  glDepthFunc(DepthFuncFromD3D(g_wasm_d3d9_state.z_func));
  glDepthMask(g_wasm_d3d9_state.z_write_enable ? GL_TRUE : GL_FALSE);
  glDisable(GL_BLEND);
  ApplyBlendState();
  return true;
}

void ApplyViewport() {
  int canvas_w = static_cast<int>(std::max<DWORD>(1, g_wasm_d3d9_state.viewport.Width));
  int canvas_h = static_cast<int>(std::max<DWORD>(1, g_wasm_d3d9_state.viewport.Height));
  emscripten_get_canvas_element_size("#canvas", &canvas_w, &canvas_h);
  if (canvas_w <= 0) canvas_w = static_cast<int>(std::max<DWORD>(1, g_wasm_d3d9_state.viewport.Width));
  if (canvas_h <= 0) canvas_h = static_cast<int>(std::max<DWORD>(1, g_wasm_d3d9_state.viewport.Height));

  const GLint vp_x = static_cast<GLint>(g_wasm_d3d9_state.viewport.X);
  const GLsizei vp_w = static_cast<GLsizei>(std::max<DWORD>(1, g_wasm_d3d9_state.viewport.Width));
  const GLsizei vp_h = static_cast<GLsizei>(std::max<DWORD>(1, g_wasm_d3d9_state.viewport.Height));
  GLint vp_y = canvas_h - static_cast<GLint>(g_wasm_d3d9_state.viewport.Y) - static_cast<GLint>(vp_h);
  if (vp_y < 0) vp_y = 0;
  glViewport(vp_x, vp_y, vp_w, vp_h);

  float min_z = g_wasm_d3d9_state.viewport.MinZ;
  float max_z = g_wasm_d3d9_state.viewport.MaxZ;
  if (min_z < 0.0f) min_z = 0.0f;
  if (min_z > 1.0f) min_z = 1.0f;
  if (max_z < 0.0f) max_z = 0.0f;
  if (max_z > 1.0f) max_z = 1.0f;
  if (max_z < min_z) std::swap(min_z, max_z);
  glDepthRangef(min_z, max_z);
}

}  // namespace

HRESULT WydD3D9Device_TestCooperativeLevel(IDirect3DDevice9*) {
  return S_OK;
}

HRESULT WydD3D9Device_Present(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const void*) {
  if (!EnsureWasmContext()) return E_FAIL;
  g_debug_present_calls += 1;
  g_debug_last_present_time_ms = MillisecondsSinceStart();
  g_debug_last_present_blend_enabled = g_wasm_d3d9_state.blend_enabled ? 1u : 0u;
  g_debug_last_present_depth_enabled = g_wasm_d3d9_state.z_enable ? 1u : 0u;
  g_debug_last_present_depth_write = g_wasm_d3d9_state.z_write_enable ? 1u : 0u;
  g_debug_last_present_alpha_test = g_wasm_d3d9_state.alpha_test_enable ? 1u : 0u;
  g_debug_last_present_src_blend = g_wasm_d3d9_state.src_blend;
  g_debug_last_present_dst_blend = g_wasm_d3d9_state.dst_blend;
  glFlush();
  return S_OK;
}

HRESULT WydD3D9Device_BeginScene(IDirect3DDevice9*) {
  if (!EnsureWasmContext()) return E_FAIL;
  g_debug_begin_scene_calls += 1;
  ResetDrawOrderFrame();
  g_wasm_d3d9_state.scene_active = true;
  return S_OK;
}

HRESULT WydD3D9Device_EndScene(IDirect3DDevice9*) {
  if (!EnsureWasmContext()) return E_FAIL;
  g_debug_end_scene_calls += 1;
  g_wasm_d3d9_state.scene_active = false;
  return S_OK;
}

HRESULT WydD3D9Device_Clear(
    IDirect3DDevice9*,
    DWORD,
    const D3DRECT*,
    DWORD flags,
    D3DCOLOR color,
    float z,
    DWORD) {
  if (!EnsureWasmContext()) return E_FAIL;
  g_debug_clear_calls += 1;
  g_debug_last_clear_color = color;
  g_debug_last_clear_flags = flags;
  g_debug_last_clear_z = z;
  g_debug_last_clear_time_ms = MillisecondsSinceStart();
  ApplyViewport();

  float a = static_cast<float>((color >> 24) & 0xFFu) / 255.0f;
  float r = static_cast<float>((color >> 16) & 0xFFu) / 255.0f;
  float g = static_cast<float>((color >> 8) & 0xFFu) / 255.0f;
  float b = static_cast<float>(color & 0xFFu) / 255.0f;

  GLbitfield mask = 0;
  if (flags & D3DCLEAR_TARGET) {
    glClearColor(r, g, b, a);
    mask |= GL_COLOR_BUFFER_BIT;
  }
  if (flags & D3DCLEAR_ZBUFFER) {
    glClearDepthf(z);
    glDepthMask(GL_TRUE);
    mask |= GL_DEPTH_BUFFER_BIT;
  }
  if (mask == 0) return S_OK;

  glClear(mask);
  if (flags & D3DCLEAR_ZBUFFER) {
    glDepthMask(g_wasm_d3d9_state.z_write_enable ? GL_TRUE : GL_FALSE);
  }
  if (DrawTraceIsEnabled()) {
    g_draw_trace_top_samples.clear();
    g_draw_trace_top_export_cache.clear();
    for (auto& probe : g_draw_trace_probes) {
      probe.depth_valid = false;
      probe.depth = 1.0f;
      probe.draw_serial = 0;
      probe.result.clear();
      probe.hit_count = 0;
      probe.first_hit_draw_serial = 0;
      probe.nearest_hit_draw_serial = 0;
      probe.nearest_zwrite_draw_serial = 0;
      probe.nearest_hit_depth = 1.0f;
      probe.nearest_zwrite_depth = 1.0f;
      probe.first_hit_result.clear();
      probe.nearest_hit_result.clear();
      probe.nearest_zwrite_result.clear();
    }
  }
  return S_OK;
}

HRESULT WydD3D9Device_GetDeviceCaps(IDirect3DDevice9*, D3DCAPS9* caps) {
  if (!caps) return E_POINTER;
  std::memset(caps, 0, sizeof(*caps));
  caps->DeviceType = D3DDEVTYPE_HAL;
  caps->AdapterOrdinal = 0;
  caps->MaxTextureBlendStages = 8;
  caps->MaxTextureWidth = 4096;
  caps->MaxVertexBlendMatrices = 4;
  // WYD v769 ships ps_1_1/vs_1_1 bytecode, so advertise that baseline.
  caps->PixelShaderVersion = 0xFFFF0101u;
  caps->VertexShaderVersion = 0xFFFE0101u;
  return S_OK;
}

HRESULT WydD3D9Device_SetViewport(IDirect3DDevice9*, const D3DVIEWPORT9* vp) {
  if (vp) g_wasm_d3d9_state.viewport = *vp;
  if (!EnsureWasmContext()) return E_FAIL;
  ApplyViewport();
  return S_OK;
}

HRESULT WydD3D9Device_SetMaterial(IDirect3DDevice9*, const D3DMATERIAL9* material) {
  if (!material) return E_POINTER;
  g_wasm_d3d9_state.material = *material;
  g_material_set_calls += 1;
  return S_OK;
}

HRESULT WydD3D9Device_GetMaterial(IDirect3DDevice9*, D3DMATERIAL9* material) {
  if (!material) return E_POINTER;
  *material = g_wasm_d3d9_state.material;
  return S_OK;
}

HRESULT WydD3D9Device_SetLight(IDirect3DDevice9*, DWORD index, const D3DLIGHT9* light) {
  if (!light) return E_POINTER;
  if (index >= g_wasm_d3d9_state.lights.size()) return D3DERR_INVALIDCALL;
  g_wasm_d3d9_state.lights[index] = *light;
  g_light_set_calls += 1;
  return S_OK;
}

HRESULT WydD3D9Device_GetLight(IDirect3DDevice9*, DWORD index, D3DLIGHT9* light) {
  if (!light) return E_POINTER;
  if (index >= g_wasm_d3d9_state.lights.size()) return D3DERR_INVALIDCALL;
  *light = g_wasm_d3d9_state.lights[index];
  return S_OK;
}

HRESULT WydD3D9Device_LightEnable(IDirect3DDevice9*, DWORD index, BOOL enable) {
  if (index >= g_wasm_d3d9_state.light_enabled.size()) return D3DERR_INVALIDCALL;
  g_wasm_d3d9_state.light_enabled[index] = enable ? 1 : 0;
  g_light_enable_calls += 1;
  return S_OK;
}

HRESULT WydD3D9Device_GetLightEnable(IDirect3DDevice9*, DWORD index, BOOL* enable) {
  if (!enable) return E_POINTER;
  if (index >= g_wasm_d3d9_state.light_enabled.size()) return D3DERR_INVALIDCALL;
  *enable = g_wasm_d3d9_state.light_enabled[index] ? 1 : 0;
  return S_OK;
}

HRESULT WydD3D9Device_SetRenderState(IDirect3DDevice9*, D3DRENDERSTATETYPE state, DWORD value) {
  if (!EnsureWasmContext()) return E_FAIL;

  switch (state) {
    case D3DRS_ZENABLE:
      g_wasm_d3d9_state.z_enable = (value != 0);
      if (g_wasm_d3d9_state.z_enable) glEnable(GL_DEPTH_TEST);
      else glDisable(GL_DEPTH_TEST);
      break;
    case D3DRS_ZWRITEENABLE:
      g_wasm_d3d9_state.z_write_enable = (value != 0);
      glDepthMask(g_wasm_d3d9_state.z_write_enable ? GL_TRUE : GL_FALSE);
      break;
    case D3DRS_ZFUNC:
      g_wasm_d3d9_state.z_func = value;
      glDepthFunc(DepthFuncFromD3D(value));
      break;
    case D3DRS_ALPHABLENDENABLE:
      g_wasm_d3d9_state.blend_enabled = (value != 0);
      if (g_wasm_d3d9_state.blend_enabled) glEnable(GL_BLEND);
      else glDisable(GL_BLEND);
      ApplyBlendState();
      break;
    case D3DRS_SRCBLEND:
      g_wasm_d3d9_state.src_blend = value;
      ApplyBlendState();
      break;
    case D3DRS_DESTBLEND:
      g_wasm_d3d9_state.dst_blend = value;
      ApplyBlendState();
      break;
    case D3DRS_ALPHATESTENABLE:
      g_wasm_d3d9_state.alpha_test_enable = value;
      break;
    case D3DRS_ALPHAREF:
      g_wasm_d3d9_state.alpha_ref = value;
      break;
    case D3DRS_ALPHAFUNC:
      g_wasm_d3d9_state.alpha_func = value;
      break;
    case D3DRS_LIGHTING:
      g_wasm_d3d9_state.lighting_enable = value;
      break;
    case D3DRS_FILLMODE:
      g_wasm_d3d9_state.fill_mode = value;
      break;
    case D3DRS_SHADEMODE:
      g_wasm_d3d9_state.shade_mode = value;
      break;
    case D3DRS_SPECULARENABLE:
      g_wasm_d3d9_state.specular_enable = value;
      break;
    case D3DRS_DITHERENABLE:
      g_wasm_d3d9_state.dither_enable = value;
      if (value) glEnable(GL_DITHER);
      else glDisable(GL_DITHER);
      break;
    case D3DRS_STENCILENABLE:
      g_wasm_d3d9_state.stencil_enable = value;
      if (value) glEnable(GL_STENCIL_TEST);
      else glDisable(GL_STENCIL_TEST);
      break;
    case D3DRS_CLIPPING:
      g_wasm_d3d9_state.clipping_enable = value;
      break;
    case D3DRS_MULTISAMPLEANTIALIAS:
      g_wasm_d3d9_state.multisample_antialias = value;
      break;
    case D3DRS_ANTIALIASEDLINEENABLE:
      g_wasm_d3d9_state.antialiased_line_enable = value;
      break;
    case D3DRS_VERTEXBLEND:
      g_wasm_d3d9_state.vertex_blend = value;
      break;
    case D3DRS_INDEXEDVERTEXBLENDENABLE:
      g_wasm_d3d9_state.indexed_vertex_blend_enable = value;
      break;
    case D3DRS_NORMALIZENORMALS:
      g_wasm_d3d9_state.normalize_normals = value;
      break;
    case D3DRS_FOGENABLE:
      g_wasm_d3d9_state.fog_enable = value;
      break;
    case D3DRS_FOGCOLOR:
      g_wasm_d3d9_state.fog_color = value;
      break;
    case D3DRS_FOGVERTEXMODE:
      g_wasm_d3d9_state.fog_vertex_mode = value;
      break;
    case D3DRS_FOGTABLEMODE:
      g_wasm_d3d9_state.fog_table_mode = value;
      break;
    case D3DRS_FOGSTART:
      g_wasm_d3d9_state.fog_start = FloatFromDWORD(value);
      break;
    case D3DRS_FOGEND:
      g_wasm_d3d9_state.fog_end = FloatFromDWORD(value);
      break;
    case D3DRS_FOGDENSITY:
      g_wasm_d3d9_state.fog_density = FloatFromDWORD(value);
      break;
    case D3DRS_RANGEFOGENABLE:
      g_wasm_d3d9_state.range_fog_enable = value;
      break;
    case D3DRS_AMBIENT:
      g_wasm_d3d9_state.ambient = value;
      break;
    case D3DRS_TEXTUREFACTOR:
      g_wasm_d3d9_state.texture_factor = value;
      break;
    case D3DRS_CULLMODE:
      g_wasm_d3d9_state.cull_mode = value;
      ApplyCullState();
      break;
    case D3DRS_COLORVERTEX:
      g_wasm_d3d9_state.color_vertex = value ? 1u : 0u;
      break;
    case D3DRS_DIFFUSEMATERIALSOURCE:
      g_wasm_d3d9_state.diffuse_material_source = value;
      break;
    case D3DRS_AMBIENTMATERIALSOURCE:
      g_wasm_d3d9_state.ambient_material_source = value;
      break;
    case D3DRS_EMISSIVEMATERIALSOURCE:
      g_wasm_d3d9_state.emissive_material_source = value;
      break;
    case D3DRS_SPECULARMATERIALSOURCE:
      g_wasm_d3d9_state.specular_material_source = value;
      break;
    default:
      break;
  }

  return S_OK;
}

namespace {

constexpr int kMaxTextureStages = 8;
constexpr int kMaxWorldMatrices = 32;
constexpr float kEpsilonW = 1.0e-7f;

struct FFPVertex {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float w = 1.0f;
  float u0 = 0.0f;
  float v0 = 0.0f;
  float u1 = 0.0f;
  float v1 = 0.0f;
  float cam_pos_x = 0.0f;
  float cam_pos_y = 0.0f;
  float cam_pos_z = 1.0f;
  float cam_nrm_x = 0.0f;
  float cam_nrm_y = 0.0f;
  float cam_nrm_z = 1.0f;
  uint8_t r = 255;
  uint8_t g = 255;
  uint8_t b = 255;
  uint8_t a = 255;
};

struct WasmFFPProgram {
  GLuint program = 0;
  GLuint vbo = 0;
  GLuint ibo = 0;
  GLint attr_pos = -1;
  GLint attr_uv0 = -1;
  GLint attr_uv1 = -1;
  GLint attr_cam_pos = -1;
  GLint attr_cam_nrm = -1;
  GLint attr_color = -1;
  GLint uni_sampler0 = -1;
  GLint uni_sampler1 = -1;
  GLint uni_use_texture0 = -1;
  GLint uni_use_texture1 = -1;
  GLint uni_color_op0 = -1;
  GLint uni_color_arg10 = -1;
  GLint uni_color_arg20 = -1;
  GLint uni_alpha_op0 = -1;
  GLint uni_alpha_arg10 = -1;
  GLint uni_alpha_arg20 = -1;
  GLint uni_color_op1 = -1;
  GLint uni_color_arg11 = -1;
  GLint uni_color_arg21 = -1;
  GLint uni_alpha_op1 = -1;
  GLint uni_alpha_arg11 = -1;
  GLint uni_alpha_arg21 = -1;
  GLint uni_stage0_texcoord_set = -1;
  GLint uni_stage1_texcoord_set = -1;
  GLint uni_stage0_tci_mode = -1;
  GLint uni_stage1_tci_mode = -1;
  GLint uni_tex_transform0 = -1;
  GLint uni_tex_transform1 = -1;
  GLint uni_tex_transform_flags0 = -1;
  GLint uni_tex_transform_flags1 = -1;
  GLint uni_texture_factor = -1;
  GLint uni_ambient = -1;
  GLint uni_lighting_enable = -1;
  GLint uni_blend_enable = -1;
  GLint uni_alpha_test_enable = -1;
  GLint uni_alpha_ref = -1;
  GLint uni_alpha_func = -1;
  GLint uni_debug_flags = -1;
  GLint uni_fog_color = -1;
  GLint uni_fog_enable = -1;
  GLint uni_fog_mode = -1;
  GLint uni_fog_start = -1;
  GLint uni_fog_end = -1;
  GLint uni_fog_density = -1;
  GLint uni_range_fog_enable = -1;
  bool ready = false;
};

struct WasmFixedFunctionState {
  D3DMATRIX world[kMaxWorldMatrices]{};
  D3DMATRIX view{};
  D3DMATRIX proj{};
  D3DMATRIX tex[kMaxTextureStages]{};
  DWORD current_fvf = D3DFVF_XYZ | D3DFVF_DIFFUSE;
  IDirect3DVertexDeclaration9* vertex_decl = nullptr;
  IDirect3DVertexShader9* vertex_shader = nullptr;
  IDirect3DPixelShader9* pixel_shader = nullptr;
  std::array<float, 256 * 4> vertex_shader_constants{};
  IDirect3DVertexBuffer9* stream0 = nullptr;
  UINT stream0_offset = 0;
  UINT stream0_stride = 0;
  IDirect3DIndexBuffer9* indices = nullptr;
  std::array<IDirect3DBaseTexture9*, kMaxTextureStages> textures{};
  std::array<std::array<DWORD, 32>, kMaxTextureStages> tex_stage{};
  std::array<std::array<DWORD, 16>, kMaxTextureStages> sampler{};
  uint64_t draw_calls = 0;
  uint64_t primitive_count = 0;
  uint64_t shader_draw_skipped = 0;
};

WasmFFPProgram g_ffp_program;
WasmFixedFunctionState g_ffp_state;
DummyDirect3DSurface9* g_default_color_surface = nullptr;
DummyDirect3DSurface9* g_default_depth_surface = nullptr;
bool g_ffp_state_initialized = false;

template <typename T>
T ReadUnaligned(const uint8_t* ptr) {
  T out{};
  if (!ptr) return out;
  std::memcpy(&out, ptr, sizeof(T));
  return out;
}

uint64_t Fnv1a64(const uint8_t* data, size_t size) {
  constexpr uint64_t kOffset = 1469598103934665603ull;
  constexpr uint64_t kPrime = 1099511628211ull;
  uint64_t h = kOffset;
  for (size_t i = 0; i < size; ++i) {
    h ^= static_cast<uint64_t>(data[i]);
    h *= kPrime;
  }
  return h;
}

uint64_t HashShaderBytecode(const std::vector<DWORD>& bytecode) {
  if (bytecode.empty()) return 0;
  return Fnv1a64(
      reinterpret_cast<const uint8_t*>(bytecode.data()),
      bytecode.size() * sizeof(DWORD));
}

void TrackShaderCreate(
    std::unordered_map<uint64_t, ShaderTelemetryEntry>* map,
    uint64_t hash,
    uint32_t dword_count,
    uint32_t version_token) {
  if (!map || hash == 0) return;
  ShaderTelemetryEntry& entry = (*map)[hash];
  entry.creates += 1;
  if (entry.dword_count == 0) entry.dword_count = dword_count;
  if (entry.version_token == 0) entry.version_token = version_token;
}

void TrackShaderBind(
    std::unordered_map<uint64_t, ShaderTelemetryEntry>* map,
    uint64_t hash) {
  if (!map || hash == 0) return;
  (*map)[hash].binds += 1;
}

void TrackShaderDrawUsed(
    std::unordered_map<uint64_t, ShaderTelemetryEntry>* map,
    uint64_t hash) {
  if (!map || hash == 0) return;
  (*map)[hash].draws_used += 1;
}

void TrackShaderDrawSkipped(
    std::unordered_map<uint64_t, ShaderTelemetryEntry>* map,
    uint64_t hash) {
  if (!map || hash == 0) return;
  (*map)[hash].draws_skipped += 1;
}

std::pair<uint64_t, ShaderTelemetryEntry> TopShaderTelemetryEntry(
    const std::unordered_map<uint64_t, ShaderTelemetryEntry>& map,
    size_t rank) {
  std::vector<std::pair<uint64_t, ShaderTelemetryEntry>> entries;
  entries.reserve(map.size());
  for (const auto& kv : map) entries.push_back(kv);
  std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
    if (a.second.draws_used != b.second.draws_used) return a.second.draws_used > b.second.draws_used;
    if (a.second.binds != b.second.binds) return a.second.binds > b.second.binds;
    return a.first < b.first;
  });
  if (rank >= entries.size()) return {0u, ShaderTelemetryEntry{}};
  return entries[rank];
}

void SetIdentity(D3DMATRIX* mat) {
  if (!mat) return;
  std::memset(mat, 0, sizeof(*mat));
  mat->_11 = mat->_22 = mat->_33 = mat->_44 = 1.0f;
}

void EnsureFFPStateInitialized() {
  if (g_ffp_state_initialized) return;
  for (int i = 0; i < kMaxWorldMatrices; ++i) SetIdentity(&g_ffp_state.world[i]);
  SetIdentity(&g_ffp_state.view);
  SetIdentity(&g_ffp_state.proj);
  for (int i = 0; i < kMaxTextureStages; ++i) SetIdentity(&g_ffp_state.tex[i]);
  for (int i = 0; i < kMaxTextureStages; ++i) {
    g_ffp_state.tex_stage[i].fill(0);
    g_ffp_state.sampler[i].fill(0);
    g_ffp_state.sampler[i][D3DSAMP_ADDRESSU] = 1;
    g_ffp_state.sampler[i][D3DSAMP_ADDRESSV] = 1;
    g_ffp_state.tex_stage[i][D3DTSS_COLORARG1] = D3DTA_TEXTURE;
    g_ffp_state.tex_stage[i][D3DTSS_COLORARG2] = D3DTA_CURRENT;
    g_ffp_state.tex_stage[i][D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
    g_ffp_state.tex_stage[i][D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
    g_ffp_state.tex_stage[i][D3DTSS_COLOROP] = D3DTOP_DISABLE;
    g_ffp_state.tex_stage[i][D3DTSS_ALPHAOP] = D3DTOP_DISABLE;
  }
  g_ffp_state.tex_stage[0][D3DTSS_COLOROP] = D3DTOP_MODULATE;
  g_ffp_state.tex_stage[0][D3DTSS_COLORARG1] = D3DTA_TEXTURE;
  g_ffp_state.tex_stage[0][D3DTSS_COLORARG2] = D3DTA_DIFFUSE;
  g_ffp_state.tex_stage[0][D3DTSS_ALPHAOP] = D3DTOP_SELECTARG1;
  g_ffp_state.tex_stage[0][D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
  g_ffp_state.tex_stage[0][D3DTSS_ALPHAARG2] = D3DTA_DIFFUSE;
  g_ffp_state_initialized = true;
}

void DummyD3DXSprite::SaveDeviceState() {
  saved_state_ = SpriteStateSnapshot{};
  saved_state_.current_fvf = g_ffp_state.current_fvf;
  saved_state_.z_enable = g_wasm_d3d9_state.z_enable;
  saved_state_.z_write_enable = g_wasm_d3d9_state.z_write_enable;
  saved_state_.src_blend = g_wasm_d3d9_state.src_blend;
  saved_state_.dst_blend = g_wasm_d3d9_state.dst_blend;
  saved_state_.blend_enabled = g_wasm_d3d9_state.blend_enabled;
  saved_state_.alpha_test_enable = g_wasm_d3d9_state.alpha_test_enable;
  saved_state_.alpha_ref = g_wasm_d3d9_state.alpha_ref;
  saved_state_.alpha_func = g_wasm_d3d9_state.alpha_func;
  saved_state_.lighting_enable = g_wasm_d3d9_state.lighting_enable;
  saved_state_.texture0 = g_ffp_state.textures[0];
  saved_state_.texture1 = g_ffp_state.textures[1];
  if (saved_state_.texture0) saved_state_.texture0->AddRef();
  if (saved_state_.texture1) saved_state_.texture1->AddRef();
  saved_state_.stage0 = g_ffp_state.tex_stage[0];
  saved_state_.stage1 = g_ffp_state.tex_stage[1];
  saved_state_.vertex_shader = g_ffp_state.vertex_shader;
  saved_state_.pixel_shader = g_ffp_state.pixel_shader;
  if (saved_state_.vertex_shader) saved_state_.vertex_shader->AddRef();
  if (saved_state_.pixel_shader) saved_state_.pixel_shader->AddRef();
}

void DummyD3DXSprite::RestoreDeviceState() {
  device_->SetRenderState(D3DRS_ZENABLE, saved_state_.z_enable ? D3DZB_TRUE : D3DZB_FALSE);
  device_->SetRenderState(D3DRS_ZWRITEENABLE, saved_state_.z_write_enable ? 1u : 0u);
  device_->SetRenderState(D3DRS_ALPHABLENDENABLE, saved_state_.blend_enabled ? 1u : 0u);
  device_->SetRenderState(D3DRS_SRCBLEND, saved_state_.src_blend);
  device_->SetRenderState(D3DRS_DESTBLEND, saved_state_.dst_blend);
  device_->SetRenderState(D3DRS_ALPHATESTENABLE, saved_state_.alpha_test_enable);
  device_->SetRenderState(D3DRS_ALPHAREF, saved_state_.alpha_ref);
  device_->SetRenderState(D3DRS_ALPHAFUNC, saved_state_.alpha_func);
  device_->SetRenderState(D3DRS_LIGHTING, saved_state_.lighting_enable);
  device_->SetTexture(0, saved_state_.texture0);
  device_->SetTexture(1, saved_state_.texture1);
  for (int i = 0; i < 32; ++i) {
    device_->SetTextureStageState(0, static_cast<D3DTEXTURESTAGESTATETYPE>(i), saved_state_.stage0[i]);
    device_->SetTextureStageState(1, static_cast<D3DTEXTURESTAGESTATETYPE>(i), saved_state_.stage1[i]);
  }
  device_->SetFVF(saved_state_.current_fvf);
  device_->SetVertexShader(saved_state_.vertex_shader);
  device_->SetPixelShader(saved_state_.pixel_shader);
}

void DummyD3DXSprite::ReleaseSnapshotRefs() {
  if (saved_state_.texture0) {
    saved_state_.texture0->Release();
    saved_state_.texture0 = nullptr;
  }
  if (saved_state_.texture1) {
    saved_state_.texture1->Release();
    saved_state_.texture1 = nullptr;
  }
  if (saved_state_.vertex_shader) {
    saved_state_.vertex_shader->Release();
    saved_state_.vertex_shader = nullptr;
  }
  if (saved_state_.pixel_shader) {
    saved_state_.pixel_shader->Release();
    saved_state_.pixel_shader = nullptr;
  }
}

DummyDirect3DVertexBuffer9* AsVertexBuffer(IDirect3DVertexBuffer9* vb) {
  return vb ? static_cast<DummyDirect3DVertexBuffer9*>(vb) : nullptr;
}

DummyDirect3DIndexBuffer9* AsIndexBuffer(IDirect3DIndexBuffer9* ib) {
  return ib ? static_cast<DummyDirect3DIndexBuffer9*>(ib) : nullptr;
}

DummyDirect3DVertexDeclaration9* AsVertexDeclaration(IDirect3DVertexDeclaration9* decl) {
  return decl ? static_cast<DummyDirect3DVertexDeclaration9*>(decl) : nullptr;
}

DummyDirect3DVertexShader9* AsVertexShader(IDirect3DVertexShader9* shader) {
  return shader ? static_cast<DummyDirect3DVertexShader9*>(shader) : nullptr;
}

DummyDirect3DPixelShader9* AsPixelShader(IDirect3DPixelShader9* shader) {
  return shader ? static_cast<DummyDirect3DPixelShader9*>(shader) : nullptr;
}

DummyDirect3DTexture9* AsTexture(IDirect3DBaseTexture9* texture) {
  return texture ? static_cast<DummyDirect3DTexture9*>(texture) : nullptr;
}

bool TexturePathContains(IDirect3DBaseTexture9* texture, const char* needle) {
  auto* tex = AsTexture(texture);
  if (!tex || !needle || *needle == '\0') return false;
  return tex->debug_source_path.find(needle) != std::string::npos;
}

bool CurrentDrawUsesTexturePath(const char* needle) {
  for (auto* texture : g_ffp_state.textures) {
    if (TexturePathContains(texture, needle)) return true;
  }
  return false;
}

bool CurrentDrawUsesSnowBillboardTexture() {
  // TMSnow renders 3D billboards with Effect/bright.wys and FVF322. Their
  // local vertices look screen-like, but must still go through the world matrix.
  return CurrentDrawUsesTexturePath("effect/bright.wys");
}

bool CurrentDrawUsesSkyTexture() {
  // TMSky is also FVF322 and can look screen-like in raw local coordinates.
  // Replaying it as 2D screen geometry expands the sky dome into huge planes.
  return CurrentDrawUsesTexturePath("sky");
}

void TrackSpecialTextureDraws(DWORD active_fvf) {
  bool saw_sky = false;
  bool saw_water = false;
  bool saw_bright = false;
  for (auto* texture : g_ffp_state.textures) {
    saw_sky = saw_sky || TexturePathContains(texture, "sky");
    saw_water = saw_water || TexturePathContains(texture, "water");
    saw_bright = saw_bright || TexturePathContains(texture, "effect/bright.wys");
  }
  if (saw_sky) g_texture_draws_sky += 1;
  if (saw_water) g_texture_draws_water += 1;
  if (saw_bright) {
    g_texture_draws_bright += 1;
    if (active_fvf == 322u) g_fvf322_bright_draws += 1;
  }
}

DummyDirect3DSurface9* AsSurface(IDirect3DSurface9* surface) {
  return surface ? static_cast<DummyDirect3DSurface9*>(surface) : nullptr;
}

DWORD PositionMaskFromFVF(DWORD fvf) {
  return fvf & 0x400Eu;
}

UINT PositionBlendCountFromFVF(DWORD fvf) {
  switch (PositionMaskFromFVF(fvf)) {
    case D3DFVF_XYZB1: return 1;
    case D3DFVF_XYZB2: return 2;
    case D3DFVF_XYZB3: return 3;
    case D3DFVF_XYZB4: return 4;
    case D3DFVF_XYZB5: return 5;
    default: return 0;
  }
}

bool PositionHasRHW(DWORD fvf) {
  return PositionMaskFromFVF(fvf) == D3DFVF_XYZRHW;
}

UINT PositionBytesFromFVF(DWORD fvf) {
  if (PositionHasRHW(fvf)) return 16;
  UINT bytes = 12;
  const UINT blend_count = PositionBlendCountFromFVF(fvf);
  if (blend_count > 0) {
    const bool has_lastbeta_ubyte4 = (fvf & D3DFVF_LASTBETA_UBYTE4) != 0;
    const bool has_lastbeta_color = (fvf & D3DFVF_LASTBETA_D3DCOLOR) != 0;
    UINT float_weights = blend_count;
    if (has_lastbeta_ubyte4 || has_lastbeta_color) {
      if (float_weights > 0) float_weights -= 1;
      bytes += 4;
    }
    bytes += float_weights * 4;
  }
  return bytes;
}

UINT EffectiveStride(DWORD fvf, UINT fallback_stride) {
  if (fallback_stride > 0) return fallback_stride;
  UINT stride = PositionBytesFromFVF(fvf);
  if (stride == 0) stride = 12;
  if (fvf & D3DFVF_NORMAL) stride += 12;
  if (fvf & D3DFVF_DIFFUSE) stride += 4;
  stride += ((fvf >> 8) & 0xFu) * 8u;
  return stride;
}

void TrackDrawFVF(DWORD fvf) {
  g_fvf_histogram[static_cast<uint32_t>(fvf)] += 1;
  if (PositionHasRHW(fvf)) g_draw_fvf_xyzrhw += 1;
  if (PositionBlendCountFromFVF(fvf) > 0) g_draw_fvf_weighted += 1;
  const UINT tex_count = (fvf >> 8) & 0xFu;
  if (tex_count >= 2) g_draw_fvf_tex2plus += 1;
}

void TrackFVF322Path(DWORD fvf, int path_kind) {
  if (fvf != 322u) return;
  switch (path_kind) {
    case 0: g_fvf322_draw_primitive_up += 1; break;
    case 1: g_fvf322_draw_indexed_primitive_up += 1; break;
    case 2: g_fvf322_draw_indexed_primitive += 1; break;
    default: break;
  }
}

void ResetUPStreamState(bool reset_indices) {
  if (g_ffp_state.stream0) {
    g_ffp_state.stream0->Release();
    g_ffp_state.stream0 = nullptr;
  }
  g_ffp_state.stream0_offset = 0;
  g_ffp_state.stream0_stride = 0;
  g_up_reset_stream0_calls += 1;

  if (reset_indices && g_ffp_state.indices) {
    g_ffp_state.indices->Release();
    g_ffp_state.indices = nullptr;
    g_up_reset_indices_calls += 1;
  }
}

std::pair<uint32_t, uint64_t> TopFVFEntry(size_t rank) {
  std::vector<std::pair<uint32_t, uint64_t>> items(g_fvf_histogram.begin(), g_fvf_histogram.end());
  std::sort(
      items.begin(),
      items.end(),
      [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
      });
  if (rank >= items.size()) return {0u, 0u};
  return items[rank];
}

std::pair<uint32_t, uint64_t> TopHistogramEntry(
    const std::unordered_map<uint32_t, uint64_t>& histogram,
    size_t rank) {
  std::vector<std::pair<uint32_t, uint64_t>> items(histogram.begin(), histogram.end());
  std::sort(
      items.begin(),
      items.end(),
      [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
      });
  if (rank >= items.size()) return {0u, 0u};
  return items[rank];
}

bool ShouldSkipFVFDraw(DWORD fvf) {
  if (g_debug_skip_fvf == 0u) return false;
  if (static_cast<uint32_t>(fvf) != g_debug_skip_fvf) return false;
  g_debug_skip_fvf_draws += 1;
  return true;
}

float MatrixUpper3x3Determinant(const D3DMATRIX& m) {
  return
      m._11 * (m._22 * m._33 - m._23 * m._32) -
      m._12 * (m._21 * m._33 - m._23 * m._31) +
      m._13 * (m._21 * m._32 - m._22 * m._31);
}

bool IsMirroredMatrix3x3(const D3DMATRIX& m) {
  return MatrixUpper3x3Determinant(m) < 0.0f;
}

void TrackDepthWriteFVF(DWORD fvf, bool depth_write_enabled) {
  auto& histogram = depth_write_enabled
      ? g_fvf_depth_write_enabled_histogram
      : g_fvf_depth_write_disabled_histogram;
  histogram[static_cast<uint32_t>(fvf)] += 1;
}

float SafeClipW(float clip_w) {
  if (std::fabs(clip_w) > kEpsilonW) return clip_w;
  return (clip_w < 0.0f) ? -kEpsilonW : kEpsilonW;
}

void TrackGLErrorsPostDraw() {
  bool had_error = false;
  for (;;) {
    const GLenum err = glGetError();
    if (err == GL_NO_ERROR) break;
    had_error = true;
    g_gl_error_total += 1;
    g_gl_error_last = static_cast<uint32_t>(err);
  }
  if (had_error) g_gl_error_draw_calls += 1;
}

GLenum PrimitiveToGL(D3DPRIMITIVETYPE primitive_type) {
  switch (primitive_type) {
    case D3DPT_POINTLIST: return GL_POINTS;
    case D3DPT_LINELIST: return GL_LINES;
    case D3DPT_LINESTRIP: return GL_LINE_STRIP;
    case D3DPT_TRIANGLELIST: return GL_TRIANGLES;
    case D3DPT_TRIANGLESTRIP: return GL_TRIANGLE_STRIP;
    case D3DPT_TRIANGLEFAN: return GL_TRIANGLE_FAN;
    default: return GL_TRIANGLES;
  }
}

UINT PrimitiveToVertexCount(D3DPRIMITIVETYPE primitive_type, UINT primitive_count) {
  switch (primitive_type) {
    case D3DPT_POINTLIST: return primitive_count;
    case D3DPT_LINELIST: return primitive_count * 2;
    case D3DPT_LINESTRIP: return primitive_count + 1;
    case D3DPT_TRIANGLELIST: return primitive_count * 3;
    case D3DPT_TRIANGLESTRIP:
    case D3DPT_TRIANGLEFAN: return primitive_count + 2;
    default: return 0;
  }
}

bool DrawTraceIsEnabled() {
  return g_draw_trace_enabled || ((g_debug_ffp_flags & kDebugEnableDrawTrace) != 0u);
}

DWORD StageStateValue(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD default_value);
void TexturePixelToRGBA(const DummyDirect3DTexture9* tex, const uint8_t* src, uint8_t* out_rgba);

std::string CurrentDrawTraceTexturePath(size_t stage) {
  if (stage >= g_ffp_state.textures.size()) return {};
  auto* tex = AsTexture(g_ffp_state.textures[stage]);
  return tex ? tex->debug_source_path : std::string{};
}

std::string CurrentDrawTraceLabel() {
  if (g_draw_trace_scope.empty()) return g_draw_trace_label;
  if (g_draw_trace_label.empty()) return g_draw_trace_scope;
  return g_draw_trace_scope + " | " + g_draw_trace_label;
}

bool ActiveFvfIsSkin(DWORD active_fvf) {
  return active_fvf >= 4370u && active_fvf <= 4388u;
}

enum Fvf322Class : uint32_t {
  kFvf322ClassSky = 0,
  kFvf322ClassShadow = 1,
  kFvf322ClassLightmap = 2,
  kFvf322ClassRain = 3,
  kFvf322ClassBright = 4,
  kFvf322ClassPattern = 5,
  kFvf322ClassOther = 6,
};

const char* Fvf322ClassName(uint32_t class_id) {
  switch (class_id) {
    case kFvf322ClassSky: return "sky";
    case kFvf322ClassShadow: return "shadow";
    case kFvf322ClassLightmap: return "lightmap";
    case kFvf322ClassRain: return "rain";
    case kFvf322ClassBright: return "bright";
    case kFvf322ClassPattern: return "pattern";
    case kFvf322ClassOther: return "other";
    default: return "unknown";
  }
}

uint32_t ClassifyFvf322Draw() {
  const std::string stage0_path = ToLowerCopy(CurrentDrawTraceTexturePath(0));
  const std::string stage1_path = ToLowerCopy(CurrentDrawTraceTexturePath(1));
  const std::string label = ToLowerCopy(CurrentDrawTraceLabel());
  const std::string combined = stage0_path + "|" + stage1_path + "|" + label;
  if (combined.find("sky") != std::string::npos) return kFvf322ClassSky;
  if (combined.find("shadow") != std::string::npos) return kFvf322ClassShadow;
  if (combined.find("lightmap") != std::string::npos) return kFvf322ClassLightmap;
  if (combined.find("rain") != std::string::npos) return kFvf322ClassRain;
  if (combined.find("bright") != std::string::npos) return kFvf322ClassBright;
  if (combined.find("pattern") != std::string::npos) return kFvf322ClassPattern;
  return kFvf322ClassOther;
}

std::string FormatDrawTraceGroup(DWORD active_fvf) {
  if (CurrentDrawUsesSkyTexture()) return "sky";
  if (ActiveFvfIsSkin(active_fvf)) return "skin";
  if (active_fvf == 594u) return "terrain594";
  if (active_fvf == 578u) return "water578";
  if (active_fvf == 530u) return "ground530";
  if (active_fvf == 322u) return std::string("fvf322/") + Fvf322ClassName(ClassifyFvf322Draw());
  return "other";
}

void ResetDrawOrderFrame() {
  g_draw_order_frame_first_sky = 0;
  g_draw_order_frame_first_skin = 0;
  g_draw_order_frame_first_terrain594 = 0;
  g_draw_order_frame_first_water578 = 0;
  g_draw_order_frame_first_fvf530 = 0;
  g_draw_order_frame_first_fvf322 = 0;
  g_draw_order_frame_count_sky = 0;
  g_draw_order_frame_count_skin = 0;
  g_draw_order_frame_count_terrain594 = 0;
  g_draw_order_frame_count_water578 = 0;
  g_draw_order_frame_count_fvf530 = 0;
  g_draw_order_frame_count_fvf322 = 0;
}

void TrackDrawOrder(uint64_t draw_serial, DWORD active_fvf, bool current_sky_texture_draw) {
  const auto mark_first = [draw_serial](uint64_t* first, uint64_t* count) {
    if (count) *count += 1u;
    if (first && *first == 0u) *first = draw_serial;
  };
  if (current_sky_texture_draw) mark_first(&g_draw_order_first_sky, &g_draw_order_count_sky);
  if (ActiveFvfIsSkin(active_fvf)) mark_first(&g_draw_order_first_skin, &g_draw_order_count_skin);
  if (active_fvf == 594u) mark_first(&g_draw_order_first_terrain594, &g_draw_order_count_terrain594);
  if (active_fvf == 578u) mark_first(&g_draw_order_first_water578, &g_draw_order_count_water578);
  if (active_fvf == 530u) mark_first(&g_draw_order_first_fvf530, &g_draw_order_count_fvf530);
  if (active_fvf == 322u) mark_first(&g_draw_order_first_fvf322, &g_draw_order_count_fvf322);
  if (current_sky_texture_draw) mark_first(&g_draw_order_frame_first_sky, &g_draw_order_frame_count_sky);
  if (ActiveFvfIsSkin(active_fvf)) mark_first(&g_draw_order_frame_first_skin, &g_draw_order_frame_count_skin);
  if (active_fvf == 594u) mark_first(&g_draw_order_frame_first_terrain594, &g_draw_order_frame_count_terrain594);
  if (active_fvf == 578u) mark_first(&g_draw_order_frame_first_water578, &g_draw_order_frame_count_water578);
  if (active_fvf == 530u) mark_first(&g_draw_order_frame_first_fvf530, &g_draw_order_frame_count_fvf530);
  if (active_fvf == 322u) mark_first(&g_draw_order_frame_first_fvf322, &g_draw_order_frame_count_fvf322);
}

void TrackFvf322ClassDraw(DWORD active_fvf) {
  if (active_fvf != 322u) return;
  const uint32_t class_id = ClassifyFvf322Draw();
  if (class_id < g_fvf322_class_draws.size()) g_fvf322_class_draws[class_id] += 1u;
}

bool IsSuspiciousSkinTexturePath(const std::string& path) {
  const std::string lower = ToLowerCopy(path);
  return lower.find("terrain") != std::string::npos ||
         lower.find("water") != std::string::npos ||
         lower.find("sky") != std::string::npos ||
         lower.find("env/") != std::string::npos ||
         lower.find("effect/bright") != std::string::npos ||
         lower.find("effect/rain") != std::string::npos ||
         lower.find("effect/pattern") != std::string::npos ||
         lower.find("effect/shadow") != std::string::npos ||
         lower.find("effect/lightmap") != std::string::npos;
}

void TrackSkinSuspiciousTexture(DWORD active_fvf, uint64_t draw_serial) {
  if (!ActiveFvfIsSkin(active_fvf)) return;
  const std::string stage0_path = CurrentDrawTraceTexturePath(0);
  const std::string stage1_path = CurrentDrawTraceTexturePath(1);
  if (!IsSuspiciousSkinTexturePath(stage0_path) && !IsSuspiciousSkinTexturePath(stage1_path)) return;
  g_skin_suspicious_texture_draws += 1u;

  char key[1024]{};
  std::snprintf(
      key,
      sizeof(key),
      "fvf=%u|s0=%s|s1=%s|s0c=%u|s1c=%u",
      static_cast<unsigned int>(active_fvf),
      stage0_path.empty() ? "(none)" : stage0_path.c_str(),
      stage1_path.empty() ? "(none)" : stage1_path.c_str(),
      static_cast<unsigned int>(StageStateValue(0, D3DTSS_COLOROP, D3DTOP_MODULATE)),
      static_cast<unsigned int>(StageStateValue(1, D3DTSS_COLOROP, D3DTOP_DISABLE)));
  if (g_skin_suspicious_texture_samples.size() >= 64u ||
      !g_skin_suspicious_texture_seen.insert(std::string(key)).second) {
    return;
  }

  char sample[1536]{};
  const std::string label = CurrentDrawTraceLabel();
  std::snprintf(
      sample,
      sizeof(sample),
      "draw=%llu fvf=%u stage0=%s stage1=%s s0c=%u s1c=%u label=%s",
      static_cast<unsigned long long>(draw_serial),
      static_cast<unsigned int>(active_fvf),
      stage0_path.empty() ? "(none)" : stage0_path.c_str(),
      stage1_path.empty() ? "(none)" : stage1_path.c_str(),
      static_cast<unsigned int>(StageStateValue(0, D3DTSS_COLOROP, D3DTOP_MODULATE)),
      static_cast<unsigned int>(StageStateValue(1, D3DTSS_COLOROP, D3DTOP_DISABLE)),
      label.empty() ? "(unlabeled)" : label.c_str());
  g_skin_suspicious_texture_samples.emplace_back(sample);
}

void TrackClipWEmptySignature(DWORD active_fvf) {
  const std::string stage0_path = CurrentDrawTraceTexturePath(0);
  const std::string stage1_path = CurrentDrawTraceTexturePath(1);
  char key[1536]{};
  std::snprintf(
      key,
      sizeof(key),
      "fvf=%u group=%s stage0=%s stage1=%s",
      static_cast<unsigned int>(active_fvf),
      FormatDrawTraceGroup(active_fvf).c_str(),
      stage0_path.empty() ? "(none)" : stage0_path.c_str(),
      stage1_path.empty() ? "(none)" : stage1_path.c_str());
  g_clipw_empty_signature_histogram[std::string(key)] += 1u;
}

void BuildClipWEmptySignatureExportCache() {
  std::vector<std::pair<std::string, uint64_t>> sorted(
      g_clipw_empty_signature_histogram.begin(),
      g_clipw_empty_signature_histogram.end());
  std::sort(
      sorted.begin(),
      sorted.end(),
      [](const std::pair<std::string, uint64_t>& a, const std::pair<std::string, uint64_t>& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
      });
  g_clipw_empty_signature_export_cache.clear();
  for (const auto& entry : sorted) {
    if (g_clipw_empty_signature_export_cache.size() >= 64u) break;
    char line[1700]{};
    std::snprintf(
        line,
        sizeof(line),
        "count=%llu %s",
        static_cast<unsigned long long>(entry.second),
        entry.first.c_str());
    g_clipw_empty_signature_export_cache.emplace_back(line);
  }
}

void ResetDrawTraceResults(bool clear_probes) {
  g_draw_trace_serial = 0;
  g_draw_trace_top_samples.clear();
  g_draw_trace_top_export_cache.clear();
  g_draw_trace_label.clear();
  g_draw_trace_scope.clear();
  for (auto& probe : g_draw_trace_probes) {
    if (clear_probes) {
      probe.enabled = false;
      probe.x = 0.0f;
      probe.y = 0.0f;
    }
    probe.depth_valid = false;
    probe.depth = 1.0f;
    probe.draw_serial = 0;
    probe.result.clear();
    probe.hit_count = 0;
    probe.first_hit_draw_serial = 0;
    probe.nearest_hit_draw_serial = 0;
    probe.nearest_zwrite_draw_serial = 0;
    probe.nearest_hit_depth = 1.0f;
    probe.nearest_zwrite_depth = 1.0f;
    probe.first_hit_result.clear();
    probe.nearest_hit_result.clear();
    probe.nearest_zwrite_result.clear();
  }
}

void AddDrawTraceTopSample(double score, uint64_t draw_serial, const std::string& text) {
  if (score <= 0.0 || text.empty()) return;
  DrawTraceTopSample sample{};
  sample.score = score;
  sample.draw_serial = draw_serial;
  sample.text = text;

  if (g_draw_trace_top_samples.size() < kDrawTraceMaxTopSamples) {
    g_draw_trace_top_samples.push_back(sample);
    return;
  }

  auto min_it = std::min_element(
      g_draw_trace_top_samples.begin(),
      g_draw_trace_top_samples.end(),
      [](const DrawTraceTopSample& a, const DrawTraceTopSample& b) {
        return a.score < b.score;
      });
  if (min_it != g_draw_trace_top_samples.end() && score > min_it->score) {
    *min_it = sample;
  }
}

struct DrawTraceScreenVertex {
  float x = 0.0f;
  float y = 0.0f;
  float z = 1.0f;
  bool valid = false;
};

DrawTraceScreenVertex ToDrawTraceScreenVertex(const FFPVertex& vertex, float width, float height) {
  DrawTraceScreenVertex out{};
  const float safe_w = SafeClipW(vertex.w);
  const float ndc_x = vertex.x / safe_w;
  const float ndc_y = vertex.y / safe_w;
  const float ndc_z = vertex.z / safe_w;
  if (!std::isfinite(ndc_x) || !std::isfinite(ndc_y) || !std::isfinite(ndc_z)) return out;
  out.x = (ndc_x * 0.5f + 0.5f) * width;
  out.y = (0.5f - ndc_y * 0.5f) * height;
  out.z = ndc_z * 0.5f + 0.5f;
  out.valid = std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
  return out;
}

bool DrawTracePointInTriangle(
    const DrawTraceScreenVertex& a,
    const DrawTraceScreenVertex& b,
    const DrawTraceScreenVertex& c,
    float px,
    float py,
    float* out_depth,
    float* out_wa = nullptr,
    float* out_wb = nullptr,
    float* out_wc = nullptr) {
  if (!a.valid || !b.valid || !c.valid || !out_depth) return false;
  const float denom =
      (b.y - c.y) * (a.x - c.x) +
      (c.x - b.x) * (a.y - c.y);
  if (std::fabs(denom) < 1.0e-5f) return false;

  const float wa =
      ((b.y - c.y) * (px - c.x) +
       (c.x - b.x) * (py - c.y)) / denom;
  const float wb =
      ((c.y - a.y) * (px - c.x) +
       (a.x - c.x) * (py - c.y)) / denom;
  const float wc = 1.0f - wa - wb;
  constexpr float eps = -1.0e-4f;
  if (wa < eps || wb < eps || wc < eps) return false;
  *out_depth = wa * a.z + wb * b.z + wc * c.z;
  if (*out_depth < -1.0e-4f || *out_depth > 1.0f + 1.0e-4f) return false;
  if (out_wa) *out_wa = wa;
  if (out_wb) *out_wb = wb;
  if (out_wc) *out_wc = wc;
  return std::isfinite(*out_depth);
}

bool DrawTraceComputeBounds(
    const std::vector<FFPVertex>& vertices,
    const std::vector<uint32_t>* indices,
    float width,
    float height,
    float* out_min_x,
    float* out_min_y,
    float* out_max_x,
    float* out_max_y) {
  if (!out_min_x || !out_min_y || !out_max_x || !out_max_y || vertices.empty()) return false;

  bool have = false;
  const auto visit = [&](uint32_t idx) {
    if (idx >= vertices.size()) return;
    const DrawTraceScreenVertex sv = ToDrawTraceScreenVertex(vertices[idx], width, height);
    if (!sv.valid) return;
    if (!have) {
      *out_min_x = *out_max_x = sv.x;
      *out_min_y = *out_max_y = sv.y;
      have = true;
      return;
    }
    *out_min_x = std::min(*out_min_x, sv.x);
    *out_max_x = std::max(*out_max_x, sv.x);
    *out_min_y = std::min(*out_min_y, sv.y);
    *out_max_y = std::max(*out_max_y, sv.y);
  };

  if (indices && !indices->empty()) {
    for (uint32_t idx : *indices) visit(idx);
  } else {
    for (uint32_t i = 0; i < vertices.size(); ++i) visit(i);
  }
  return have;
}

std::string FormatDrawTraceVertexColorStats(
    const std::vector<FFPVertex>& vertices,
    const std::vector<uint32_t>* indices) {
  if (vertices.empty()) return {};

  uint64_t count = 0;
  uint64_t sum_r = 0;
  uint64_t sum_g = 0;
  uint64_t sum_b = 0;
  uint64_t sum_a = 0;
  uint32_t min_r = 255;
  uint32_t min_g = 255;
  uint32_t min_b = 255;
  uint32_t min_a = 255;
  uint32_t max_r = 0;
  uint32_t max_g = 0;
  uint32_t max_b = 0;
  uint32_t max_a = 0;
  uint64_t grayish = 0;
  uint64_t colored = 0;
  uint64_t alpha_zero = 0;

  const auto visit = [&](uint32_t idx) {
    if (idx >= vertices.size()) return;
    const FFPVertex& v = vertices[idx];
    const uint32_t r = v.r;
    const uint32_t g = v.g;
    const uint32_t b = v.b;
    const uint32_t a = v.a;
    const uint32_t max_rgb = std::max(r, std::max(g, b));
    const uint32_t min_rgb = std::min(r, std::min(g, b));
    const uint32_t luma = (r + g + b) / 3u;
    sum_r += r;
    sum_g += g;
    sum_b += b;
    sum_a += a;
    min_r = std::min(min_r, r);
    min_g = std::min(min_g, g);
    min_b = std::min(min_b, b);
    min_a = std::min(min_a, a);
    max_r = std::max(max_r, r);
    max_g = std::max(max_g, g);
    max_b = std::max(max_b, b);
    max_a = std::max(max_a, a);
    if (max_rgb - min_rgb <= 12u && luma >= 25u && luma <= 210u) grayish += 1u;
    if (max_rgb - min_rgb > 35u && luma >= 25u) colored += 1u;
    if (a == 0u) alpha_zero += 1u;
    count += 1u;
  };

  if (indices && !indices->empty()) {
    for (uint32_t idx : *indices) visit(idx);
  } else {
    for (uint32_t i = 0; i < vertices.size(); ++i) visit(i);
  }

  if (count == 0u) return {};
  char buf[320]{};
  std::snprintf(
      buf,
      sizeof(buf),
      " vcol=n%llu avg=%u,%u,%u,%u min=%u,%u,%u,%u max=%u,%u,%u,%u gray=%.1f colored=%.1f a0=%.1f",
      static_cast<unsigned long long>(count),
      static_cast<unsigned int>(sum_r / count),
      static_cast<unsigned int>(sum_g / count),
      static_cast<unsigned int>(sum_b / count),
      static_cast<unsigned int>(sum_a / count),
      static_cast<unsigned int>(min_r),
      static_cast<unsigned int>(min_g),
      static_cast<unsigned int>(min_b),
      static_cast<unsigned int>(min_a),
      static_cast<unsigned int>(max_r),
      static_cast<unsigned int>(max_g),
      static_cast<unsigned int>(max_b),
      static_cast<unsigned int>(max_a),
      (static_cast<double>(grayish) * 100.0) / static_cast<double>(count),
      (static_cast<double>(colored) * 100.0) / static_cast<double>(count),
      (static_cast<double>(alpha_zero) * 100.0) / static_cast<double>(count));
  return std::string(buf);
}

std::string FormatDrawTraceClipStats(
    DWORD active_fvf,
    const std::vector<FFPVertex>& vertices,
    const std::vector<uint32_t>* indices) {
  const bool skinned_fvf = (active_fvf >= 4370u && active_fvf <= 4388u);
  const bool terrain_or_water_fvf = (active_fvf == 594u || active_fvf == 578u);
  if ((!skinned_fvf && !terrain_or_water_fvf) || vertices.empty()) return {};

  uint64_t count = 0;
  uint64_t positive_w = 0;
  uint64_t negative_w = 0;
  uint64_t near_w = 0;
  uint64_t inside = 0;
  uint64_t large_ndc = 0;
  float min_w = std::numeric_limits<float>::infinity();
  float max_w = -std::numeric_limits<float>::infinity();
  float min_ndc_x = std::numeric_limits<float>::infinity();
  float max_ndc_x = -std::numeric_limits<float>::infinity();
  float min_ndc_y = std::numeric_limits<float>::infinity();
  float max_ndc_y = -std::numeric_limits<float>::infinity();
  float min_ndc_z = std::numeric_limits<float>::infinity();
  float max_ndc_z = -std::numeric_limits<float>::infinity();

  const auto visit = [&](uint32_t idx) {
    if (idx >= vertices.size()) return;
    const FFPVertex& v = vertices[idx];
    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) || !std::isfinite(v.w)) return;
    count += 1u;
    min_w = std::min(min_w, v.w);
    max_w = std::max(max_w, v.w);
    if (v.w > kEpsilonW) positive_w += 1u;
    else if (v.w < -kEpsilonW) negative_w += 1u;
    else near_w += 1u;

    const float safe_w = SafeClipW(v.w);
    const float ndc_x = v.x / safe_w;
    const float ndc_y = v.y / safe_w;
    const float ndc_z = v.z / safe_w;
    if (!std::isfinite(ndc_x) || !std::isfinite(ndc_y) || !std::isfinite(ndc_z)) return;
    min_ndc_x = std::min(min_ndc_x, ndc_x);
    max_ndc_x = std::max(max_ndc_x, ndc_x);
    min_ndc_y = std::min(min_ndc_y, ndc_y);
    max_ndc_y = std::max(max_ndc_y, ndc_y);
    min_ndc_z = std::min(min_ndc_z, ndc_z);
    max_ndc_z = std::max(max_ndc_z, ndc_z);
    if (std::fabs(ndc_x) <= 1.25f &&
        std::fabs(ndc_y) <= 1.25f &&
        ndc_z >= -1.25f &&
        ndc_z <= 1.25f &&
        v.w > kEpsilonW) {
      inside += 1u;
    }
    if (std::fabs(ndc_x) > 8.0f || std::fabs(ndc_y) > 8.0f || std::fabs(ndc_z) > 8.0f) {
      large_ndc += 1u;
    }
  };

  if (indices && !indices->empty()) {
    for (uint32_t idx : *indices) visit(idx);
  } else {
    for (uint32_t i = 0; i < vertices.size(); ++i) visit(i);
  }

  if (count == 0u) return {};
  char buf[384]{};
  std::snprintf(
      buf,
      sizeof(buf),
      " %s=n%llu w+%llu w-%llu w0%llu inside%llu large%llu w%.3f..%.3f ndcX%.2f..%.2f ndcY%.2f..%.2f ndcZ%.2f..%.2f",
      skinned_fvf ? "skinclip" : "worldclip",
      static_cast<unsigned long long>(count),
      static_cast<unsigned long long>(positive_w),
      static_cast<unsigned long long>(negative_w),
      static_cast<unsigned long long>(near_w),
      static_cast<unsigned long long>(inside),
      static_cast<unsigned long long>(large_ndc),
      min_w,
      max_w,
      min_ndc_x,
      max_ndc_x,
      min_ndc_y,
      max_ndc_y,
      min_ndc_z,
      max_ndc_z);
  return std::string(buf);
}

std::string FormatDrawTraceTextureStats(size_t stage, const char* prefix) {
  if (stage >= g_ffp_state.textures.size()) return {};
  auto* tex = AsTexture(g_ffp_state.textures[stage]);
  if (!tex || tex->pixels.empty() || tex->width == 0u || tex->height == 0u || tex->bytes_per_pixel <= 0) {
    return {};
  }

  const uint64_t total_pixels = static_cast<uint64_t>(tex->width) * static_cast<uint64_t>(tex->height);
  const uint64_t step = std::max<uint64_t>(1u, total_pixels / 4096u);
  uint64_t count = 0;
  uint64_t sum_r = 0;
  uint64_t sum_g = 0;
  uint64_t sum_b = 0;
  uint64_t sum_a = 0;
  uint64_t grayish = 0;
  uint64_t colored = 0;
  uint64_t alpha_zero = 0;

  for (uint64_t i = 0; i < total_pixels; i += step) {
    const UINT x = static_cast<UINT>(i % tex->width);
    const UINT y = static_cast<UINT>(i / tex->width);
    const uint8_t* row = tex->pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(tex->pitch);
    const uint8_t* src = row + static_cast<size_t>(x) * static_cast<size_t>(tex->bytes_per_pixel);
    uint8_t rgba[4]{};
    TexturePixelToRGBA(tex, src, rgba);

    const uint32_t r = rgba[0];
    const uint32_t g = rgba[1];
    const uint32_t b = rgba[2];
    const uint32_t a = rgba[3];
    const uint32_t max_rgb = std::max(r, std::max(g, b));
    const uint32_t min_rgb = std::min(r, std::min(g, b));
    const uint32_t luma = (r + g + b) / 3u;
    sum_r += r;
    sum_g += g;
    sum_b += b;
    sum_a += a;
    if (max_rgb - min_rgb <= 12u && luma >= 25u && luma <= 210u) grayish += 1u;
    if (max_rgb - min_rgb > 35u && luma >= 25u) colored += 1u;
    if (a == 0u) alpha_zero += 1u;
    count += 1u;
  }

  if (count == 0u) return {};
  char buf[256]{};
  std::snprintf(
      buf,
      sizeof(buf),
      " %s=%ux%u/f%u avg=%u,%u,%u,%u gray=%.1f colored=%.1f a0=%.1f",
      prefix ? prefix : "tex",
      static_cast<unsigned int>(tex->width),
      static_cast<unsigned int>(tex->height),
      static_cast<unsigned int>(tex->format),
      static_cast<unsigned int>(sum_r / count),
      static_cast<unsigned int>(sum_g / count),
      static_cast<unsigned int>(sum_b / count),
      static_cast<unsigned int>(sum_a / count),
      (static_cast<double>(grayish) * 100.0) / static_cast<double>(count),
      (static_cast<double>(colored) * 100.0) / static_cast<double>(count),
      (static_cast<double>(alpha_zero) * 100.0) / static_cast<double>(count));
  return std::string(buf);
}

uint8_t ClampTraceByte(float value) {
  const float clamped = std::max(0.0f, std::min(255.0f, value));
  return static_cast<uint8_t>(clamped + 0.5f);
}

float ResolveTraceAddress(float value, DWORD address_mode) {
  if (address_mode == 1u) {
    float wrapped = value - std::floor(value);
    if (wrapped < 0.0f) wrapped += 1.0f;
    return wrapped;
  }
  return std::max(0.0f, std::min(1.0f, value));
}

bool SampleTraceTexture(size_t stage, float u, float v, uint8_t out_rgba[4]) {
  if (!out_rgba || stage >= g_ffp_state.textures.size()) return false;
  auto* tex = AsTexture(g_ffp_state.textures[stage]);
  if (!tex || tex->pixels.empty() || tex->width == 0u || tex->height == 0u || tex->bytes_per_pixel <= 0) return false;

  const DWORD addr_u = g_ffp_state.sampler[stage][D3DSAMP_ADDRESSU];
  const DWORD addr_v = g_ffp_state.sampler[stage][D3DSAMP_ADDRESSV];
  const float su = ResolveTraceAddress(u, addr_u);
  const float sv = ResolveTraceAddress(v, addr_v);
  const UINT x = std::min<UINT>(tex->width - 1u, static_cast<UINT>(su * static_cast<float>(tex->width)));
  const UINT y = std::min<UINT>(tex->height - 1u, static_cast<UINT>(sv * static_cast<float>(tex->height)));
  const uint8_t* row = tex->pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(tex->pitch);
  const uint8_t* src = row + static_cast<size_t>(x) * static_cast<size_t>(tex->bytes_per_pixel);
  TexturePixelToRGBA(tex, src, out_rgba);
  return true;
}

std::string FormatDrawTraceProbeSample(
    const FFPVertex& a,
    const FFPVertex& b,
    const FFPVertex& c,
    float wa,
    float wb,
    float wc) {
  const auto interp = [&](float av, float bv, float cv) {
    return wa * av + wb * bv + wc * cv;
  };
  const float u0 = interp(a.u0, b.u0, c.u0);
  const float v0 = interp(a.v0, b.v0, c.v0);
  const float u1 = interp(a.u1, b.u1, c.u1);
  const float v1 = interp(a.v1, b.v1, c.v1);
  const uint8_t diffuse[4] = {
      ClampTraceByte(interp(static_cast<float>(a.r), static_cast<float>(b.r), static_cast<float>(c.r))),
      ClampTraceByte(interp(static_cast<float>(a.g), static_cast<float>(b.g), static_cast<float>(c.g))),
      ClampTraceByte(interp(static_cast<float>(a.b), static_cast<float>(b.b), static_cast<float>(c.b))),
      ClampTraceByte(interp(static_cast<float>(a.a), static_cast<float>(b.a), static_cast<float>(c.a))),
  };
  uint8_t tex0[4]{255, 255, 255, 255};
  uint8_t tex1[4]{255, 255, 255, 255};
  const bool has0 = SampleTraceTexture(0, u0, v0, tex0);
  const bool has1 = SampleTraceTexture(1, u1, v1, tex1);

  char buf[384]{};
  std::snprintf(
      buf,
      sizeof(buf),
      " pix=uv0=%.3f,%.3f t0=%s:%u,%u,%u,%u uv1=%.3f,%.3f t1=%s:%u,%u,%u,%u dif=%u,%u,%u,%u",
      u0,
      v0,
      has0 ? "hit" : "none",
      static_cast<unsigned int>(tex0[0]),
      static_cast<unsigned int>(tex0[1]),
      static_cast<unsigned int>(tex0[2]),
      static_cast<unsigned int>(tex0[3]),
      u1,
      v1,
      has1 ? "hit" : "none",
      static_cast<unsigned int>(tex1[0]),
      static_cast<unsigned int>(tex1[1]),
      static_cast<unsigned int>(tex1[2]),
      static_cast<unsigned int>(tex1[3]),
      static_cast<unsigned int>(diffuse[0]),
      static_cast<unsigned int>(diffuse[1]),
      static_cast<unsigned int>(diffuse[2]),
      static_cast<unsigned int>(diffuse[3]));
  return std::string(buf);
}

std::string FormatDrawTraceProbeHitEntry(const std::string& entry, float depth) {
  char prefix[96]{};
  std::snprintf(prefix, sizeof(prefix), "probeDepth=%.5f ", depth);
  return std::string(prefix) + entry;
}

void DrawTraceRecordProbeHit(
    DrawTraceProbe* probe,
    float depth,
    bool depth_write_enabled,
    uint64_t draw_serial,
    const std::string& entry) {
  if (!probe || !probe->enabled) return;
  if (!std::isfinite(depth)) return;

  const std::string hit_entry = FormatDrawTraceProbeHitEntry(entry, depth);
  probe->hit_count += 1u;

  if (probe->first_hit_draw_serial == 0u) {
    probe->first_hit_draw_serial = draw_serial;
    probe->first_hit_result = hit_entry;
  }

  if (probe->nearest_hit_draw_serial == 0u || depth < probe->nearest_hit_depth) {
    probe->nearest_hit_draw_serial = draw_serial;
    probe->nearest_hit_depth = depth;
    probe->nearest_hit_result = hit_entry;
  }

  if (depth_write_enabled &&
      (probe->nearest_zwrite_draw_serial == 0u || depth < probe->nearest_zwrite_depth)) {
    probe->nearest_zwrite_draw_serial = draw_serial;
    probe->nearest_zwrite_depth = depth;
    probe->nearest_zwrite_result = hit_entry;
  }
}

std::string FormatDrawTraceEntry(
    uint64_t draw_serial,
    DWORD active_fvf,
    bool depth_test_enabled,
    bool depth_write_enabled,
    bool blend_enabled,
    bool alpha_test_enabled,
    bool skipped,
    const char* skip_reason,
    float min_x,
    float min_y,
    float max_x,
    float max_y,
    const std::string& clip_stats,
    const std::string& vertex_color_stats,
    double coverage) {
  const std::string stage0_path = CurrentDrawTraceTexturePath(0);
  const std::string stage1_path = CurrentDrawTraceTexturePath(1);
  const std::string label = CurrentDrawTraceLabel();
  const std::string group = FormatDrawTraceGroup(active_fvf);
  const std::string texture_stats =
      FormatDrawTraceTextureStats(0, "tex0") + FormatDrawTraceTextureStats(1, "tex1");
  const DWORD s0_color_op = StageStateValue(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
  const DWORD s0_color_arg1 = StageStateValue(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
  const DWORD s0_color_arg2 = StageStateValue(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
  const DWORD s0_alpha_op = StageStateValue(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
  const DWORD s0_alpha_arg1 = StageStateValue(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
  const DWORD s0_alpha_arg2 = StageStateValue(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
  const DWORD s1_color_op = StageStateValue(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
  const DWORD s1_color_arg1 = StageStateValue(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
  const DWORD s1_color_arg2 = StageStateValue(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
  const DWORD s1_alpha_op = StageStateValue(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
  const DWORD s1_alpha_arg1 = StageStateValue(1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
  const DWORD s1_alpha_arg2 = StageStateValue(1, D3DTSS_ALPHAARG2, D3DTA_CURRENT);
  char fvf530_stats[384]{};
  if (active_fvf == 530u) {
    std::snprintf(
        fvf530_stats,
        sizeof(fvf530_stats),
        " fvf530=v%u/i%u stableW%u inside%u large%u ndcX%.2f..%.2f ndcY%.2f..%.2f ndcZ%.2f..%.2f",
        g_fvf530_last_vertex_count,
        g_fvf530_last_index_count,
        g_fvf530_last_stable_w_vertices,
        g_fvf530_last_inside_vertices,
        g_fvf530_last_large_ndc_vertices,
        g_fvf530_last_min_ndc_x,
        g_fvf530_last_max_ndc_x,
        g_fvf530_last_min_ndc_y,
        g_fvf530_last_max_ndc_y,
        g_fvf530_last_min_ndc_z,
        g_fvf530_last_max_ndc_z);
  }
  char buf[4096]{};
  std::snprintf(
      buf,
      sizeof(buf),
      "draw=%llu fvf=%u group=%s coverage=%.5f bbox=%.1f,%.1f..%.1f,%.1f depthTest=%u zwrite=%u zFunc=%u blend=%u alphaTest=%u skipped=%u reason=%s s0c=%u/%u/%u s0a=%u/%u/%u s1c=%u/%u/%u s1a=%u/%u/%u stage0=%s stage1=%s%s%s%s%s label=%s",
      static_cast<unsigned long long>(draw_serial),
      static_cast<unsigned int>(active_fvf),
      group.c_str(),
      coverage,
      min_x,
      min_y,
      max_x,
      max_y,
      depth_test_enabled ? 1u : 0u,
      depth_write_enabled ? 1u : 0u,
      static_cast<unsigned int>(g_wasm_d3d9_state.z_func),
      blend_enabled ? 1u : 0u,
      alpha_test_enabled ? 1u : 0u,
      skipped ? 1u : 0u,
      skip_reason && *skip_reason ? skip_reason : "-",
      static_cast<unsigned int>(s0_color_op),
      static_cast<unsigned int>(s0_color_arg1),
      static_cast<unsigned int>(s0_color_arg2),
      static_cast<unsigned int>(s0_alpha_op),
      static_cast<unsigned int>(s0_alpha_arg1),
      static_cast<unsigned int>(s0_alpha_arg2),
      static_cast<unsigned int>(s1_color_op),
      static_cast<unsigned int>(s1_color_arg1),
      static_cast<unsigned int>(s1_color_arg2),
      static_cast<unsigned int>(s1_alpha_op),
      static_cast<unsigned int>(s1_alpha_arg1),
      static_cast<unsigned int>(s1_alpha_arg2),
      stage0_path.empty() ? "(none)" : stage0_path.c_str(),
      stage1_path.empty() ? "(none)" : stage1_path.c_str(),
      fvf530_stats,
      clip_stats.c_str(),
      vertex_color_stats.c_str(),
      texture_stats.c_str(),
      label.empty() ? "(unlabeled)" : label.c_str());
  return std::string(buf);
}

void DrawTraceUpdateProbe(
    DrawTraceProbe* probe,
    float depth,
    bool depth_test_enabled,
    bool depth_write_enabled,
    uint64_t draw_serial,
    const std::string& entry) {
  if (!probe || !probe->enabled) return;
  bool visible = true;
  if (depth_test_enabled) {
    const float current_depth = probe->depth_valid ? probe->depth : 1.0f;
    visible = depth <= current_depth + 1.0e-4f;
  }
  if (!visible) return;

  probe->draw_serial = draw_serial;
  probe->result = entry;
  if (depth_test_enabled && depth_write_enabled) {
    probe->depth = depth;
    probe->depth_valid = true;
  }
}

void DrawTraceVisitTriangle(
    const DrawTraceScreenVertex& a,
    const DrawTraceScreenVertex& b,
    const DrawTraceScreenVertex& c,
    const FFPVertex* va,
    const FFPVertex* vb,
    const FFPVertex* vc,
    bool depth_test_enabled,
    bool depth_write_enabled,
    uint64_t draw_serial,
    const std::string& entry) {
  for (auto& probe : g_draw_trace_probes) {
    if (!probe.enabled) continue;
    float depth = 1.0f;
    float wa = 0.0f;
    float wb = 0.0f;
    float wc = 0.0f;
    if (DrawTracePointInTriangle(a, b, c, probe.x, probe.y, &depth, &wa, &wb, &wc)) {
      std::string probe_entry = entry;
      if (va && vb && vc) {
        probe_entry += FormatDrawTraceProbeSample(*va, *vb, *vc, wa, wb, wc);
      }
      DrawTraceRecordProbeHit(&probe, depth, depth_write_enabled, draw_serial, probe_entry);
      DrawTraceUpdateProbe(&probe, depth, depth_test_enabled, depth_write_enabled, draw_serial, probe_entry);
    }
  }
}

void RecordDrawTrace(
    GLenum draw_mode,
    const std::vector<FFPVertex>& vertices,
    const std::vector<uint32_t>* indices,
    DWORD active_fvf,
    bool depth_test_enabled,
    bool depth_write_enabled,
    bool blend_enabled,
    bool alpha_test_enabled,
    bool skipped,
    const char* skip_reason) {
  if (!DrawTraceIsEnabled()) return;
  if (vertices.empty()) return;

  const uint64_t draw_serial = ++g_draw_trace_serial;
  const float width = static_cast<float>(std::max<DWORD>(1, g_wasm_d3d9_state.viewport.Width));
  const float height = static_cast<float>(std::max<DWORD>(1, g_wasm_d3d9_state.viewport.Height));

  float min_x = 0.0f;
  float min_y = 0.0f;
  float max_x = 0.0f;
  float max_y = 0.0f;
  const bool have_bounds = DrawTraceComputeBounds(vertices, indices, width, height, &min_x, &min_y, &max_x, &max_y);
  double coverage = 0.0;
  if (have_bounds) {
    const float clipped_min_x = std::max(0.0f, std::min(width, min_x));
    const float clipped_max_x = std::max(0.0f, std::min(width, max_x));
    const float clipped_min_y = std::max(0.0f, std::min(height, min_y));
    const float clipped_max_y = std::max(0.0f, std::min(height, max_y));
    const float clipped_w = std::max(0.0f, clipped_max_x - clipped_min_x);
    const float clipped_h = std::max(0.0f, clipped_max_y - clipped_min_y);
    coverage = static_cast<double>(clipped_w) * static_cast<double>(clipped_h) /
        std::max(1.0, static_cast<double>(width) * static_cast<double>(height));
  }

  const std::string entry = FormatDrawTraceEntry(
      draw_serial,
      active_fvf,
      depth_test_enabled,
      depth_write_enabled,
      blend_enabled,
      alpha_test_enabled,
      skipped,
      skip_reason,
      min_x,
      min_y,
      max_x,
      max_y,
      FormatDrawTraceClipStats(active_fvf, vertices, indices),
      FormatDrawTraceVertexColorStats(vertices, indices),
      coverage);

  double score = coverage;
  const std::string stage0_path = CurrentDrawTraceTexturePath(0);
  if (active_fvf == 530u) score *= 3.0;
  if (active_fvf >= 4370u && active_fvf <= 4388u) {
    score *= 4.0;
    if (depth_write_enabled) score += 0.5;
  }
  if (stage0_path.find("mesh/hs0037") != std::string::npos ||
      stage0_path.find("mesh/hs0045") != std::string::npos ||
      stage0_path.find("mesh/hs0054") != std::string::npos) {
    score += 0.25;
  }
  if (skipped) score += 0.5;
  if (coverage >= 0.005 || active_fvf == 530u || (active_fvf >= 4370u && active_fvf <= 4388u) || skipped) {
    AddDrawTraceTopSample(score, draw_serial, entry);
  }

  if (skipped || draw_mode == GL_LINES || draw_mode == GL_POINTS) {
    g_draw_trace_label.clear();
    return;
  }

  std::vector<DrawTraceScreenVertex> screen_vertices;
  screen_vertices.reserve(vertices.size());
  for (const FFPVertex& vertex : vertices) {
    screen_vertices.push_back(ToDrawTraceScreenVertex(vertex, width, height));
  }

  const auto vertex_at = [&](uint32_t index) -> DrawTraceScreenVertex {
    if (index >= screen_vertices.size()) return DrawTraceScreenVertex{};
    return screen_vertices[index];
  };
  const auto ffp_at = [&](uint32_t index) -> const FFPVertex* {
    if (index >= vertices.size()) return nullptr;
    return &vertices[index];
  };
  const auto index_at = [&](size_t position) -> uint32_t {
    if (indices && position < indices->size()) return (*indices)[position];
    return static_cast<uint32_t>(position);
  };
  const size_t count = indices && !indices->empty() ? indices->size() : vertices.size();

  if (draw_mode == GL_TRIANGLES) {
    for (size_t i = 0; i + 2u < count; i += 3u) {
      const uint32_t ia = index_at(i);
      const uint32_t ib = index_at(i + 1u);
      const uint32_t ic = index_at(i + 2u);
      DrawTraceVisitTriangle(
          vertex_at(ia),
          vertex_at(ib),
          vertex_at(ic),
          ffp_at(ia),
          ffp_at(ib),
          ffp_at(ic),
          depth_test_enabled,
          depth_write_enabled,
          draw_serial,
          entry);
    }
  } else if (draw_mode == GL_TRIANGLE_STRIP) {
    for (size_t i = 0; i + 2u < count; ++i) {
      const uint32_t ia = index_at(i);
      const uint32_t ib = index_at(i + 1u);
      const uint32_t ic = index_at(i + 2u);
      const DrawTraceScreenVertex a = vertex_at(ia);
      const DrawTraceScreenVertex b = vertex_at(ib);
      const DrawTraceScreenVertex c = vertex_at(ic);
      if ((i & 1u) == 0u) {
        DrawTraceVisitTriangle(a, b, c, ffp_at(ia), ffp_at(ib), ffp_at(ic), depth_test_enabled, depth_write_enabled, draw_serial, entry);
      } else {
        DrawTraceVisitTriangle(b, a, c, ffp_at(ib), ffp_at(ia), ffp_at(ic), depth_test_enabled, depth_write_enabled, draw_serial, entry);
      }
    }
  } else if (draw_mode == GL_TRIANGLE_FAN && count >= 3u) {
    const uint32_t ibase = index_at(0u);
    const DrawTraceScreenVertex base = vertex_at(ibase);
    for (size_t i = 1u; i + 1u < count; ++i) {
      const uint32_t ib = index_at(i);
      const uint32_t ic = index_at(i + 1u);
      DrawTraceVisitTriangle(
          base,
          vertex_at(ib),
          vertex_at(ic),
          ffp_at(ibase),
          ffp_at(ib),
          ffp_at(ic),
          depth_test_enabled,
          depth_write_enabled,
          draw_serial,
          entry);
    }
  }
  g_draw_trace_label.clear();
}

void BuildDrawTraceTopExportCache() {
  std::vector<DrawTraceTopSample> sorted = g_draw_trace_top_samples;
  std::sort(
      sorted.begin(),
      sorted.end(),
      [](const DrawTraceTopSample& a, const DrawTraceTopSample& b) {
        if (a.score == b.score) return a.draw_serial < b.draw_serial;
        return a.score > b.score;
      });
  g_draw_trace_top_export_cache.clear();
  g_draw_trace_top_export_cache.reserve(sorted.size());
  for (const DrawTraceTopSample& sample : sorted) {
    char prefix[96]{};
    std::snprintf(
        prefix,
        sizeof(prefix),
        "score=%.5f rankDraw=%llu ",
        sample.score,
        static_cast<unsigned long long>(sample.draw_serial));
    g_draw_trace_top_export_cache.push_back(std::string(prefix) + sample.text);
  }
}

constexpr BYTE kDeclTypeFloat1 = 0u;
constexpr BYTE kDeclTypeFloat2 = 1u;
constexpr BYTE kDeclTypeFloat3 = 2u;
constexpr BYTE kDeclTypeFloat4 = 3u;
constexpr BYTE kDeclTypeD3DColor = 4u;
constexpr BYTE kDeclTypeUByte4 = 5u;
constexpr BYTE kDeclTypeUnused = 17u;

constexpr BYTE kDeclUsagePosition = 0u;
constexpr BYTE kDeclUsageBlendWeight = 1u;
constexpr BYTE kDeclUsageBlendIndices = 2u;
constexpr BYTE kDeclUsageNormal = 3u;
constexpr BYTE kDeclUsageTexCoord = 5u;
constexpr BYTE kDeclUsageColor = 10u;

struct DeclDecodedVertex {
  D3DXVECTOR3 position{0.0f, 0.0f, 0.0f};
  D3DXVECTOR3 normal{0.0f, 0.0f, 1.0f};
  float tex0_u = 0.0f;
  float tex0_v = 0.0f;
  float tex1_u = 0.0f;
  float tex1_v = 0.0f;
  DWORD diffuse = 0xFFFFFFFFu;
  std::array<float, 4> weights{1.0f, 0.0f, 0.0f, 0.0f};
  std::array<uint8_t, 4> indices{0u, 0u, 0u, 0u};
  UINT influence_count = 1;
  bool has_position = false;
  bool has_normal = false;
  bool has_tex0 = false;
  bool has_tex1 = false;
  bool has_diffuse = false;
  bool has_blend_indices = false;
};

bool IsDeclEnd(const D3DVERTEXELEMENT9& elem) {
  return elem.Stream == 0xFFu && elem.Type == kDeclTypeUnused;
}

void ARGBToRGBA8(uint32_t color, uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a);

size_t VertexDeclTypeSize(BYTE type) {
  switch (type) {
    case kDeclTypeFloat1: return 4u;
    case kDeclTypeFloat2: return 8u;
    case kDeclTypeFloat3: return 12u;
    case kDeclTypeFloat4: return 16u;
    case kDeclTypeD3DColor: return 4u;
    case kDeclTypeUByte4: return 4u;
    default: return 0u;
  }
}

UINT BlendInfluenceCountFromDeclaration(const DummyDirect3DVertexDeclaration9& decl) {
  UINT influences = 1;
  for (const D3DVERTEXELEMENT9& elem : decl.elements) {
    if (IsDeclEnd(elem)) break;
    if (elem.Usage != kDeclUsageBlendWeight) continue;
    switch (elem.Type) {
      case kDeclTypeFloat1:
        influences = 2;
        break;
      case kDeclTypeFloat2:
        influences = 3;
        break;
      case kDeclTypeFloat3:
        influences = 4;
        break;
      case kDeclTypeFloat4:
        influences = 4;
        break;
      default:
        break;
    }
  }
  return influences;
}

std::array<uint8_t, 4> DecodePackedIndices(DWORD packed, bool rgba_order) {
  const uint8_t a = static_cast<uint8_t>((packed >> 24) & 0xFFu);
  const uint8_t r = static_cast<uint8_t>((packed >> 16) & 0xFFu);
  const uint8_t g = static_cast<uint8_t>((packed >> 8) & 0xFFu);
  const uint8_t b = static_cast<uint8_t>(packed & 0xFFu);
  if (rgba_order) return {r, g, b, a};
  return {b, g, r, a};
}

UINT EstimateSkinPaletteSizeFromConstants() {
  UINT last_non_zero = 0;
  for (UINT bone = 0; bone < 80; ++bone) {
    const size_t base = static_cast<size_t>(9 + bone * 3) * 4u;
    if (base + 11 >= g_ffp_state.vertex_shader_constants.size()) break;
    bool non_zero = false;
    for (size_t i = 0; i < 12; ++i) {
      if (std::fabs(g_ffp_state.vertex_shader_constants[base + i]) > 1.0e-6f) {
        non_zero = true;
        break;
      }
    }
    if (non_zero) last_non_zero = bone + 1;
  }
  return last_non_zero > 0 ? last_non_zero : 1u;
}

bool DeclarationUsesD3DColorBlendIndices(const DummyDirect3DVertexDeclaration9& decl, WORD* out_offset) {
  if (out_offset) *out_offset = 0;
  for (const D3DVERTEXELEMENT9& elem : decl.elements) {
    if (IsDeclEnd(elem)) break;
    if (elem.Stream != 0) continue;
    if (elem.Usage == kDeclUsageBlendIndices && elem.Type == kDeclTypeD3DColor) {
      if (out_offset) *out_offset = elem.Offset;
      return true;
    }
  }
  return false;
}

bool PreferRGBAIndexOrder(
    const DummyDirect3DVertexDeclaration9& decl,
    const uint8_t* src,
    UINT vertex_count,
    UINT stride,
    UINT palette_size) {
  WORD blend_offset = 0;
  (void)src;
  (void)vertex_count;
  (void)stride;
  (void)palette_size;
  if (!DeclarationUsesD3DColorBlendIndices(decl, &blend_offset)) return true;

  // The shipped skinmesh shaders read the D3DCOLOR blend-index register as
  // v2.zyxw before multiplying by 765.01, so the packed byte order is B,G,R,A.
  return false;
}

float ShaderConst(UINT reg, UINT component) {
  const size_t index = static_cast<size_t>(reg) * 4u + component;
  if (index >= g_ffp_state.vertex_shader_constants.size()) return 0.0f;
  return g_ffp_state.vertex_shader_constants[index];
}

D3DXVECTOR3 TransformPositionBySkinMatrix(const D3DXVECTOR3& position, UINT bone_index) {
  const UINT safe_bone = std::min<UINT>(bone_index, 79u);
  const UINT reg = 9u + safe_bone * 3u;
  const float x = position.x;
  const float y = position.y;
  const float z = position.z;
  return D3DXVECTOR3(
      x * ShaderConst(reg + 0, 0) + y * ShaderConst(reg + 0, 1) + z * ShaderConst(reg + 0, 2) + ShaderConst(reg + 0, 3),
      x * ShaderConst(reg + 1, 0) + y * ShaderConst(reg + 1, 1) + z * ShaderConst(reg + 1, 2) + ShaderConst(reg + 1, 3),
      x * ShaderConst(reg + 2, 0) + y * ShaderConst(reg + 2, 1) + z * ShaderConst(reg + 2, 2) + ShaderConst(reg + 2, 3));
}

D3DXVECTOR3 TransformNormalBySkinMatrix(const D3DXVECTOR3& normal, UINT bone_index) {
  const UINT safe_bone = std::min<UINT>(bone_index, 79u);
  const UINT reg = 9u + safe_bone * 3u;
  const float x = normal.x;
  const float y = normal.y;
  const float z = normal.z;
  return D3DXVECTOR3(
      x * ShaderConst(reg + 0, 0) + y * ShaderConst(reg + 0, 1) + z * ShaderConst(reg + 0, 2),
      x * ShaderConst(reg + 1, 0) + y * ShaderConst(reg + 1, 1) + z * ShaderConst(reg + 1, 2),
      x * ShaderConst(reg + 2, 0) + y * ShaderConst(reg + 2, 1) + z * ShaderConst(reg + 2, 2));
}

D3DXVECTOR4 TransformPositionByShaderMatrix4(const D3DXVECTOR3& position, UINT start_reg) {
  const float x = position.x;
  const float y = position.y;
  const float z = position.z;
  return D3DXVECTOR4(
      x * ShaderConst(start_reg + 0, 0) + y * ShaderConst(start_reg + 0, 1) + z * ShaderConst(start_reg + 0, 2) + ShaderConst(start_reg + 0, 3),
      x * ShaderConst(start_reg + 1, 0) + y * ShaderConst(start_reg + 1, 1) + z * ShaderConst(start_reg + 1, 2) + ShaderConst(start_reg + 1, 3),
      x * ShaderConst(start_reg + 2, 0) + y * ShaderConst(start_reg + 2, 1) + z * ShaderConst(start_reg + 2, 2) + ShaderConst(start_reg + 2, 3),
      x * ShaderConst(start_reg + 3, 0) + y * ShaderConst(start_reg + 3, 1) + z * ShaderConst(start_reg + 3, 2) + ShaderConst(start_reg + 3, 3));
}

void EncodeFloatColor(
    float r,
    float g,
    float b,
    float a,
    uint8_t* out_r,
    uint8_t* out_g,
    uint8_t* out_b,
    uint8_t* out_a) {
  const auto clamp8 = [](float v) -> uint8_t {
    const float clamped = std::max(0.0f, std::min(1.0f, v));
    return static_cast<uint8_t>(clamped * 255.0f + 0.5f);
  };
  if (out_r) *out_r = clamp8(r);
  if (out_g) *out_g = clamp8(g);
  if (out_b) *out_b = clamp8(b);
  if (out_a) *out_a = clamp8(a);
}

D3DCOLORVALUE ColorValue(float r, float g, float b, float a) {
  D3DCOLORVALUE out{};
  out.r = r;
  out.g = g;
  out.b = b;
  out.a = a;
  return out;
}

D3DCOLORVALUE ColorValueFromARGB(uint32_t color) {
  return ColorValue(
      static_cast<float>((color >> 16) & 0xFFu) / 255.0f,
      static_cast<float>((color >> 8) & 0xFFu) / 255.0f,
      static_cast<float>(color & 0xFFu) / 255.0f,
      static_cast<float>((color >> 24) & 0xFFu) / 255.0f);
}

D3DCOLORVALUE ColorValueFromVertex(const FFPVertex& vertex) {
  return ColorValue(
      static_cast<float>(vertex.r) / 255.0f,
      static_cast<float>(vertex.g) / 255.0f,
      static_cast<float>(vertex.b) / 255.0f,
      static_cast<float>(vertex.a) / 255.0f);
}

D3DCOLORVALUE SelectMaterialSource(
    DWORD source,
    const D3DCOLORVALUE& material_color,
    bool has_color1,
    const D3DCOLORVALUE& color1) {
  if (g_wasm_d3d9_state.color_vertex && source == D3DMCS_COLOR1 && has_color1) return color1;
  return material_color;
}

D3DXVECTOR3 TransformDirectionToView(const D3DVECTOR& direction) {
  return D3DXVECTOR3(
      direction.x * g_ffp_state.view._11 + direction.y * g_ffp_state.view._21 + direction.z * g_ffp_state.view._31,
      direction.x * g_ffp_state.view._12 + direction.y * g_ffp_state.view._22 + direction.z * g_ffp_state.view._32,
      direction.x * g_ffp_state.view._13 + direction.y * g_ffp_state.view._23 + direction.z * g_ffp_state.view._33);
}

D3DXVECTOR3 TransformPositionToView(const D3DVECTOR& position) {
  return D3DXVECTOR3(
      position.x * g_ffp_state.view._11 + position.y * g_ffp_state.view._21 + position.z * g_ffp_state.view._31 + g_ffp_state.view._41,
      position.x * g_ffp_state.view._12 + position.y * g_ffp_state.view._22 + position.z * g_ffp_state.view._32 + g_ffp_state.view._42,
      position.x * g_ffp_state.view._13 + position.y * g_ffp_state.view._23 + position.z * g_ffp_state.view._33 + g_ffp_state.view._43);
}

void AccumulateLight(
    const D3DLIGHT9& light,
    const D3DXVECTOR3& cam_pos,
    const D3DXVECTOR3& cam_nrm,
    const D3DCOLORVALUE& material_diffuse,
    const D3DCOLORVALUE& material_ambient,
    float* out_r,
    float* out_g,
    float* out_b) {
  if (!out_r || !out_g || !out_b) return;

  *out_r += material_ambient.r * light.Ambient.r;
  *out_g += material_ambient.g * light.Ambient.g;
  *out_b += material_ambient.b * light.Ambient.b;

  D3DXVECTOR3 to_light{};
  float attenuation = 1.0f;

  if (light.Type == D3DLIGHT_DIRECTIONAL) {
    D3DXVECTOR3 view_dir = Normalize3(TransformDirectionToView(light.Direction));
    to_light = Normalize3(D3DXVECTOR3(-view_dir.x, -view_dir.y, -view_dir.z));
  } else {
    D3DXVECTOR3 view_pos = TransformPositionToView(light.Position);
    to_light = D3DXVECTOR3(view_pos.x - cam_pos.x, view_pos.y - cam_pos.y, view_pos.z - cam_pos.z);
    const float distance = std::sqrt(Dot3(to_light, to_light));
    if (light.Range > 0.0f && distance > light.Range) return;
    if (distance > 1.0e-6f) {
      to_light = D3DXVECTOR3(to_light.x / distance, to_light.y / distance, to_light.z / distance);
    } else {
      to_light = D3DXVECTOR3(0.0f, 0.0f, 1.0f);
    }

    const float denom =
        light.Attenuation0 + light.Attenuation1 * distance + light.Attenuation2 * distance * distance;
    if (denom > 1.0e-6f) attenuation = 1.0f / denom;
  }

  const float ndotl = std::max(0.0f, Dot3(Normalize3(cam_nrm), to_light));
  *out_r += material_diffuse.r * light.Diffuse.r * ndotl * attenuation;
  *out_g += material_diffuse.g * light.Diffuse.g * ndotl * attenuation;
  *out_b += material_diffuse.b * light.Diffuse.b * ndotl * attenuation;
}

void ApplyLegacyLightingToVertex(
    const D3DXVECTOR3& cam_pos,
    const D3DXVECTOR3& cam_nrm,
    bool has_color1,
    const D3DCOLORVALUE& color1,
    FFPVertex* out_vertex) {
  if (!out_vertex || !g_wasm_d3d9_state.lighting_enable) return;

  bool has_enabled_light = false;
  for (BOOL enabled : g_wasm_d3d9_state.light_enabled) {
    if (enabled) {
      has_enabled_light = true;
      break;
    }
  }
  if (!has_enabled_light) {
    // Startup render blocks can enable D3DRS_LIGHTING before any D3DLIGHT9 is
    // installed. Keeping the existing vertex color avoids turning the scene
    // black while still honoring real fixed-function lights once they appear.
    return;
  }

  const D3DMATERIAL9& material = g_wasm_d3d9_state.material;
  const D3DCOLORVALUE material_diffuse =
      SelectMaterialSource(g_wasm_d3d9_state.diffuse_material_source, material.Diffuse, has_color1, color1);
  const D3DCOLORVALUE material_ambient =
      SelectMaterialSource(g_wasm_d3d9_state.ambient_material_source, material.Ambient, has_color1, color1);
  const D3DCOLORVALUE material_emissive =
      SelectMaterialSource(g_wasm_d3d9_state.emissive_material_source, material.Emissive, has_color1, color1);
  const D3DCOLORVALUE global_ambient = ColorValueFromARGB(g_wasm_d3d9_state.ambient);

  float r = material_emissive.r + material_ambient.r * global_ambient.r;
  float g = material_emissive.g + material_ambient.g * global_ambient.g;
  float b = material_emissive.b + material_ambient.b * global_ambient.b;

  for (size_t i = 0; i < g_wasm_d3d9_state.lights.size(); ++i) {
    if (!g_wasm_d3d9_state.light_enabled[i]) continue;
    AccumulateLight(g_wasm_d3d9_state.lights[i], cam_pos, cam_nrm, material_diffuse, material_ambient, &r, &g, &b);
  }

  const float alpha = (material_diffuse.a > 0.0f) ? material_diffuse.a : ColorValueFromVertex(*out_vertex).a;
  EncodeFloatColor(r, g, b, alpha, &out_vertex->r, &out_vertex->g, &out_vertex->b, &out_vertex->a);
  g_lighted_vertices += 1;
}

bool DecodeVertexFromDeclaration(
    const uint8_t* src,
    UINT stride,
    const DummyDirect3DVertexDeclaration9& decl,
    UINT palette_size,
    bool rgba_index_order,
    FFPVertex* out_vertex) {
  if (!src || !out_vertex || stride == 0) return false;

  DeclDecodedVertex decoded{};
  decoded.influence_count = BlendInfluenceCountFromDeclaration(decl);

  for (const D3DVERTEXELEMENT9& elem : decl.elements) {
    if (IsDeclEnd(elem)) break;
    if (elem.Stream != 0) continue;

    const size_t elem_size = VertexDeclTypeSize(elem.Type);
    if (elem_size == 0u) continue;
    if (static_cast<size_t>(elem.Offset) + elem_size > stride) return false;
    const uint8_t* field = src + elem.Offset;

    if (elem.Usage == kDeclUsagePosition && elem.Type == kDeclTypeFloat3) {
      decoded.position.x = ReadUnaligned<float>(field + 0);
      decoded.position.y = ReadUnaligned<float>(field + 4);
      decoded.position.z = ReadUnaligned<float>(field + 8);
      decoded.has_position = true;
      continue;
    }

    if (elem.Usage == kDeclUsageBlendWeight) {
      switch (elem.Type) {
        case kDeclTypeFloat1:
          decoded.weights[0] = ReadUnaligned<float>(field + 0);
          decoded.influence_count = 2;
          break;
        case kDeclTypeFloat2:
          decoded.weights[0] = ReadUnaligned<float>(field + 0);
          decoded.weights[1] = ReadUnaligned<float>(field + 4);
          decoded.influence_count = 3;
          break;
        case kDeclTypeFloat3:
          decoded.weights[0] = ReadUnaligned<float>(field + 0);
          decoded.weights[1] = ReadUnaligned<float>(field + 4);
          decoded.weights[2] = ReadUnaligned<float>(field + 8);
          decoded.influence_count = 4;
          break;
        case kDeclTypeFloat4:
          decoded.weights[0] = ReadUnaligned<float>(field + 0);
          decoded.weights[1] = ReadUnaligned<float>(field + 4);
          decoded.weights[2] = ReadUnaligned<float>(field + 8);
          decoded.weights[3] = ReadUnaligned<float>(field + 12);
          decoded.influence_count = 4;
          break;
        default:
          break;
      }
      continue;
    }

    if (elem.Usage == kDeclUsageBlendIndices) {
      DWORD packed = 0u;
      if (elem.Type == kDeclTypeD3DColor || elem.Type == kDeclTypeUByte4) {
        packed = ReadUnaligned<DWORD>(field);
        decoded.indices = DecodePackedIndices(packed, rgba_index_order);
        decoded.has_blend_indices = true;
      }
      continue;
    }

    if (elem.Usage == kDeclUsageNormal && elem.Type == kDeclTypeFloat3) {
      decoded.normal.x = ReadUnaligned<float>(field + 0);
      decoded.normal.y = ReadUnaligned<float>(field + 4);
      decoded.normal.z = ReadUnaligned<float>(field + 8);
      decoded.has_normal = true;
      continue;
    }

    if (elem.Usage == kDeclUsageTexCoord) {
      const float tu = ReadUnaligned<float>(field + 0);
      const float tv = (elem_size >= 8u) ? ReadUnaligned<float>(field + 4) : 0.0f;
      if (elem.UsageIndex == 0) {
        decoded.tex0_u = tu;
        decoded.tex0_v = tv;
        decoded.has_tex0 = true;
      } else if (elem.UsageIndex == 1) {
        decoded.tex1_u = tu;
        decoded.tex1_v = tv;
        decoded.has_tex1 = true;
      }
      continue;
    }

    if (elem.Usage == kDeclUsageColor && elem.Type == kDeclTypeD3DColor) {
      decoded.diffuse = ReadUnaligned<DWORD>(field);
      decoded.has_diffuse = true;
      continue;
    }
  }

  if (!decoded.has_position) return false;

  if (!decoded.has_tex1) {
    decoded.tex1_u = decoded.tex0_u;
    decoded.tex1_v = decoded.tex0_v;
  }

  if (decoded.influence_count > 1) {
    float explicit_sum = 0.0f;
    for (UINT i = 0; i + 1 < decoded.influence_count && i < 4; ++i) explicit_sum += decoded.weights[i];
    const float inferred = std::max(0.0f, 1.0f - explicit_sum);
    decoded.weights[decoded.influence_count - 1] = inferred;
  } else {
    decoded.weights[0] = 1.0f;
  }

  float weight_sum = 0.0f;
  for (UINT i = 0; i < decoded.influence_count && i < 4; ++i) weight_sum += decoded.weights[i];
  if (weight_sum <= 1.0e-6f) {
    decoded.weights = {1.0f, 0.0f, 0.0f, 0.0f};
    decoded.influence_count = 1;
    weight_sum = 1.0f;
  }

  const bool use_skinning = decoded.has_blend_indices && (g_ffp_state.vertex_shader != nullptr);
  D3DXVECTOR3 cam_pos{};
  D3DXVECTOR3 cam_nrm(0.0f, 0.0f, 1.0f);

  if (use_skinning) {
    D3DXVECTOR3 accum_pos(0.0f, 0.0f, 0.0f);
    D3DXVECTOR3 accum_nrm(0.0f, 0.0f, 0.0f);
    for (UINT i = 0; i < decoded.influence_count && i < 4; ++i) {
      float w = decoded.weights[i];
      if (w <= 1.0e-6f) continue;
      const UINT bone_index = (palette_size > 0u) ? std::min<UINT>(decoded.indices[i], palette_size - 1u) : decoded.indices[i];
      const D3DXVECTOR3 bone_pos = TransformPositionBySkinMatrix(decoded.position, bone_index);
      const D3DXVECTOR3 bone_nrm = TransformNormalBySkinMatrix(decoded.normal, bone_index);
      accum_pos.x += bone_pos.x * w;
      accum_pos.y += bone_pos.y * w;
      accum_pos.z += bone_pos.z * w;
      accum_nrm.x += bone_nrm.x * w;
      accum_nrm.y += bone_nrm.y * w;
      accum_nrm.z += bone_nrm.z * w;
    }
    cam_pos = accum_pos;
    cam_nrm = accum_nrm;
    if (std::fabs(cam_nrm.x) > 1.0e-6f || std::fabs(cam_nrm.y) > 1.0e-6f || std::fabs(cam_nrm.z) > 1.0e-6f) {
      D3DXVec3Normalize(&cam_nrm, &cam_nrm);
    } else {
      cam_nrm = D3DXVECTOR3(0.0f, 0.0f, 1.0f);
    }
  } else {
    D3DXMATRIX world_view{};
    D3DXMatrixMultiply(
        &world_view,
        reinterpret_cast<const D3DXMATRIX*>(&g_ffp_state.world[0]),
        reinterpret_cast<const D3DXMATRIX*>(&g_ffp_state.view));
    D3DXVec3TransformCoord(&cam_pos, &decoded.position, &world_view);
    D3DXVECTOR3 world_nrm{};
    world_nrm.x = decoded.normal.x * g_ffp_state.world[0]._11 + decoded.normal.y * g_ffp_state.world[0]._21 + decoded.normal.z * g_ffp_state.world[0]._31;
    world_nrm.y = decoded.normal.x * g_ffp_state.world[0]._12 + decoded.normal.y * g_ffp_state.world[0]._22 + decoded.normal.z * g_ffp_state.world[0]._32;
    world_nrm.z = decoded.normal.x * g_ffp_state.world[0]._13 + decoded.normal.y * g_ffp_state.world[0]._23 + decoded.normal.z * g_ffp_state.world[0]._33;
    cam_nrm.x = world_nrm.x * g_ffp_state.view._11 + world_nrm.y * g_ffp_state.view._21 + world_nrm.z * g_ffp_state.view._31;
    cam_nrm.y = world_nrm.x * g_ffp_state.view._12 + world_nrm.y * g_ffp_state.view._22 + world_nrm.z * g_ffp_state.view._32;
    cam_nrm.z = world_nrm.x * g_ffp_state.view._13 + world_nrm.y * g_ffp_state.view._23 + world_nrm.z * g_ffp_state.view._33;
    D3DXVec3Normalize(&cam_nrm, &cam_nrm);
  }

  D3DXVECTOR4 clip{};
  if (use_skinning) {
    clip = TransformPositionByShaderMatrix4(cam_pos, 2u);
  } else {
    D3DXVec3Transform(&clip, &cam_pos, reinterpret_cast<const D3DXMATRIX*>(&g_ffp_state.proj));
  }
  out_vertex->x = clip.x;
  out_vertex->y = clip.y;
  out_vertex->z = ((g_debug_ffp_flags & kDebugUseRawClipZ) != 0u) ? clip.z : (2.0f * clip.z - clip.w);
  out_vertex->w = std::isfinite(clip.w) ? clip.w : 1.0e-5f;
  out_vertex->u0 = decoded.tex0_u;
  out_vertex->v0 = decoded.tex0_v;
  out_vertex->u1 = decoded.tex1_u;
  out_vertex->v1 = decoded.tex1_v;
  out_vertex->cam_pos_x = cam_pos.x;
  out_vertex->cam_pos_y = cam_pos.y;
  out_vertex->cam_pos_z = cam_pos.z;
  out_vertex->cam_nrm_x = cam_nrm.x;
  out_vertex->cam_nrm_y = cam_nrm.y;
  out_vertex->cam_nrm_z = cam_nrm.z;

  if (use_skinning) {
    const D3DXVECTOR3 light_dir = Normalize3(D3DXVECTOR3(ShaderConst(1u, 0), ShaderConst(1u, 1), ShaderConst(1u, 2)));
    const float ndotl = std::max(0.0f, Dot3(cam_nrm, light_dir));
    const float diff_r = ShaderConst(8u, 0);
    const float diff_g = ShaderConst(8u, 1);
    const float diff_b = ShaderConst(8u, 2);
    const float diff_a = ShaderConst(8u, 3);
    const float amb_r = ShaderConst(7u, 0);
    const float amb_g = ShaderConst(7u, 1);
    const float amb_b = ShaderConst(7u, 2);
    const float lit_r = amb_r + diff_r * ndotl;
    const float lit_g = amb_g + diff_g * ndotl;
    const float lit_b = amb_b + diff_b * ndotl;
    EncodeFloatColor(lit_r, lit_g, lit_b, diff_a > 0.0f ? diff_a : 1.0f, &out_vertex->r, &out_vertex->g, &out_vertex->b, &out_vertex->a);
  } else if (decoded.has_diffuse) {
    ARGBToRGBA8(decoded.diffuse, &out_vertex->r, &out_vertex->g, &out_vertex->b, &out_vertex->a);
  } else {
    out_vertex->r = 255;
    out_vertex->g = 255;
    out_vertex->b = 255;
    out_vertex->a = 255;
  }

  if (!use_skinning) {
    const D3DCOLORVALUE color1 = decoded.has_diffuse ? ColorValueFromARGB(decoded.diffuse) : ColorValueFromVertex(*out_vertex);
    ApplyLegacyLightingToVertex(cam_pos, cam_nrm, decoded.has_diffuse, color1, out_vertex);
  }

  return true;
}

HRESULT BuildVerticesFromDeclarationStream(
    const uint8_t* src,
    UINT vertex_count,
    UINT stride,
    const DummyDirect3DVertexDeclaration9& decl,
    std::vector<FFPVertex>* out_vertices) {
  if (!src || !out_vertices || vertex_count == 0 || stride == 0) return D3DERR_INVALIDCALL;

  const UINT palette_size = EstimateSkinPaletteSizeFromConstants();
  const bool rgba_index_order = PreferRGBAIndexOrder(decl, src, vertex_count, stride, palette_size);
  const bool uses_skinning = (BlendInfluenceCountFromDeclaration(decl) > 1u) || DeclarationUsesD3DColorBlendIndices(decl, nullptr);
  g_decl_draw_calls += 1;
  if (uses_skinning) g_decl_skinned_draw_calls += 1;
  if (rgba_index_order) g_decl_rgba_index_order_draws += 1;
  else g_decl_bgra_index_order_draws += 1;

  out_vertices->clear();
  out_vertices->reserve(vertex_count);
  for (UINT i = 0; i < vertex_count; ++i) {
    FFPVertex vertex{};
    if (!DecodeVertexFromDeclaration(
            src + static_cast<size_t>(i) * stride,
            stride,
            decl,
            palette_size,
            rgba_index_order,
            &vertex)) {
      return D3DERR_INVALIDCALL;
    }
    out_vertices->push_back(vertex);
    g_decl_vertices_total += 1;
    const float safe_w = SafeClipW(vertex.w);
    const float ndc_x = vertex.x / safe_w;
    const float ndc_y = vertex.y / safe_w;
    const float ndc_z = vertex.z / safe_w;
    if (std::fabs(ndc_x) <= 1.25f &&
        std::fabs(ndc_y) <= 1.25f &&
        ndc_z >= -1.25f &&
        ndc_z <= 1.25f) {
      g_decl_vertices_in_clip += 1;
    }
  }
  return S_OK;
}

bool CompileShader(GLuint shader, const char* source) {
  if (!source) return false;
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint ok = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (ok == GL_TRUE) return true;
  char log[1024]{};
  GLsizei len = 0;
  glGetShaderInfoLog(shader, sizeof(log) - 1, &len, log);
  std::fprintf(stderr, "[WASM FFP] shader compile failed: %s\n", log);
  return false;
}

bool EnsureFFPProgram() {
  if (g_ffp_program.ready) return true;
  if (!EnsureWasmContext()) return false;

  static const char* kVs = R"GLSL(
attribute vec4 aPos;
attribute vec2 aUV0;
attribute vec2 aUV1;
attribute vec3 aCamPos;
attribute vec3 aCamNormal;
attribute vec4 aColor;
uniform int uStage0TexcoordSet;
uniform int uStage1TexcoordSet;
uniform int uStage0TCIMode;
uniform int uStage1TCIMode;
uniform mat4 uTexTransform0;
uniform mat4 uTexTransform1;
uniform int uTexTransformFlags0;
uniform int uTexTransformFlags1;
uniform int uFogEnable;
uniform int uFogMode;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uFogDensity;
uniform int uRangeFogEnable;
varying vec2 vUV0;
varying vec2 vUV1;
varying vec4 vColor;
varying float vFogFactor;

vec2 chooseBaseTexcoord(int setIndex) {
  return (setIndex == 0) ? aUV0 : aUV1;
}

vec2 generatedTexcoord(int tciMode) {
  vec3 n = normalize(aCamNormal);
  vec3 e = normalize(-aCamPos);
  if (tciMode == 1) return n.xy;
  if (tciMode == 2) return aCamPos.xy;
  if (tciMode == 3) {
    vec3 r = reflect(e, n);
    return r.xy;
  }
  return vec2(0.0);
}

vec2 resolveTexcoord(int setIndex, int tciMode) {
  if (tciMode == 0) return chooseBaseTexcoord(setIndex);
  return generatedTexcoord(tciMode);
}

vec2 transformTexcoord(vec2 uv, mat4 m, int flags) {
  int count = int(mod(float(flags), 256.0));
  bool projected = mod(floor(float(flags) / 256.0), 2.0) >= 1.0;
  vec4 t = vec4(uv, 0.0, 1.0);
  if (count != 0) {
    t = m * t;
  }
  if (projected && abs(t.w) > 1.0e-6) {
    t.xy /= t.w;
  }
  return t.xy;
}

float computeFogFactor(vec3 camPos) {
  if (uFogEnable == 0 || uFogMode == 0) return 1.0;
  float dist = (uRangeFogEnable != 0) ? length(camPos) : abs(camPos.z);
  if (uFogMode == 3) {
    float denom = max(abs(uFogEnd - uFogStart), 1.0e-6);
    return clamp((uFogEnd - dist) / denom, 0.0, 1.0);
  }
  if (uFogMode == 1) {
    return clamp(exp(-max(0.0, uFogDensity) * dist), 0.0, 1.0);
  }
  if (uFogMode == 2) {
    float d = max(0.0, uFogDensity) * dist;
    return clamp(exp(-(d * d)), 0.0, 1.0);
  }
  return 1.0;
}

void main() {
  gl_Position = aPos;
  vec2 base0 = resolveTexcoord(uStage0TexcoordSet, uStage0TCIMode);
  vec2 base1 = resolveTexcoord(uStage1TexcoordSet, uStage1TCIMode);
  vUV0 = transformTexcoord(base0, uTexTransform0, uTexTransformFlags0);
  vUV1 = transformTexcoord(base1, uTexTransform1, uTexTransformFlags1);
  vColor = aColor;
  vFogFactor = computeFogFactor(aCamPos);
}
)GLSL";

  static const char* kFs = R"GLSL(
precision mediump float;
uniform sampler2D uSampler0;
uniform sampler2D uSampler1;
uniform int uUseTexture0;
uniform int uUseTexture1;
uniform int uColorOp0;
uniform int uColorArg10;
uniform int uColorArg20;
uniform int uAlphaOp0;
uniform int uAlphaArg10;
uniform int uAlphaArg20;
uniform int uColorOp1;
uniform int uColorArg11;
uniform int uColorArg21;
uniform int uAlphaOp1;
uniform int uAlphaArg11;
uniform int uAlphaArg21;
uniform vec4 uTextureFactor;
uniform vec4 uAmbient;
uniform int uLightingEnable;
uniform int uBlendEnable;
uniform int uAlphaTestEnable;
uniform float uAlphaRef;
uniform int uAlphaFunc;
uniform int uDebugFlags;
uniform vec4 uFogColor;
varying vec2 vUV0;
varying vec2 vUV1;
varying vec4 vColor;
varying float vFogFactor;

vec4 resolveArg(int arg, vec4 texel, vec4 diffuse, vec4 currentColor) {
  int src = int(mod(float(arg), 16.0));
  vec4 c = diffuse;
  if (src == 2) c = texel;
  else if (src == 1) c = currentColor;
  else if (src == 3) c = uTextureFactor;
  float farg = float(arg);
  bool complement = mod(floor(farg / 16.0), 2.0) >= 1.0;
  bool alphaReplicate = mod(floor(farg / 32.0), 2.0) >= 1.0;
  if (complement) c = vec4(1.0) - c;
  if (alphaReplicate) c = vec4(c.aaaa);
  return c;
}

vec4 applyColorOp(int op, vec4 a1, vec4 a2, vec4 currentColor, vec4 diffuse, vec4 texel) {
  if (op == 1) return currentColor;
  if (op == 2) return a1;
  if (op == 3) return a2;
  if (op == 4) return a1 * a2;
  if (op == 5) return min(a1 * a2 * 2.0, vec4(1.0));
  if (op == 6) return min(a1 * a2 * 4.0, vec4(1.0));
  if (op == 7) return min(a1 + a2, vec4(1.0));
  if (op == 8) return clamp(a1 + a2 - vec4(0.5), 0.0, 1.0);
  if (op == 9) return clamp((a1 + a2 - vec4(0.5)) * 2.0, 0.0, 1.0);
  if (op == 10) return clamp(a1 - a2, 0.0, 1.0);
  if (op == 11) return min(a1 + a2 * (vec4(1.0) - a1), vec4(1.0));
  if (op == 12) return a1 * diffuse.a + a2 * (1.0 - diffuse.a);
  if (op == 13) return a1 * texel.a + a2 * (1.0 - texel.a);
  if (op == 14) return a1 * uTextureFactor.a + a2 * (1.0 - uTextureFactor.a);
  if (op == 15) return min(a1 + a2 * (1.0 - texel.a), vec4(1.0));
  if (op == 16) return a1 * currentColor.a + a2 * (1.0 - currentColor.a);
  if (op == 18) return vec4(min(a1.rgb * a1.a + a2.rgb, vec3(1.0)), currentColor.a);
  if (op == 19) return vec4(min(a1.rgb + a2.rgb * a1.a, vec3(1.0)), currentColor.a);
  if (op == 20) return vec4(min((vec3(1.0) - a1.aaa) * a2.rgb + a1.rgb, vec3(1.0)), currentColor.a);
  if (op == 21) return vec4(min((vec3(1.0) - a1.rgb) * a2.rgb + a1.aaa, vec3(1.0)), currentColor.a);
  if (op == 24) {
    float dp = dot((a1.rgb - vec3(0.5)) * 2.0, (a2.rgb - vec3(0.5)) * 2.0);
    dp = clamp(dp, 0.0, 1.0);
    return vec4(dp, dp, dp, currentColor.a);
  }
  return a1 * a2;
}

float applyAlphaOp(int op, float a1, float a2, float currentA, float diffuseA, float texA) {
  if (op == 1) return currentA;
  if (op == 2) return a1;
  if (op == 3) return a2;
  if (op == 4) return a1 * a2;
  if (op == 5) return min(a1 * a2 * 2.0, 1.0);
  if (op == 6) return min(a1 * a2 * 4.0, 1.0);
  if (op == 7) return min(a1 + a2, 1.0);
  if (op == 8) return clamp(a1 + a2 - 0.5, 0.0, 1.0);
  if (op == 9) return clamp((a1 + a2 - 0.5) * 2.0, 0.0, 1.0);
  if (op == 10) return clamp(a1 - a2, 0.0, 1.0);
  if (op == 11) return min(a1 + a2 * (1.0 - a1), 1.0);
  if (op == 12) return a1 * diffuseA + a2 * (1.0 - diffuseA);
  if (op == 13) return a1 * texA + a2 * (1.0 - texA);
  if (op == 14) return a1 * uTextureFactor.a + a2 * (1.0 - uTextureFactor.a);
  if (op == 15) return min(a1 + a2 * (1.0 - texA), 1.0);
  if (op == 16) return a1 * currentA + a2 * (1.0 - currentA);
  return a1 * a2;
}

bool alphaTestPass(float a, float refv, int func) {
  if (func == 1) return false;
  if (func == 2) return a < refv;
  if (func == 3) return abs(a - refv) < (1.0 / 255.0);
  if (func == 4) return a <= refv;
  if (func == 5) return a > refv;
  if (func == 6) return abs(a - refv) >= (1.0 / 255.0);
  if (func == 7) return a >= refv;
  return true;
}

void main() {
  vec4 texel0 = (uUseTexture0 != 0) ? texture2D(uSampler0, vUV0) : vec4(1.0);
  vec4 texel1 = (uUseTexture1 != 0) ? texture2D(uSampler1, vUV1) : vec4(1.0);
  bool forceOpaqueTextureAlpha = mod(floor(float(uDebugFlags) / 8.0), 2.0) >= 1.0;
  if (forceOpaqueTextureAlpha) {
    texel0.a = 1.0;
    texel1.a = 1.0;
  }
	  vec4 diffuse = vColor;
	
	  vec3 currentRgb = diffuse.rgb;
	  float currentA = diffuse.a;

  vec4 stage0Current = vec4(currentRgb, currentA);
  vec3 stage0Rgb = currentRgb;
  float stage0A = currentA;
  if (uColorOp0 != 1) {
    vec4 c1 = resolveArg(uColorArg10, texel0, diffuse, stage0Current);
    vec4 c2 = resolveArg(uColorArg20, texel0, diffuse, stage0Current);
    stage0Rgb = applyColorOp(uColorOp0, c1, c2, stage0Current, diffuse, texel0).rgb;
  }
  if (uAlphaOp0 != 1) {
    vec4 a1v = resolveArg(uAlphaArg10, texel0, diffuse, stage0Current);
    vec4 a2v = resolveArg(uAlphaArg20, texel0, diffuse, stage0Current);
    stage0A = applyAlphaOp(uAlphaOp0, a1v.a, a2v.a, currentA, diffuse.a, texel0.a);
  }
  currentRgb = stage0Rgb;
  currentA = stage0A;

  vec4 stage1Current = vec4(currentRgb, currentA);
  vec3 stage1Rgb = currentRgb;
  float stage1A = currentA;
  if (uColorOp1 != 1) {
    vec4 c1 = resolveArg(uColorArg11, texel1, diffuse, stage1Current);
    vec4 c2 = resolveArg(uColorArg21, texel1, diffuse, stage1Current);
    stage1Rgb = applyColorOp(uColorOp1, c1, c2, stage1Current, diffuse, texel1).rgb;
  }
  if (uAlphaOp1 != 1) {
    vec4 a1v = resolveArg(uAlphaArg11, texel1, diffuse, stage1Current);
    vec4 a2v = resolveArg(uAlphaArg21, texel1, diffuse, stage1Current);
    stage1A = applyAlphaOp(uAlphaOp1, a1v.a, a2v.a, currentA, diffuse.a, texel1.a);
  }
  currentRgb = stage1Rgb;
  currentA = stage1A;

  vec4 outColor = vec4(clamp(currentRgb, 0.0, 1.0), clamp(currentA, 0.0, 1.0));
  if (uAlphaTestEnable != 0 && !alphaTestPass(outColor.a, uAlphaRef, uAlphaFunc)) discard;
  outColor.rgb = mix(uFogColor.rgb, outColor.rgb, clamp(vFogFactor, 0.0, 1.0));
  gl_FragColor = outColor;
}
)GLSL";

  GLuint vs = glCreateShader(GL_VERTEX_SHADER);
  GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
  if (!CompileShader(vs, kVs) || !CompileShader(fs, kFs)) {
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    return false;
  }

  GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glBindAttribLocation(program, 0, "aPos");
  glBindAttribLocation(program, 1, "aUV0");
  glBindAttribLocation(program, 2, "aUV1");
  glBindAttribLocation(program, 3, "aCamPos");
  glBindAttribLocation(program, 4, "aCamNormal");
  glBindAttribLocation(program, 5, "aColor");
  glLinkProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);

  GLint linked = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  if (linked != GL_TRUE) {
    char log[1024]{};
    GLsizei len = 0;
    glGetProgramInfoLog(program, sizeof(log) - 1, &len, log);
    std::fprintf(stderr, "[WASM FFP] program link failed: %s\n", log);
    glDeleteProgram(program);
    return false;
  }

  GLuint vbo = 0;
  GLuint ibo = 0;
  glGenBuffers(1, &vbo);
  glGenBuffers(1, &ibo);

  g_ffp_program.program = program;
  g_ffp_program.vbo = vbo;
  g_ffp_program.ibo = ibo;
  g_ffp_program.attr_pos = 0;
  g_ffp_program.attr_uv0 = 1;
  g_ffp_program.attr_uv1 = 2;
  g_ffp_program.attr_cam_pos = 3;
  g_ffp_program.attr_cam_nrm = 4;
  g_ffp_program.attr_color = 5;
  g_ffp_program.uni_sampler0 = glGetUniformLocation(program, "uSampler0");
  g_ffp_program.uni_sampler1 = glGetUniformLocation(program, "uSampler1");
  g_ffp_program.uni_use_texture0 = glGetUniformLocation(program, "uUseTexture0");
  g_ffp_program.uni_use_texture1 = glGetUniformLocation(program, "uUseTexture1");
  g_ffp_program.uni_color_op0 = glGetUniformLocation(program, "uColorOp0");
  g_ffp_program.uni_color_arg10 = glGetUniformLocation(program, "uColorArg10");
  g_ffp_program.uni_color_arg20 = glGetUniformLocation(program, "uColorArg20");
  g_ffp_program.uni_alpha_op0 = glGetUniformLocation(program, "uAlphaOp0");
  g_ffp_program.uni_alpha_arg10 = glGetUniformLocation(program, "uAlphaArg10");
  g_ffp_program.uni_alpha_arg20 = glGetUniformLocation(program, "uAlphaArg20");
  g_ffp_program.uni_color_op1 = glGetUniformLocation(program, "uColorOp1");
  g_ffp_program.uni_color_arg11 = glGetUniformLocation(program, "uColorArg11");
  g_ffp_program.uni_color_arg21 = glGetUniformLocation(program, "uColorArg21");
  g_ffp_program.uni_alpha_op1 = glGetUniformLocation(program, "uAlphaOp1");
  g_ffp_program.uni_alpha_arg11 = glGetUniformLocation(program, "uAlphaArg11");
  g_ffp_program.uni_alpha_arg21 = glGetUniformLocation(program, "uAlphaArg21");
  g_ffp_program.uni_stage0_texcoord_set = glGetUniformLocation(program, "uStage0TexcoordSet");
  g_ffp_program.uni_stage1_texcoord_set = glGetUniformLocation(program, "uStage1TexcoordSet");
  g_ffp_program.uni_stage0_tci_mode = glGetUniformLocation(program, "uStage0TCIMode");
  g_ffp_program.uni_stage1_tci_mode = glGetUniformLocation(program, "uStage1TCIMode");
  g_ffp_program.uni_tex_transform0 = glGetUniformLocation(program, "uTexTransform0");
  g_ffp_program.uni_tex_transform1 = glGetUniformLocation(program, "uTexTransform1");
  g_ffp_program.uni_tex_transform_flags0 = glGetUniformLocation(program, "uTexTransformFlags0");
  g_ffp_program.uni_tex_transform_flags1 = glGetUniformLocation(program, "uTexTransformFlags1");
  g_ffp_program.uni_texture_factor = glGetUniformLocation(program, "uTextureFactor");
  g_ffp_program.uni_ambient = glGetUniformLocation(program, "uAmbient");
  g_ffp_program.uni_lighting_enable = glGetUniformLocation(program, "uLightingEnable");
  g_ffp_program.uni_blend_enable = glGetUniformLocation(program, "uBlendEnable");
  g_ffp_program.uni_alpha_test_enable = glGetUniformLocation(program, "uAlphaTestEnable");
  g_ffp_program.uni_alpha_ref = glGetUniformLocation(program, "uAlphaRef");
  g_ffp_program.uni_alpha_func = glGetUniformLocation(program, "uAlphaFunc");
  g_ffp_program.uni_debug_flags = glGetUniformLocation(program, "uDebugFlags");
  g_ffp_program.uni_fog_color = glGetUniformLocation(program, "uFogColor");
  g_ffp_program.uni_fog_enable = glGetUniformLocation(program, "uFogEnable");
  g_ffp_program.uni_fog_mode = glGetUniformLocation(program, "uFogMode");
  g_ffp_program.uni_fog_start = glGetUniformLocation(program, "uFogStart");
  g_ffp_program.uni_fog_end = glGetUniformLocation(program, "uFogEnd");
  g_ffp_program.uni_fog_density = glGetUniformLocation(program, "uFogDensity");
  g_ffp_program.uni_range_fog_enable = glGetUniformLocation(program, "uRangeFogEnable");
  g_ffp_program.ready = true;
  return true;
}

void BuildWorldViewProj(D3DXMATRIX* out) {
  if (!out) return;
  EnsureFFPStateInitialized();
  D3DXMATRIX mat_wv{};
  D3DXMatrixMultiply(&mat_wv, reinterpret_cast<const D3DXMATRIX*>(&g_ffp_state.world[0]), reinterpret_cast<const D3DXMATRIX*>(&g_ffp_state.view));
  D3DXMatrixMultiply(out, &mat_wv, reinterpret_cast<const D3DXMATRIX*>(&g_ffp_state.proj));
}

void ARGBToRGBA8(uint32_t color, uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a) {
  if (a) *a = static_cast<uint8_t>((color >> 24) & 0xFFu);
  if (r) *r = static_cast<uint8_t>((color >> 16) & 0xFFu);
  if (g) *g = static_cast<uint8_t>((color >> 8) & 0xFFu);
  if (b) *b = static_cast<uint8_t>(color & 0xFFu);
}

void TexturePixelToRGBA(const DummyDirect3DTexture9* tex, const uint8_t* src, uint8_t* out_rgba) {
  if (!tex || !src || !out_rgba) return;
  switch (tex->format) {
    case D3DFMT_R5G6B5: {
      const uint16_t v = ReadUnaligned<uint16_t>(src);
      Decode565(v, &out_rgba[0], &out_rgba[1], &out_rgba[2]);
      out_rgba[3] = 255;
      break;
    }
    case D3DFMT_X1R5G5B5:
    case D3DFMT_A1R5G5B5: {
      const uint16_t v = ReadUnaligned<uint16_t>(src);
      out_rgba[0] = static_cast<uint8_t>((((v >> 10) & 0x1Fu) << 3) | (((v >> 10) & 0x1Fu) >> 2));
      out_rgba[1] = static_cast<uint8_t>((((v >> 5) & 0x1Fu) << 3) | (((v >> 5) & 0x1Fu) >> 2));
      out_rgba[2] = static_cast<uint8_t>(((v & 0x1Fu) << 3) | ((v & 0x1Fu) >> 2));
      out_rgba[3] = (tex->format == D3DFMT_A1R5G5B5) ? ((v & 0x8000u) ? 255 : 0) : 255;
      break;
    }
    case D3DFMT_A4R4G4B4: {
      const uint16_t v = ReadUnaligned<uint16_t>(src);
      const uint8_t a4 = static_cast<uint8_t>((v >> 12) & 0x0Fu);
      const uint8_t r4 = static_cast<uint8_t>((v >> 8) & 0x0Fu);
      const uint8_t g4 = static_cast<uint8_t>((v >> 4) & 0x0Fu);
      const uint8_t b4 = static_cast<uint8_t>(v & 0x0Fu);
      out_rgba[0] = static_cast<uint8_t>((r4 << 4) | r4);
      out_rgba[1] = static_cast<uint8_t>((g4 << 4) | g4);
      out_rgba[2] = static_cast<uint8_t>((b4 << 4) | b4);
      out_rgba[3] = static_cast<uint8_t>((a4 << 4) | a4);
      break;
    }
    case D3DFMT_A8B8G8R8:
      out_rgba[0] = src[0];
      out_rgba[1] = src[1];
      out_rgba[2] = src[2];
      out_rgba[3] = src[3];
      break;
    case D3DFMT_X8R8G8B8:
      if ((g_debug_ffp_flags & kDebugSwapTextureRB) != 0u) {
        out_rgba[0] = src[0];
        out_rgba[1] = src[1];
        out_rgba[2] = src[2];
      } else {
        out_rgba[0] = src[2];
        out_rgba[1] = src[1];
        out_rgba[2] = src[0];
      }
      out_rgba[3] = 255;
      break;
    case D3DFMT_A8R8G8B8:
    default:
      if ((g_debug_ffp_flags & kDebugSwapTextureRB) != 0u) {
        out_rgba[0] = src[0];
        out_rgba[1] = src[1];
        out_rgba[2] = src[2];
      } else {
        out_rgba[0] = src[2];
        out_rgba[1] = src[1];
        out_rgba[2] = src[0];
      }
      out_rgba[3] = src[3];
      break;
  }
}

void BuildGLTextureRGBA(const DummyDirect3DTexture9* tex, std::vector<uint8_t>* out_rgba) {
  if (!tex || !out_rgba) return;
  out_rgba->assign(static_cast<size_t>(tex->width) * static_cast<size_t>(tex->height) * 4u, 0);
  uint8_t decoded[4]{};
  for (UINT y = 0; y < tex->height; ++y) {
    const uint8_t* row = tex->pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(tex->pitch);
    for (UINT x = 0; x < tex->width; ++x) {
      const uint8_t* src = row + static_cast<size_t>(x) * static_cast<size_t>(tex->bytes_per_pixel);
      TexturePixelToRGBA(tex, src, decoded);
      const size_t dst = (static_cast<size_t>(y) * static_cast<size_t>(tex->width) + static_cast<size_t>(x)) * 4u;
      (*out_rgba)[dst + 0] = decoded[0];
      (*out_rgba)[dst + 1] = decoded[1];
      (*out_rgba)[dst + 2] = decoded[2];
      (*out_rgba)[dst + 3] = decoded[3];
    }
  }
}

void ApplyTextureSamplerState(DWORD stage) {
  if (stage >= kMaxTextureStages) return;

  const DWORD addr_u = g_ffp_state.sampler[stage][D3DSAMP_ADDRESSU];
  const DWORD addr_v = g_ffp_state.sampler[stage][D3DSAMP_ADDRESSV];
  const GLint wrap_u = (addr_u == 1) ? GL_REPEAT : GL_CLAMP_TO_EDGE;
  const GLint wrap_v = (addr_v == 1) ? GL_REPEAT : GL_CLAMP_TO_EDGE;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_u);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_v);

  const DWORD min_filter = g_ffp_state.sampler[stage][D3DSAMP_MINFILTER];
  const DWORD mag_filter = g_ffp_state.sampler[stage][D3DSAMP_MAGFILTER];
  const GLint gl_min = (min_filter == 1) ? GL_NEAREST : GL_LINEAR;
  const GLint gl_mag = (mag_filter == 1) ? GL_NEAREST : GL_LINEAR;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_min);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_mag);
}

bool BindTextureStage(DWORD stage) {
  if (stage >= kMaxTextureStages) return false;
  auto* tex = AsTexture(g_ffp_state.textures[stage]);
  if (!tex || tex->pixels.empty()) return false;

  if (tex->gl_texture == 0) {
    glGenTextures(1, &tex->gl_texture);
    tex->gl_dirty = true;
  }

  glActiveTexture(GL_TEXTURE0 + stage);
  glBindTexture(GL_TEXTURE_2D, tex->gl_texture);
  ApplyTextureSamplerState(stage);

  if (tex->gl_dirty) {
    std::vector<uint8_t> rgba;
    BuildGLTextureRGBA(tex, &rgba);
    if (rgba.empty()) return false;
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        static_cast<GLsizei>(tex->width),
        static_cast<GLsizei>(tex->height),
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        rgba.data());
    tex->gl_dirty = false;
    g_tex_upload_count += 1;
  }

  g_tex_draw_count += 1;
  return true;
}

DWORD StageStateValue(DWORD stage, D3DTEXTURESTAGESTATETYPE type, DWORD default_value) {
  if (stage >= kMaxTextureStages) return default_value;
  const int idx = static_cast<int>(type);
  if (idx < 0 || idx >= static_cast<int>(g_ffp_state.tex_stage[stage].size())) return default_value;
  return g_ffp_state.tex_stage[stage][idx];
}

float NormalizeAlphaRefValue(DWORD ref_raw) {
  if (ref_raw <= 0xFu) {
    const uint8_t n = static_cast<uint8_t>(ref_raw & 0x0Fu);
    return static_cast<float>((n << 4) | n) / 255.0f;
  }
  if (ref_raw <= 0xFFu) return static_cast<float>(ref_raw) / 255.0f;
  const uint8_t hi8 = static_cast<uint8_t>((ref_raw >> 24) & 0xFFu);
  if (hi8 != 0) return static_cast<float>(hi8) / 255.0f;
  const uint8_t hi4 = static_cast<uint8_t>((ref_raw >> 12) & 0x0Fu);
  if (hi4 != 0) return static_cast<float>((hi4 << 4) | hi4) / 255.0f;
  return static_cast<float>(ref_raw & 0xFFu) / 255.0f;
}

void DecodeD3DColor(D3DCOLOR color, float* out_r, float* out_g, float* out_b, float* out_a) {
  if (out_a) *out_a = static_cast<float>((color >> 24) & 0xFFu) / 255.0f;
  if (out_r) *out_r = static_cast<float>((color >> 16) & 0xFFu) / 255.0f;
  if (out_g) *out_g = static_cast<float>((color >> 8) & 0xFFu) / 255.0f;
  if (out_b) *out_b = static_cast<float>(color & 0xFFu) / 255.0f;
}

struct StageTexCoordBinding {
  GLint set_index = 0;
  GLint tci_mode = 0;
};

StageTexCoordBinding ResolveStageTexCoordBinding(DWORD stage, DWORD default_set) {
  const DWORD raw = StageStateValue(stage, D3DTSS_TEXCOORDINDEX, default_set);
  const DWORD set_raw = (raw & 0xFFFFu);
  const DWORD mode_raw = ((raw >> 16) & 0xFFFFu);
  StageTexCoordBinding out{};
  out.set_index = (set_raw == 0u) ? 0 : 1;
  switch (mode_raw) {
    case 1u:
      out.tci_mode = 1;
      break;
    case 2u:
      out.tci_mode = 2;
      break;
    case 3u:
      out.tci_mode = 3;
      break;
    default:
      out.tci_mode = 0;
      break;
  }
  return out;
}

void UploadTexTransformUniforms(
    DWORD stage,
    GLint uni_matrix,
    GLint uni_flags) {
  const DWORD ttf = StageStateValue(stage, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
  const GLint flags_uniform = static_cast<GLint>(ttf & 0x1FFu);
  if (uni_flags >= 0) glUniform1i(uni_flags, flags_uniform);
  if (uni_matrix >= 0) {
    glUniformMatrix4fv(
        uni_matrix,
        1,
        GL_FALSE,
        reinterpret_cast<const GLfloat*>(&g_ffp_state.tex[stage]));
  }
}

bool ApplyStageUniforms(
    DWORD stage,
    bool has_texture,
    bool force_disable,
    GLint uni_use_texture,
    GLint uni_color_op,
    GLint uni_color_arg1,
    GLint uni_color_arg2,
    GLint uni_alpha_op,
    GLint uni_alpha_arg1,
    GLint uni_alpha_arg2) {
  const DWORD default_color_op = (stage == 0) ? D3DTOP_MODULATE : D3DTOP_DISABLE;
  const DWORD default_alpha_op = (stage == 0) ? D3DTOP_SELECTARG1 : D3DTOP_DISABLE;
  DWORD color_op = StageStateValue(stage, D3DTSS_COLOROP, default_color_op);
  DWORD color_arg1 = StageStateValue(stage, D3DTSS_COLORARG1, D3DTA_TEXTURE);
  DWORD color_arg2 = StageStateValue(stage, D3DTSS_COLORARG2, (stage == 0) ? D3DTA_DIFFUSE : D3DTA_CURRENT);
  DWORD alpha_op = StageStateValue(stage, D3DTSS_ALPHAOP, default_alpha_op);
  DWORD alpha_arg1 = StageStateValue(stage, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
  DWORD alpha_arg2 = StageStateValue(stage, D3DTSS_ALPHAARG2, (stage == 0) ? D3DTA_DIFFUSE : D3DTA_CURRENT);
  if (color_op == 0) color_op = D3DTOP_DISABLE;
  if (alpha_op == 0) alpha_op = D3DTOP_DISABLE;
  if (force_disable) {
    color_op = D3DTOP_DISABLE;
    alpha_op = D3DTOP_DISABLE;
  }

  if (!has_texture) {
    // D3D9 semantics: if no texture is set for the stage, texture arguments
    // resolve to diffuse (not to previous stage current).
    if ((color_arg1 & 0xFu) == D3DTA_TEXTURE) color_arg1 = D3DTA_DIFFUSE;
    if ((color_arg2 & 0xFu) == D3DTA_TEXTURE) color_arg2 = D3DTA_DIFFUSE;
    if ((alpha_arg1 & 0xFu) == D3DTA_TEXTURE) alpha_arg1 = D3DTA_DIFFUSE;
    if ((alpha_arg2 & 0xFu) == D3DTA_TEXTURE) alpha_arg2 = D3DTA_DIFFUSE;
  }

  if (color_op == D3DTOP_DISABLE) {
    alpha_op = D3DTOP_DISABLE;
  } else if (alpha_op == D3DTOP_DISABLE) {
    // D3D9 treats alpha-op disable with color-op enabled as undefined.
    // Keep deterministic output by selecting arg1.
    alpha_op = D3DTOP_SELECTARG1;
  }

  if (uni_use_texture >= 0) glUniform1i(uni_use_texture, has_texture ? 1 : 0);
  if (uni_color_op >= 0) glUniform1i(uni_color_op, static_cast<GLint>(color_op));
  if (uni_color_arg1 >= 0) glUniform1i(uni_color_arg1, static_cast<GLint>(color_arg1));
  if (uni_color_arg2 >= 0) glUniform1i(uni_color_arg2, static_cast<GLint>(color_arg2));
  if (uni_alpha_op >= 0) glUniform1i(uni_alpha_op, static_cast<GLint>(alpha_op));
  if (uni_alpha_arg1 >= 0) glUniform1i(uni_alpha_arg1, static_cast<GLint>(alpha_arg1));
  if (uni_alpha_arg2 >= 0) glUniform1i(uni_alpha_arg2, static_cast<GLint>(alpha_arg2));
  return color_op != D3DTOP_DISABLE;
}

void ApplyFFPUniforms(bool has_stage0_texture, bool has_stage1_texture) {
  const bool stage0_active = ApplyStageUniforms(
      0,
      has_stage0_texture,
      false,
      g_ffp_program.uni_use_texture0,
      g_ffp_program.uni_color_op0,
      g_ffp_program.uni_color_arg10,
      g_ffp_program.uni_color_arg20,
      g_ffp_program.uni_alpha_op0,
      g_ffp_program.uni_alpha_arg10,
      g_ffp_program.uni_alpha_arg20);
  const bool disable_stage1 =
      !stage0_active || ((g_debug_ffp_flags & kDebugDisableStage1) != 0u);
  ApplyStageUniforms(
      1,
      disable_stage1 ? false : has_stage1_texture,
      disable_stage1,
      g_ffp_program.uni_use_texture1,
      g_ffp_program.uni_color_op1,
      g_ffp_program.uni_color_arg11,
      g_ffp_program.uni_color_arg21,
      g_ffp_program.uni_alpha_op1,
      g_ffp_program.uni_alpha_arg11,
      g_ffp_program.uni_alpha_arg21);

  const StageTexCoordBinding stage0_bind = ResolveStageTexCoordBinding(0, 0u);
  const StageTexCoordBinding stage1_bind = ResolveStageTexCoordBinding(1, 1u);
  if (g_ffp_program.uni_stage0_texcoord_set >= 0) glUniform1i(g_ffp_program.uni_stage0_texcoord_set, stage0_bind.set_index);
  if (g_ffp_program.uni_stage1_texcoord_set >= 0) glUniform1i(g_ffp_program.uni_stage1_texcoord_set, stage1_bind.set_index);
  if (g_ffp_program.uni_stage0_tci_mode >= 0) glUniform1i(g_ffp_program.uni_stage0_tci_mode, stage0_bind.tci_mode);
  if (g_ffp_program.uni_stage1_tci_mode >= 0) glUniform1i(g_ffp_program.uni_stage1_tci_mode, stage1_bind.tci_mode);

  if (stage1_bind.tci_mode != 0) g_stage1_generated_tci_draws += 1;
  if (stage1_bind.set_index == 0) g_stage1_tci0_draws += 1;
  else if (stage1_bind.set_index == 1) g_stage1_tci1_draws += 1;
  else g_stage1_tci_other_draws += 1;

  const DWORD stage1_ttf = StageStateValue(1, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
  if (stage1_ttf != D3DTTFF_DISABLE) g_stage1_textransform_draws += 1;
  UploadTexTransformUniforms(0, g_ffp_program.uni_tex_transform0, g_ffp_program.uni_tex_transform_flags0);
  UploadTexTransformUniforms(1, g_ffp_program.uni_tex_transform1, g_ffp_program.uni_tex_transform_flags1);

  float tf_r = 1.0f, tf_g = 1.0f, tf_b = 1.0f, tf_a = 1.0f;
  DecodeD3DColor(g_wasm_d3d9_state.texture_factor, &tf_r, &tf_g, &tf_b, &tf_a);
  if (g_ffp_program.uni_texture_factor >= 0) glUniform4f(g_ffp_program.uni_texture_factor, tf_r, tf_g, tf_b, tf_a);

  float amb_r = 1.0f, amb_g = 1.0f, amb_b = 1.0f, amb_a = 1.0f;
  DecodeD3DColor(g_wasm_d3d9_state.ambient, &amb_r, &amb_g, &amb_b, &amb_a);
  if (g_ffp_program.uni_ambient >= 0) glUniform4f(g_ffp_program.uni_ambient, amb_r, amb_g, amb_b, amb_a);
  if (g_ffp_program.uni_lighting_enable >= 0) {
    glUniform1i(g_ffp_program.uni_lighting_enable, g_wasm_d3d9_state.lighting_enable ? 1 : 0);
  }

  if (g_ffp_program.uni_blend_enable >= 0) {
    glUniform1i(g_ffp_program.uni_blend_enable, g_wasm_d3d9_state.blend_enabled ? 1 : 0);
  }

  const bool alpha_test_active =
      ((g_debug_ffp_flags & kDebugDisableAlphaTest) == 0u) &&
      (g_wasm_d3d9_state.alpha_test_enable != 0) &&
      (g_wasm_d3d9_state.alpha_func != D3DCMP_ALWAYS);
  if (g_ffp_program.uni_alpha_test_enable >= 0) {
    glUniform1i(g_ffp_program.uni_alpha_test_enable, alpha_test_active ? 1 : 0);
  }
  if (g_ffp_program.uni_alpha_ref >= 0) {
    glUniform1f(g_ffp_program.uni_alpha_ref, NormalizeAlphaRefValue(g_wasm_d3d9_state.alpha_ref));
  }
  if (g_ffp_program.uni_alpha_func >= 0) {
    glUniform1i(g_ffp_program.uni_alpha_func, static_cast<GLint>(g_wasm_d3d9_state.alpha_func));
  }
  if (g_ffp_program.uni_debug_flags >= 0) {
    const uint32_t shader_debug_flags = g_debug_ffp_flags & kDebugForceOpaqueTextureAlpha;
    glUniform1i(g_ffp_program.uni_debug_flags, static_cast<GLint>(shader_debug_flags));
  }
  if (g_ffp_program.uni_fog_color >= 0) {
    float fog_r = 0.0f, fog_g = 0.0f, fog_b = 0.0f, fog_a = 1.0f;
    DecodeD3DColor(g_wasm_d3d9_state.fog_color, &fog_r, &fog_g, &fog_b, &fog_a);
    glUniform4f(g_ffp_program.uni_fog_color, fog_r, fog_g, fog_b, fog_a);
  }
  const bool fog_active =
      ((g_debug_ffp_flags & kDebugDisableFog) == 0u) &&
      (g_wasm_d3d9_state.fog_enable != 0) &&
      (g_wasm_d3d9_state.fog_vertex_mode != D3DFOG_NONE);
  if (g_ffp_program.uni_fog_enable >= 0) {
    glUniform1i(g_ffp_program.uni_fog_enable, fog_active ? 1 : 0);
  }
  if (g_ffp_program.uni_fog_mode >= 0) {
    glUniform1i(g_ffp_program.uni_fog_mode, static_cast<GLint>(g_wasm_d3d9_state.fog_vertex_mode));
  }
  if (g_ffp_program.uni_fog_start >= 0) {
    glUniform1f(g_ffp_program.uni_fog_start, g_wasm_d3d9_state.fog_start);
  }
  if (g_ffp_program.uni_fog_end >= 0) {
    glUniform1f(g_ffp_program.uni_fog_end, g_wasm_d3d9_state.fog_end);
  }
  if (g_ffp_program.uni_fog_density >= 0) {
    glUniform1f(g_ffp_program.uni_fog_density, g_wasm_d3d9_state.fog_density);
  }
  if (g_ffp_program.uni_range_fog_enable >= 0) {
    glUniform1i(g_ffp_program.uni_range_fog_enable, g_wasm_d3d9_state.range_fog_enable ? 1 : 0);
  }
}

bool DecodeVertexFromFVF(
    const uint8_t* src,
    UINT stride,
    DWORD fvf,
    const D3DXMATRIX& world_view,
    const D3DXMATRIX& wvp,
    FFPVertex* out_vertex,
    bool* out_forced_screen_like,
    bool force_screen_space) {
  if (!src || !out_vertex || stride < 12) return false;
  if (out_forced_screen_like) *out_forced_screen_like = false;
  const bool has_xyzrhw = PositionHasRHW(fvf);
  const bool has_normal = (fvf & D3DFVF_NORMAL) != 0;
  const bool has_diffuse = (fvf & D3DFVF_DIFFUSE) != 0;
  const UINT tex_count = (fvf >> 8) & 0xFu;

  float vx = ReadUnaligned<float>(src + 0);
  float vy = ReadUnaligned<float>(src + 4);
  float vz = ReadUnaligned<float>(src + 8);
  const UINT position_bytes = PositionBytesFromFVF(fvf);
  size_t offset = position_bytes;
  if (position_bytes > stride) return false;

  D3DXVECTOR3 normal(0.0f, 0.0f, 1.0f);
  if (has_normal) {
    if (offset + 12 > stride) return false;
    normal.x = ReadUnaligned<float>(src + offset + 0);
    normal.y = ReadUnaligned<float>(src + offset + 4);
    normal.z = ReadUnaligned<float>(src + offset + 8);
    offset += 12;
  }

  uint32_t diffuse = 0xFFFFFFFFu;
  if (has_diffuse) {
    if (offset + 4 > stride) return false;
    diffuse = ReadUnaligned<uint32_t>(src + offset);
    offset += 4;
  }

  float tu0 = 0.0f;
  float tv0 = 0.0f;
  float tu1 = 0.0f;
  float tv1 = 0.0f;
  if (tex_count > 0 && offset + 8u <= stride) {
    tu0 = ReadUnaligned<float>(src + offset + 0);
    tv0 = ReadUnaligned<float>(src + offset + 4);
    if (tex_count > 1 && offset + 16u <= stride) {
      tu1 = ReadUnaligned<float>(src + offset + 8);
      tv1 = ReadUnaligned<float>(src + offset + 12);
    } else {
      tu1 = tu0;
      tv1 = tv0;
    }
  }

  const size_t tex_bytes = static_cast<size_t>(tex_count) * 8u;
  if (offset + tex_bytes > stride) {
    // Some legacy meshes have larger runtime stride than strict FVF decode path;
    // keep compatibility by not hard-failing here.
  }

  float clip_x = 0.0f;
  float clip_y = 0.0f;
  float clip_z = 0.0f;
  float clip_w = 1.0f;
  D3DXVECTOR3 cam_pos(vx, vy, vz);
  D3DXVECTOR3 cam_nrm(0.0f, 0.0f, 1.0f);

  if (!has_xyzrhw && fvf == 322u) {
    const float vp_w = std::max(1.0f, static_cast<float>(g_wasm_d3d9_state.viewport.Width));
    const float vp_h = std::max(1.0f, static_cast<float>(g_wasm_d3d9_state.viewport.Height));
    const bool screen_xy = (vx >= -64.0f && vx <= (vp_w + 64.0f)) &&
                           (vy >= -64.0f && vy <= (vp_h + 64.0f));
    const bool depth_01 = (vz >= -0.01f && vz <= 1.01f);
    if (screen_xy && depth_01) {
      g_fvf322_screenlike_vertices += 1;
      if (out_forced_screen_like) *out_forced_screen_like = true;
    }
  }

  if (has_xyzrhw || force_screen_space) {
    const float vp_w = std::max(1.0f, static_cast<float>(g_wasm_d3d9_state.viewport.Width));
    const float vp_h = std::max(1.0f, static_cast<float>(g_wasm_d3d9_state.viewport.Height));
    const float ndc_x = ((vx - static_cast<float>(g_wasm_d3d9_state.viewport.X)) / vp_w) * 2.0f - 1.0f;
    const float ndc_y = 1.0f - ((vy - static_cast<float>(g_wasm_d3d9_state.viewport.Y)) / vp_h) * 2.0f;
    const float ndc_z = vz * 2.0f - 1.0f;
    // FVF322 startup overlays can be emitted in viewport space without XYZRHW.
    // Replay them like projected quads only when the whole draw matches.
    clip_w = 1.0f;
    clip_x = ndc_x;
    clip_y = ndc_y;
    clip_z = ndc_z;
  } else {
    D3DXVECTOR3 in(vx, vy, vz);
    D3DXVECTOR4 clip{};
    D3DXVec3Transform(&clip, &in, &wvp);
    // Preserve clip-space and let GL perform clipping/perspective divide.
    clip_x = clip.x;
    clip_y = clip.y;
    // D3D clip-space z maps to [0,w], while GL expects [-w,w].
    clip_z = ((g_debug_ffp_flags & kDebugUseRawClipZ) != 0u) ? clip.z : (2.0f * clip.z - clip.w);
    clip_w = SafeClipW(clip.w);

    D3DXVec3TransformCoord(&cam_pos, &in, &world_view);

    D3DXVECTOR3 world_nrm{};
    world_nrm.x = normal.x * g_ffp_state.world[0]._11 + normal.y * g_ffp_state.world[0]._21 + normal.z * g_ffp_state.world[0]._31;
    world_nrm.y = normal.x * g_ffp_state.world[0]._12 + normal.y * g_ffp_state.world[0]._22 + normal.z * g_ffp_state.world[0]._32;
    world_nrm.z = normal.x * g_ffp_state.world[0]._13 + normal.y * g_ffp_state.world[0]._23 + normal.z * g_ffp_state.world[0]._33;
    cam_nrm.x = world_nrm.x * g_ffp_state.view._11 + world_nrm.y * g_ffp_state.view._21 + world_nrm.z * g_ffp_state.view._31;
    cam_nrm.y = world_nrm.x * g_ffp_state.view._12 + world_nrm.y * g_ffp_state.view._22 + world_nrm.z * g_ffp_state.view._32;
    cam_nrm.z = world_nrm.x * g_ffp_state.view._13 + world_nrm.y * g_ffp_state.view._23 + world_nrm.z * g_ffp_state.view._33;
    D3DXVec3Normalize(&cam_nrm, &cam_nrm);
  }

  out_vertex->x = clip_x;
  out_vertex->y = clip_y;
  out_vertex->z = clip_z;
  out_vertex->w = clip_w;
  out_vertex->u0 = tu0;
  out_vertex->v0 = tv0;
  out_vertex->u1 = tu1;
  out_vertex->v1 = tv1;
  out_vertex->cam_pos_x = cam_pos.x;
  out_vertex->cam_pos_y = cam_pos.y;
  out_vertex->cam_pos_z = cam_pos.z;
  out_vertex->cam_nrm_x = cam_nrm.x;
  out_vertex->cam_nrm_y = cam_nrm.y;
  out_vertex->cam_nrm_z = cam_nrm.z;
  ARGBToRGBA8(diffuse, &out_vertex->r, &out_vertex->g, &out_vertex->b, &out_vertex->a);
  if (fvf == 594u && ((g_debug_ffp_flags & kDebugForceFVF594White) != 0u)) {
    out_vertex->r = 255;
    out_vertex->g = 255;
    out_vertex->b = 255;
    out_vertex->a = 255;
  }
  if (has_diffuse &&
      out_vertex->a == 0 &&
      (out_vertex->r != 0 || out_vertex->g != 0 || out_vertex->b != 0) &&
      g_wasm_d3d9_state.alpha_test_enable != 0 &&
      g_wasm_d3d9_state.alpha_func != D3DCMP_ALWAYS) {
    // Only promote alpha on alpha-test passes where a zero alpha would
    // accidentally discard tinted geometry.
    out_vertex->a = 255;
  }
  if (!has_xyzrhw && !force_screen_space) {
    ApplyLegacyLightingToVertex(cam_pos, cam_nrm, has_diffuse, ColorValueFromARGB(diffuse), out_vertex);
  }
  return true;
}

bool IsFVF322ScreenlikeRawVertex(const uint8_t* src, UINT stride) {
  if (!src || stride < 12u) return false;
  const float vx = ReadUnaligned<float>(src + 0);
  const float vy = ReadUnaligned<float>(src + 4);
  const float vz = ReadUnaligned<float>(src + 8);
  const float vp_w = std::max(1.0f, static_cast<float>(g_wasm_d3d9_state.viewport.Width));
  const float vp_h = std::max(1.0f, static_cast<float>(g_wasm_d3d9_state.viewport.Height));
  const bool screen_xy = (vx >= -64.0f && vx <= (vp_w + 64.0f)) &&
                         (vy >= -64.0f && vy <= (vp_h + 64.0f));
  const bool depth_01 = (vz >= -0.01f && vz <= 1.01f);
  return screen_xy && depth_01;
}

bool IsFVF322NearScreenlikeRawVertex(const uint8_t* src, UINT stride) {
  if (!src || stride < 12u) return false;
  const float vx = ReadUnaligned<float>(src + 0);
  const float vy = ReadUnaligned<float>(src + 4);
  const float vz = ReadUnaligned<float>(src + 8);
  const float vp_w = std::max(1.0f, static_cast<float>(g_wasm_d3d9_state.viewport.Width));
  const float vp_h = std::max(1.0f, static_cast<float>(g_wasm_d3d9_state.viewport.Height));
  const float x_margin = std::max(160.0f, vp_w * 0.5f);
  const float y_margin = std::max(160.0f, vp_h * 0.5f);
  const bool screen_xy = (vx >= -x_margin && vx <= (vp_w + x_margin)) &&
                         (vy >= -y_margin && vy <= (vp_h + y_margin));
  const bool depth_01 = (vz >= -0.25f && vz <= 1.25f);
  return screen_xy && depth_01;
}

std::vector<uint32_t> BuildWireframeTriangleListIndices(size_t vertex_count, const std::vector<uint32_t>* indices) {
  std::vector<uint32_t> lines;
  const size_t source_count = indices ? indices->size() : vertex_count;
  lines.reserve((source_count / 3u) * 6u);
  for (size_t i = 0; i + 2u < source_count; i += 3u) {
    const uint32_t i0 = indices ? (*indices)[i + 0u] : static_cast<uint32_t>(i + 0u);
    const uint32_t i1 = indices ? (*indices)[i + 1u] : static_cast<uint32_t>(i + 1u);
    const uint32_t i2 = indices ? (*indices)[i + 2u] : static_cast<uint32_t>(i + 2u);
    lines.push_back(i0);
    lines.push_back(i1);
    lines.push_back(i1);
    lines.push_back(i2);
    lines.push_back(i2);
    lines.push_back(i0);
  }
  return lines;
}

bool VertexHasStableClipW(const FFPVertex& v) {
  if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) || !std::isfinite(v.w)) return false;
  return v.w > 1.0e-5f;
}

FFPVertex LerpFFPVertex(const FFPVertex& a, const FFPVertex& b, float t) {
  t = std::max(0.0f, std::min(1.0f, t));
  FFPVertex out{};
  auto lerpf = [&](float av, float bv) -> float { return av + (bv - av) * t; };
  out.x = lerpf(a.x, b.x);
  out.y = lerpf(a.y, b.y);
  out.z = lerpf(a.z, b.z);
  out.w = lerpf(a.w, b.w);
  out.u0 = lerpf(a.u0, b.u0);
  out.v0 = lerpf(a.v0, b.v0);
  out.u1 = lerpf(a.u1, b.u1);
  out.v1 = lerpf(a.v1, b.v1);
  out.cam_pos_x = lerpf(a.cam_pos_x, b.cam_pos_x);
  out.cam_pos_y = lerpf(a.cam_pos_y, b.cam_pos_y);
  out.cam_pos_z = lerpf(a.cam_pos_z, b.cam_pos_z);
  out.cam_nrm_x = lerpf(a.cam_nrm_x, b.cam_nrm_x);
  out.cam_nrm_y = lerpf(a.cam_nrm_y, b.cam_nrm_y);
  out.cam_nrm_z = lerpf(a.cam_nrm_z, b.cam_nrm_z);
  out.r = static_cast<uint8_t>(std::round(lerpf(static_cast<float>(a.r), static_cast<float>(b.r))));
  out.g = static_cast<uint8_t>(std::round(lerpf(static_cast<float>(a.g), static_cast<float>(b.g))));
  out.b = static_cast<uint8_t>(std::round(lerpf(static_cast<float>(a.b), static_cast<float>(b.b))));
  out.a = static_cast<uint8_t>(std::round(lerpf(static_cast<float>(a.a), static_cast<float>(b.a))));
  return out;
}

float ClipPlaneEval(const FFPVertex& v, int plane) {
  switch (plane) {
    case 0: return v.w - 1.0e-5f;   // w >= epsilon
    case 1: return v.x + v.w;       // left
    case 2: return -v.x + v.w;      // right
    case 3: return v.y + v.w;       // bottom
    case 4: return -v.y + v.w;      // top
    case 5: return v.z + v.w;       // near
    case 6: return -v.z + v.w;      // far
    default: return -1.0f;
  }
}

bool VertexInsideClipVolume(const FFPVertex& v, int plane) {
  if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) || !std::isfinite(v.w)) return false;
  return ClipPlaneEval(v, plane) >= 0.0f;
}

bool VertexFullyInsideClipVolume(const FFPVertex& v) {
  if (!VertexHasStableClipW(v)) return false;
  for (int plane = 1; plane < 7; ++plane) {
    if (!VertexInsideClipVolume(v, plane)) return false;
  }
  return true;
}

FFPVertex IntersectEdgeWithClipPlane(const FFPVertex& a, const FFPVertex& b, int plane) {
  const float fa = ClipPlaneEval(a, plane);
  const float fb = ClipPlaneEval(b, plane);
  const float denom = (fa - fb);
  float t = 0.0f;
  if (std::fabs(denom) > 1.0e-7f) t = fa / denom;
  FFPVertex out = LerpFFPVertex(a, b, t);
  if (!std::isfinite(out.x) || !std::isfinite(out.y) || !std::isfinite(out.z) || !std::isfinite(out.w)) {
    out = a;
  }
  if (!std::isfinite(out.w) || out.w <= 1.0e-5f) out.w = 1.0e-5f;
  return out;
}

bool ClipTriangleToClipVolume(
    const FFPVertex& v0,
    const FFPVertex& v1,
    const FFPVertex& v2,
    std::vector<FFPVertex>* out_poly) {
  if (!out_poly) return false;
  out_poly->clear();
  std::vector<FFPVertex> poly;
  std::vector<FFPVertex> tmp;
  poly.reserve(8);
  tmp.reserve(8);
  poly.push_back(v0);
  poly.push_back(v1);
  poly.push_back(v2);

  for (int plane = 0; plane < 7; ++plane) {
    if (poly.empty()) return false;
    tmp.clear();
    const size_t n = poly.size();
    for (size_t i = 0; i < n; ++i) {
      const FFPVertex& a = poly[i];
      const FFPVertex& b = poly[(i + 1u) % n];
      const bool a_in = VertexInsideClipVolume(a, plane);
      const bool b_in = VertexInsideClipVolume(b, plane);
      if (a_in && b_in) {
        tmp.push_back(b);
      } else if (a_in && !b_in) {
        tmp.push_back(IntersectEdgeWithClipPlane(a, b, plane));
      } else if (!a_in && b_in) {
        tmp.push_back(IntersectEdgeWithClipPlane(a, b, plane));
        tmp.push_back(b);
      }
    }
    poly.swap(tmp);
  }

  if (poly.size() < 3u) return false;
  *out_poly = std::move(poly);
  return true;
}

bool TrianglePassesClipWReject(const std::vector<FFPVertex>& vertices, uint32_t i0, uint32_t i1, uint32_t i2) {
  if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) return false;
  return VertexHasStableClipW(vertices[i0]) && VertexHasStableClipW(vertices[i1]) && VertexHasStableClipW(vertices[i2]);
}

bool BuildClipWClippedTriangles(
    GLenum mode,
    const std::vector<FFPVertex>& vertices,
    const std::vector<uint32_t>* indices,
    std::vector<FFPVertex>* out_vertices,
    std::vector<uint32_t>* out_indices,
    bool force_clip_triangle_repair = false) {
  if (!out_vertices || !out_indices) return false;
  *out_vertices = vertices;
  out_indices->clear();
  bool rejected_any = false;
  uint64_t kept = 0;
  uint64_t rejected = 0;

  auto idx_at = [&](size_t i) -> uint32_t {
    return indices ? (*indices)[i] : static_cast<uint32_t>(i);
  };

  const bool enable_clip_triangle_repair =
      force_clip_triangle_repair || ((g_debug_ffp_flags & kDebugClipTriangleRepair) != 0u);

  auto vertex_fully_inside = [&](uint32_t idx) -> bool {
    if (idx >= vertices.size()) return false;
    const FFPVertex& v = vertices[idx];
    if (!VertexHasStableClipW(v)) return false;
    for (int plane = 1; plane < 7; ++plane) {
      if (!VertexInsideClipVolume(v, plane)) return false;
    }
    return true;
  };

  auto emit_triangle = [&](uint32_t ia, uint32_t ib, uint32_t ic) {
    if (!enable_clip_triangle_repair) {
      if (!TrianglePassesClipWReject(vertices, ia, ib, ic)) {
        rejected_any = true;
        rejected += 1;
        return;
      }
      out_indices->push_back(ia);
      out_indices->push_back(ib);
      out_indices->push_back(ic);
      kept += 1;
      return;
    }

    if (ia >= vertices.size() || ib >= vertices.size() || ic >= vertices.size()) {
      rejected_any = true;
      rejected += 1;
      return;
    }

    const bool fully_inside =
        vertex_fully_inside(ia) &&
        vertex_fully_inside(ib) &&
        vertex_fully_inside(ic);
    if (fully_inside) {
      out_indices->push_back(ia);
      out_indices->push_back(ib);
      out_indices->push_back(ic);
      kept += 1;
      return;
    }

    std::vector<FFPVertex> clipped_poly;
    if (!ClipTriangleToClipVolume(vertices[ia], vertices[ib], vertices[ic], &clipped_poly)) {
      rejected_any = true;
      rejected += 1;
      return;
    }

    rejected_any = true;
    const uint32_t base = static_cast<uint32_t>(out_vertices->size());
    out_vertices->insert(out_vertices->end(), clipped_poly.begin(), clipped_poly.end());
    for (size_t i = 1; i + 1 < clipped_poly.size(); ++i) {
      out_indices->push_back(base);
      out_indices->push_back(base + static_cast<uint32_t>(i));
      out_indices->push_back(base + static_cast<uint32_t>(i + 1));
      kept += 1;
    }
  };

  const size_t count = indices ? indices->size() : vertices.size();
  if (mode == GL_TRIANGLES) {
    for (size_t i = 0; i + 2u < count; i += 3u) {
      emit_triangle(idx_at(i + 0u), idx_at(i + 1u), idx_at(i + 2u));
    }
  } else if (mode == GL_TRIANGLE_STRIP) {
    for (size_t i = 0; i + 2u < count; ++i) {
      const uint32_t a = idx_at(i + 0u);
      const uint32_t b = idx_at(i + 1u);
      const uint32_t c = idx_at(i + 2u);
      if ((i & 1u) == 0u) emit_triangle(a, b, c);
      else emit_triangle(b, a, c);
    }
  } else if (mode == GL_TRIANGLE_FAN) {
    if (count >= 3u) {
      const uint32_t base = idx_at(0u);
      for (size_t i = 1u; i + 1u < count; ++i) {
        emit_triangle(base, idx_at(i), idx_at(i + 1u));
      }
    }
  } else {
    return false;
  }

  g_clip_w_reject_triangles += rejected;
  g_clip_w_keep_triangles += kept;
  return rejected_any;
}

void TrackSkyClipStats(
    GLenum mode,
    const std::vector<FFPVertex>& vertices,
    const std::vector<uint32_t>* indices) {
  g_sky_clip_draws += 1;
  g_sky_clip_last_vertex_count = static_cast<uint32_t>(std::min<size_t>(vertices.size(), UINT32_MAX));
  g_sky_clip_last_index_count = static_cast<uint32_t>(std::min<size_t>(indices ? indices->size() : 0u, UINT32_MAX));
  g_sky_clip_last_stable_w_vertices = 0;
  g_sky_clip_last_negative_w_vertices = 0;
  g_sky_clip_last_near_w_vertices = 0;
  g_sky_clip_last_inside_vertices = 0;
  g_sky_clip_last_large_ndc_vertices = 0;
  g_sky_clip_last_triangle_count = 0;
  g_sky_clip_last_triangles_all_stable_w = 0;
  g_sky_clip_last_triangles_any_unstable_w = 0;
  g_sky_clip_last_triangles_any_outside = 0;

  bool have_vertex = false;
  auto update_range = [&](float w, float ndc_x, float ndc_y, float ndc_z) {
    if (!std::isfinite(w) || !std::isfinite(ndc_x) || !std::isfinite(ndc_y) || !std::isfinite(ndc_z)) return;
    if (!have_vertex) {
      g_sky_clip_last_min_w = g_sky_clip_last_max_w = w;
      g_sky_clip_last_min_ndc_x = g_sky_clip_last_max_ndc_x = ndc_x;
      g_sky_clip_last_min_ndc_y = g_sky_clip_last_max_ndc_y = ndc_y;
      g_sky_clip_last_min_ndc_z = g_sky_clip_last_max_ndc_z = ndc_z;
      have_vertex = true;
      return;
    }
    g_sky_clip_last_min_w = std::min(g_sky_clip_last_min_w, w);
    g_sky_clip_last_max_w = std::max(g_sky_clip_last_max_w, w);
    g_sky_clip_last_min_ndc_x = std::min(g_sky_clip_last_min_ndc_x, ndc_x);
    g_sky_clip_last_max_ndc_x = std::max(g_sky_clip_last_max_ndc_x, ndc_x);
    g_sky_clip_last_min_ndc_y = std::min(g_sky_clip_last_min_ndc_y, ndc_y);
    g_sky_clip_last_max_ndc_y = std::max(g_sky_clip_last_max_ndc_y, ndc_y);
    g_sky_clip_last_min_ndc_z = std::min(g_sky_clip_last_min_ndc_z, ndc_z);
    g_sky_clip_last_max_ndc_z = std::max(g_sky_clip_last_max_ndc_z, ndc_z);
  };

  for (const FFPVertex& v : vertices) {
    if (VertexHasStableClipW(v)) g_sky_clip_last_stable_w_vertices += 1;
    if (v.w < 0.0f) g_sky_clip_last_negative_w_vertices += 1;
    if (std::fabs(v.w) <= 1.0e-5f) g_sky_clip_last_near_w_vertices += 1;
    if (VertexFullyInsideClipVolume(v)) g_sky_clip_last_inside_vertices += 1;

    const float safe_w = SafeClipW(v.w);
    const float ndc_x = v.x / safe_w;
    const float ndc_y = v.y / safe_w;
    const float ndc_z = v.z / safe_w;
    if (std::fabs(ndc_x) > 10.0f || std::fabs(ndc_y) > 10.0f || std::fabs(ndc_z) > 10.0f) {
      g_sky_clip_last_large_ndc_vertices += 1;
    }
    update_range(v.w, ndc_x, ndc_y, ndc_z);
  }

  if (!have_vertex) {
    g_sky_clip_last_min_w = g_sky_clip_last_max_w = 0.0f;
    g_sky_clip_last_min_ndc_x = g_sky_clip_last_max_ndc_x = 0.0f;
    g_sky_clip_last_min_ndc_y = g_sky_clip_last_max_ndc_y = 0.0f;
    g_sky_clip_last_min_ndc_z = g_sky_clip_last_max_ndc_z = 0.0f;
  }

  const size_t count = indices ? indices->size() : vertices.size();
  auto idx_at = [&](size_t i) -> uint32_t {
    return indices ? (*indices)[i] : static_cast<uint32_t>(i);
  };
  auto track_triangle = [&](uint32_t ia, uint32_t ib, uint32_t ic) {
    g_sky_clip_last_triangle_count += 1;
    if (ia >= vertices.size() || ib >= vertices.size() || ic >= vertices.size()) {
      g_sky_clip_last_triangles_any_unstable_w += 1;
      g_sky_clip_last_triangles_any_outside += 1;
      return;
    }
    const bool a_stable = VertexHasStableClipW(vertices[ia]);
    const bool b_stable = VertexHasStableClipW(vertices[ib]);
    const bool c_stable = VertexHasStableClipW(vertices[ic]);
    if (a_stable && b_stable && c_stable) g_sky_clip_last_triangles_all_stable_w += 1;
    else g_sky_clip_last_triangles_any_unstable_w += 1;

    const bool fully_inside =
        VertexFullyInsideClipVolume(vertices[ia]) &&
        VertexFullyInsideClipVolume(vertices[ib]) &&
        VertexFullyInsideClipVolume(vertices[ic]);
    if (!fully_inside) g_sky_clip_last_triangles_any_outside += 1;
  };

  if (mode == GL_TRIANGLES) {
    for (size_t i = 0; i + 2u < count; i += 3u) {
      track_triangle(idx_at(i + 0u), idx_at(i + 1u), idx_at(i + 2u));
    }
  } else if (mode == GL_TRIANGLE_STRIP) {
    for (size_t i = 0; i + 2u < count; ++i) {
      const uint32_t a = idx_at(i + 0u);
      const uint32_t b = idx_at(i + 1u);
      const uint32_t c = idx_at(i + 2u);
      if ((i & 1u) == 0u) track_triangle(a, b, c);
      else track_triangle(b, a, c);
    }
  } else if (mode == GL_TRIANGLE_FAN && count >= 3u) {
    const uint32_t base = idx_at(0u);
    for (size_t i = 1u; i + 1u < count; ++i) {
      track_triangle(base, idx_at(i), idx_at(i + 1u));
    }
  }
}

void TrackFVF530DrawStats(const std::vector<FFPVertex>& vertices, const std::vector<uint32_t>* indices) {
  g_fvf530_draws += 1;
  g_fvf530_last_vertex_count = static_cast<uint32_t>(std::min<size_t>(vertices.size(), UINT32_MAX));
  g_fvf530_last_index_count = static_cast<uint32_t>(std::min<size_t>(indices ? indices->size() : 0u, UINT32_MAX));
  g_fvf530_last_stable_w_vertices = 0;
  g_fvf530_last_inside_vertices = 0;
  g_fvf530_last_large_ndc_vertices = 0;

  bool have_vertex = false;
  bool unstable_w = false;
  auto update_range = [&](float ndc_x, float ndc_y, float ndc_z) {
    if (!std::isfinite(ndc_x) || !std::isfinite(ndc_y) || !std::isfinite(ndc_z)) return;
    if (!have_vertex) {
      g_fvf530_last_min_ndc_x = g_fvf530_last_max_ndc_x = ndc_x;
      g_fvf530_last_min_ndc_y = g_fvf530_last_max_ndc_y = ndc_y;
      g_fvf530_last_min_ndc_z = g_fvf530_last_max_ndc_z = ndc_z;
      have_vertex = true;
      return;
    }
    g_fvf530_last_min_ndc_x = std::min(g_fvf530_last_min_ndc_x, ndc_x);
    g_fvf530_last_max_ndc_x = std::max(g_fvf530_last_max_ndc_x, ndc_x);
    g_fvf530_last_min_ndc_y = std::min(g_fvf530_last_min_ndc_y, ndc_y);
    g_fvf530_last_max_ndc_y = std::max(g_fvf530_last_max_ndc_y, ndc_y);
    g_fvf530_last_min_ndc_z = std::min(g_fvf530_last_min_ndc_z, ndc_z);
    g_fvf530_last_max_ndc_z = std::max(g_fvf530_last_max_ndc_z, ndc_z);
  };

  for (const FFPVertex& v : vertices) {
    g_fvf530_vertices += 1;
    if (VertexHasStableClipW(v)) g_fvf530_last_stable_w_vertices += 1;
    else unstable_w = true;
    if (VertexFullyInsideClipVolume(v)) {
      g_fvf530_last_inside_vertices += 1;
      g_fvf530_inside_vertices += 1;
    }
    const float safe_w = SafeClipW(v.w);
    const float ndc_x = v.x / safe_w;
    const float ndc_y = v.y / safe_w;
    const float ndc_z = v.z / safe_w;
    if (std::fabs(ndc_x) > 10.0f || std::fabs(ndc_y) > 10.0f || std::fabs(ndc_z) > 10.0f) {
      g_fvf530_last_large_ndc_vertices += 1;
    }
    update_range(ndc_x, ndc_y, ndc_z);
  }

  if (!have_vertex) {
    g_fvf530_last_min_ndc_x = g_fvf530_last_max_ndc_x = 0.0f;
    g_fvf530_last_min_ndc_y = g_fvf530_last_max_ndc_y = 0.0f;
    g_fvf530_last_min_ndc_z = g_fvf530_last_max_ndc_z = 0.0f;
  }
  if (unstable_w) g_fvf530_unstable_w_draws += 1;
  const float width = g_fvf530_last_max_ndc_x - g_fvf530_last_min_ndc_x;
  const float height = g_fvf530_last_max_ndc_y - g_fvf530_last_min_ndc_y;
  if (width > 3.0f || height > 3.0f || g_fvf530_last_large_ndc_vertices > 0) {
    g_fvf530_large_bounds_draws += 1;
    auto* stage0 = AsTexture(g_ffp_state.textures[0]);
    auto* stage1 = AsTexture(g_ffp_state.textures[1]);
    const std::string stage0_path = stage0 ? stage0->debug_source_path : "";
    const std::string stage1_path = stage1 ? stage1->debug_source_path : "";
    char sample[768]{};
    std::snprintf(
        sample,
        sizeof(sample),
        "draw=%llu stage0=%s stage1=%s vertices=%u indices=%u stableW=%u inside=%u largeNdc=%u ndcX=%.3f..%.3f ndcY=%.3f..%.3f ndcZ=%.3f..%.3f",
        static_cast<unsigned long long>(g_fvf530_draws),
        stage0_path.empty() ? "(none)" : stage0_path.c_str(),
        stage1_path.empty() ? "(none)" : stage1_path.c_str(),
        g_fvf530_last_vertex_count,
        g_fvf530_last_index_count,
        g_fvf530_last_stable_w_vertices,
        g_fvf530_last_inside_vertices,
        g_fvf530_last_large_ndc_vertices,
        g_fvf530_last_min_ndc_x,
        g_fvf530_last_max_ndc_x,
        g_fvf530_last_min_ndc_y,
        g_fvf530_last_max_ndc_y,
        g_fvf530_last_min_ndc_z,
        g_fvf530_last_max_ndc_z);
    if (g_fvf530_large_bound_samples.size() < 64u &&
        g_fvf530_large_bound_seen.insert(sample).second) {
      g_fvf530_large_bound_samples.emplace_back(sample);
    }
  }
}

bool ShouldSkipFVF530LargeBoundsDraw() {
  if ((g_debug_ffp_flags & kDebugDisableFVF530LargeBoundsSkip) != 0u) return false;
  if (g_fvf530_last_inside_vertices != 0u) return false;
  if (g_fvf530_last_large_ndc_vertices == 0u) return false;
  const float width = g_fvf530_last_max_ndc_x - g_fvf530_last_min_ndc_x;
  const float height = g_fvf530_last_max_ndc_y - g_fvf530_last_min_ndc_y;
  return width > 8.0f || height > 8.0f;
}

void BuildSkyScreenQuad(std::vector<FFPVertex>* out_vertices, std::vector<uint32_t>* out_indices) {
  if (!out_vertices || !out_indices) return;
  out_vertices->clear();
  out_indices->clear();

  constexpr float bottom_y = -1.0f;  // full-viewport background fill
  const auto make_vertex = [](float x, float y, float u, float v) {
    FFPVertex out{};
    out.x = x;
    out.y = y;
    out.z = 0.999f;
    out.w = 1.0f;
    out.u0 = u;
    out.v0 = v;
    out.u1 = u;
    out.v1 = v;
    out.cam_pos_x = 0.0f;
    out.cam_pos_y = 0.0f;
    out.cam_pos_z = 1.0f;
    out.cam_nrm_x = 0.0f;
    out.cam_nrm_y = 0.0f;
    out.cam_nrm_z = 1.0f;
    out.r = 128;
    out.g = 128;
    out.b = 128;
    out.a = 255;
    return out;
  };

  out_vertices->push_back(make_vertex(-1.0f, 1.0f, 0.0f, 0.0f));
  out_vertices->push_back(make_vertex(1.0f, 1.0f, 1.0f, 0.0f));
  out_vertices->push_back(make_vertex(1.0f, bottom_y, 1.0f, 1.0f));
  out_vertices->push_back(make_vertex(-1.0f, bottom_y, 0.0f, 1.0f));
  out_indices->assign({0u, 1u, 2u, 0u, 2u, 3u});
}

bool UploadAndDraw(GLenum gl_mode, const std::vector<FFPVertex>& vertices, const std::vector<uint32_t>* indices) {
  if (!EnsureWasmContext()) return false;
  if (!EnsureFFPProgram()) return false;
  if (vertices.empty()) return true;

  ApplyViewport();
  bool depth_test_enabled =
      g_wasm_d3d9_state.z_enable && ((g_debug_ffp_flags & kDebugDisableDepthTest) == 0u);
  bool depth_write_enabled =
      g_wasm_d3d9_state.z_write_enable && ((g_debug_ffp_flags & kDebugDisableDepthWrite) == 0u);
  const DWORD active_fvf = g_ffp_state.current_fvf;
  const bool marked_sky_draw = g_mark_next_draw_sky;
  g_mark_next_draw_sky = false;
  const bool current_sky_texture_draw = marked_sky_draw || CurrentDrawUsesSkyTexture();
  const uint64_t draw_order_serial = ++g_draw_order_serial;
  TrackDrawOrder(draw_order_serial, active_fvf, current_sky_texture_draw);
  TrackFvf322ClassDraw(active_fvf);
  TrackSkinSuspiciousTexture(active_fvf, draw_order_serial);

  if (active_fvf == 594u && ((g_debug_ffp_flags & kDebugTerrainDepthTestOff) != 0u)) {
    depth_test_enabled = false;
  }
  const bool legacy_depth_guard_enabled =
      ((g_debug_ffp_flags & kDebugEnableLegacyDepthWriteGuard) != 0u) &&
      ((g_debug_ffp_flags & kDebugDisableDepthWriteGuard) == 0u);
  if (legacy_depth_guard_enabled) {
    const bool skin_depth_guard =
        ActiveFvfIsSkin(active_fvf) &&
        ((g_debug_ffp_flags & kDebugGuardSkinDepthWrite) == 0u);
    bool guard_depth_for_fvf =
        (active_fvf == 322u ||
         active_fvf == 530u ||
         active_fvf == 594u ||
         skin_depth_guard);
    if ((g_debug_ffp_flags & kDebugDepthGuardOnly322) != 0u) guard_depth_for_fvf = (active_fvf == 322u);
    if ((g_debug_ffp_flags & kDebugDepthGuardOnly594) != 0u) guard_depth_for_fvf = (active_fvf == 594u);
    if (depth_write_enabled && guard_depth_for_fvf) {
      depth_write_enabled = false;
      g_depth_write_guard_forced_draws += 1;
    }
  }
  if (active_fvf == 322u) {
    const uint32_t fvf322_class = ClassifyFvf322Draw();
    if (fvf322_class == kFvf322ClassSky) {
      depth_test_enabled = false;
    }
    depth_write_enabled = false;
  }
  if (active_fvf == 324u && !depth_write_enabled) {
    depth_test_enabled = false;
  }
  TrackDepthWriteFVF(active_fvf, depth_write_enabled);
  if (current_sky_texture_draw && ((g_debug_ffp_flags & kDebugSkipSkyDraw) != 0u)) {
    TrackSkyClipStats(gl_mode, vertices, indices);
    return true;
  }
  const bool replace_sky_with_screen_quad =
      current_sky_texture_draw &&
      active_fvf == 322u &&
      ((g_debug_ffp_flags & kDebugEnableSkyScreenQuad) != 0u);
  if (depth_test_enabled && !replace_sky_with_screen_quad) glEnable(GL_DEPTH_TEST);
  else glDisable(GL_DEPTH_TEST);
  glDepthMask((depth_write_enabled && !replace_sky_with_screen_quad) ? GL_TRUE : GL_FALSE);
  const bool blend_enabled =
      g_wasm_d3d9_state.blend_enabled && ((g_debug_ffp_flags & kDebugDisableBlend) == 0u);
  const bool effective_depth_test_enabled = depth_test_enabled && !replace_sky_with_screen_quad;
  const bool effective_depth_write_enabled = depth_write_enabled && !replace_sky_with_screen_quad;
  const bool effective_blend_enabled = blend_enabled && !marked_sky_draw && !replace_sky_with_screen_quad;
  if (marked_sky_draw || replace_sky_with_screen_quad) {
    glDisable(GL_BLEND);
  } else if (blend_enabled) {
    glEnable(GL_BLEND);
    ApplyBlendState();
  } else {
    glDisable(GL_BLEND);
  }
  D3DMATRIX world_view{};
  D3DXMatrixMultiply(
      reinterpret_cast<D3DXMATRIX*>(&world_view),
      reinterpret_cast<const D3DXMATRIX*>(&g_ffp_state.world[0]),
      reinterpret_cast<const D3DXMATRIX*>(&g_ffp_state.view));
  const bool mirrored_world_view = IsMirroredMatrix3x3(world_view);
  const bool cull_active =
      !marked_sky_draw &&
      !replace_sky_with_screen_quad &&
      ((g_debug_ffp_flags & kDebugDisableCull) == 0u) &&
      (g_wasm_d3d9_state.cull_mode != D3DCULL_NONE);
  if (mirrored_world_view) {
    g_cull_mirror_worldview_draws += 1;
    if (cull_active) g_cull_frontface_flipped_draws += 1;
  }
  glUseProgram(g_ffp_program.program);
  if (marked_sky_draw || replace_sky_with_screen_quad) glDisable(GL_CULL_FACE);
  else ApplyCullState(mirrored_world_view);

  const DWORD saved_stage0_color_op = StageStateValue(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
  const DWORD saved_stage0_color_arg1 = StageStateValue(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
  const DWORD saved_stage0_color_arg2 = StageStateValue(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
  const DWORD saved_stage0_alpha_op = StageStateValue(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
  const DWORD saved_stage0_alpha_arg1 = StageStateValue(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
  if (marked_sky_draw) {
    g_ffp_state.tex_stage[0][D3DTSS_COLOROP] = D3DTOP_ADDSIGNED;
    g_ffp_state.tex_stage[0][D3DTSS_COLORARG1] = D3DTA_TEXTURE;
    g_ffp_state.tex_stage[0][D3DTSS_COLORARG2] = D3DTA_DIFFUSE;
    g_ffp_state.tex_stage[0][D3DTSS_ALPHAOP] = D3DTOP_SELECTARG1;
    g_ffp_state.tex_stage[0][D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
  }
  const DWORD stage0_color_op = marked_sky_draw ? D3DTOP_ADDSIGNED : saved_stage0_color_op;
  const DWORD stage0_alpha_op = marked_sky_draw ? D3DTOP_SELECTARG1 : saved_stage0_alpha_op;
  const bool sky_like_fvf322_draw =
      marked_sky_draw ||
      (active_fvf == 322u &&
       stage0_color_op == D3DTOP_ADDSIGNED &&
       g_wasm_d3d9_state.fog_enable == 0);

  const bool has_stage0_texture = BindTextureStage(0);
  const bool has_stage1_texture = BindTextureStage(1);
  if (stage0_color_op == D3DTOP_ADDSIGNED) {
    auto* stage0_texture = AsTexture(g_ffp_state.textures[0]);
    g_stage0_colorop8_draws += 1;
    g_stage0_colorop8_last_fvf = active_fvf;
    if (has_stage0_texture) g_stage0_colorop8_with_texture += 1;
    else g_stage0_colorop8_without_texture += 1;
    if (stage0_texture) {
      g_stage0_colorop8_last_width = stage0_texture->width;
      g_stage0_colorop8_last_height = stage0_texture->height;
      g_stage0_colorop8_last_path_len = static_cast<uint32_t>(stage0_texture->debug_source_path.size());
      if (stage0_texture->debug_source_path.empty()) g_stage0_colorop8_pathless_texture += 1;
    } else {
      g_stage0_colorop8_last_width = 0;
      g_stage0_colorop8_last_height = 0;
      g_stage0_colorop8_last_path_len = 0;
    }
  }
  TrackSpecialTextureDraws(active_fvf);
  if (active_fvf == 530u) {
    TrackFVF530DrawStats(vertices, indices);
    if (ShouldSkipFVF530LargeBoundsDraw()) {
      g_fvf530_large_bounds_skip_draws += 1;
      RecordDrawTrace(
          gl_mode,
          vertices,
          indices,
          active_fvf,
          effective_depth_test_enabled,
          effective_depth_write_enabled,
          effective_blend_enabled,
          false,
          true,
          "fvf530-large-bounds");
      return true;
    }
  }
  if (current_sky_texture_draw) TrackSkyClipStats(gl_mode, vertices, indices);
  if (marked_sky_draw) g_texture_draws_sky += 1;
  if (active_fvf == 322u) {
    if (has_stage0_texture) g_fvf322_with_stage0_texture += 1;
    else g_fvf322_without_stage0_texture += 1;
  }
  if (g_ffp_program.uni_sampler0 >= 0) glUniform1i(g_ffp_program.uni_sampler0, 0);
  if (g_ffp_program.uni_sampler1 >= 0) glUniform1i(g_ffp_program.uni_sampler1, 1);
  ApplyFFPUniforms(has_stage0_texture, has_stage1_texture);
  if (marked_sky_draw) {
    g_ffp_state.tex_stage[0][D3DTSS_COLOROP] = saved_stage0_color_op;
    g_ffp_state.tex_stage[0][D3DTSS_COLORARG1] = saved_stage0_color_arg1;
    g_ffp_state.tex_stage[0][D3DTSS_COLORARG2] = saved_stage0_color_arg2;
    g_ffp_state.tex_stage[0][D3DTSS_ALPHAOP] = saved_stage0_alpha_op;
    g_ffp_state.tex_stage[0][D3DTSS_ALPHAARG1] = saved_stage0_alpha_arg1;
  }

  const bool alpha_test_enabled =
      ((g_debug_ffp_flags & kDebugDisableAlphaTest) == 0u) &&
      (g_wasm_d3d9_state.alpha_test_enable != 0) &&
      (g_wasm_d3d9_state.alpha_func != D3DCMP_ALWAYS);
  if (alpha_test_enabled) g_draw_alpha_test_enabled += 1;
  else g_draw_alpha_test_disabled += 1;
  if (blend_enabled) g_draw_blend_enabled += 1;
  if (!depth_test_enabled) g_draw_depth_test_disabled += 1;
  if (!depth_write_enabled) g_draw_depth_write_disabled += 1;
  if (g_wasm_d3d9_state.lighting_enable != 0) g_draw_lighting_enabled += 1;
  if (g_wasm_d3d9_state.fog_enable != 0 && g_wasm_d3d9_state.fog_vertex_mode != D3DFOG_NONE) g_draw_fog_enabled += 1;
  if ((g_debug_ffp_flags & kDebugDisableCull) != 0u || g_wasm_d3d9_state.cull_mode == D3DCULL_NONE) g_draw_cull_none += 1;
  else if (g_wasm_d3d9_state.cull_mode == D3DCULL_CW) g_draw_cull_cw += 1;
  else g_draw_cull_ccw += 1;
  if (stage0_color_op == D3DTOP_SELECTARG1) g_draw_stage0_color_selectarg1 += 1;
  if (stage0_color_op == D3DTOP_MODULATE) g_draw_stage0_color_modulate += 1;
  if (stage0_alpha_op == D3DTOP_SELECTARG1) g_draw_stage0_alpha_selectarg1 += 1;
  if (stage0_alpha_op == D3DTOP_MODULATE) g_draw_stage0_alpha_modulate += 1;

  glBindBuffer(GL_ARRAY_BUFFER, g_ffp_program.vbo);

  glEnableVertexAttribArray(static_cast<GLuint>(g_ffp_program.attr_pos));
  glEnableVertexAttribArray(static_cast<GLuint>(g_ffp_program.attr_uv0));
  glEnableVertexAttribArray(static_cast<GLuint>(g_ffp_program.attr_uv1));
  glEnableVertexAttribArray(static_cast<GLuint>(g_ffp_program.attr_cam_pos));
  glEnableVertexAttribArray(static_cast<GLuint>(g_ffp_program.attr_cam_nrm));
  glEnableVertexAttribArray(static_cast<GLuint>(g_ffp_program.attr_color));

  glVertexAttribPointer(
      static_cast<GLuint>(g_ffp_program.attr_pos),
      4,
      GL_FLOAT,
      GL_FALSE,
      sizeof(FFPVertex),
      reinterpret_cast<const void*>(offsetof(FFPVertex, x)));
  glVertexAttribPointer(
      static_cast<GLuint>(g_ffp_program.attr_uv0),
      2,
      GL_FLOAT,
      GL_FALSE,
      sizeof(FFPVertex),
      reinterpret_cast<const void*>(offsetof(FFPVertex, u0)));
  glVertexAttribPointer(
      static_cast<GLuint>(g_ffp_program.attr_uv1),
      2,
      GL_FLOAT,
      GL_FALSE,
      sizeof(FFPVertex),
      reinterpret_cast<const void*>(offsetof(FFPVertex, u1)));
  glVertexAttribPointer(
      static_cast<GLuint>(g_ffp_program.attr_cam_pos),
      3,
      GL_FLOAT,
      GL_FALSE,
      sizeof(FFPVertex),
      reinterpret_cast<const void*>(offsetof(FFPVertex, cam_pos_x)));
  glVertexAttribPointer(
      static_cast<GLuint>(g_ffp_program.attr_cam_nrm),
      3,
      GL_FLOAT,
      GL_FALSE,
      sizeof(FFPVertex),
      reinterpret_cast<const void*>(offsetof(FFPVertex, cam_nrm_x)));
  glVertexAttribPointer(
      static_cast<GLuint>(g_ffp_program.attr_color),
      4,
      GL_UNSIGNED_BYTE,
      GL_TRUE,
      sizeof(FFPVertex),
      reinterpret_cast<const void*>(offsetof(FFPVertex, r)));

  GLenum draw_mode = gl_mode;
  const std::vector<uint32_t>* draw_indices = indices;
  const std::vector<FFPVertex>* draw_vertices = &vertices;
  std::vector<uint32_t> wireframe_indices;
  std::vector<FFPVertex> clip_w_vertices;
  std::vector<uint32_t> clip_w_indices;
  std::vector<FFPVertex> sky_screen_vertices;
  std::vector<uint32_t> sky_screen_indices;
  if (replace_sky_with_screen_quad) {
    BuildSkyScreenQuad(&sky_screen_vertices, &sky_screen_indices);
    draw_vertices = &sky_screen_vertices;
    draw_indices = &sky_screen_indices;
    draw_mode = GL_TRIANGLES;
  } else if (g_wasm_d3d9_state.fill_mode == D3DFILL_WIREFRAME && gl_mode == GL_TRIANGLES) {
    wireframe_indices = BuildWireframeTriangleListIndices(vertices.size(), indices);
    draw_indices = &wireframe_indices;
    draw_mode = GL_LINES;
    g_draw_wireframe += 1;
  }

  bool enable_clipw_reject = ((g_debug_ffp_flags & kDebugEnableClipWReject) != 0u);
  const bool allow_auto_clipw = ((g_debug_ffp_flags & kDebugDisableAutoClipWReject) == 0u);
  const bool auto_clipw_profile =
      (active_fvf == 322u ||
       active_fvf == 530u ||
       active_fvf == 578u ||
       active_fvf == 594u ||
       (active_fvf >= 4370u && active_fvf <= 4388u));
  if (!enable_clipw_reject && allow_auto_clipw && auto_clipw_profile) {
    enable_clipw_reject = true;
    if (active_fvf == 322u) g_fvf322_auto_clipw_draws += 1;
    if (active_fvf == 530u) g_fvf530_auto_clipw_draws += 1;
    if (active_fvf == 594u) g_fvf594_auto_clipw_draws += 1;
  }
  if (enable_clipw_reject &&
      (draw_mode == GL_TRIANGLES || draw_mode == GL_TRIANGLE_STRIP || draw_mode == GL_TRIANGLE_FAN)) {
    const bool force_clip_triangle_repair =
        current_sky_texture_draw ||
        active_fvf == 578u;
    if (BuildClipWClippedTriangles(
            draw_mode,
            *draw_vertices,
            draw_indices,
            &clip_w_vertices,
            &clip_w_indices,
            force_clip_triangle_repair)) {
      g_clip_w_reject_draws += 1;
      if (auto_clipw_profile) {
        if (active_fvf == 322u) g_fvf322_auto_clipw_reject_draws += 1;
        if (active_fvf == 530u) g_fvf530_auto_clipw_reject_draws += 1;
        if (active_fvf == 594u) g_fvf594_auto_clipw_reject_draws += 1;
      }
      if (clip_w_indices.empty() || clip_w_vertices.empty()) {
        TrackClipWEmptySignature(active_fvf);
        RecordDrawTrace(
            draw_mode,
            *draw_vertices,
            draw_indices,
            active_fvf,
            effective_depth_test_enabled,
            effective_depth_write_enabled,
            effective_blend_enabled,
            alpha_test_enabled,
            true,
            "clipw-empty");
        return true;
      }
      draw_mode = GL_TRIANGLES;
      draw_vertices = &clip_w_vertices;
      draw_indices = &clip_w_indices;
    }
  }

  RecordDrawTrace(
      draw_mode,
      *draw_vertices,
      draw_indices,
      active_fvf,
      effective_depth_test_enabled,
      effective_depth_write_enabled,
      effective_blend_enabled,
      alpha_test_enabled,
      false,
      nullptr);

  glBufferData(
      GL_ARRAY_BUFFER,
      static_cast<GLsizeiptr>(draw_vertices->size() * sizeof(FFPVertex)),
      draw_vertices->data(),
      GL_STREAM_DRAW);

  if (!draw_indices || draw_indices->empty()) {
    glDrawArrays(draw_mode, 0, static_cast<GLsizei>(draw_vertices->size()));
    TrackGLErrorsPostDraw();
    return true;
  }

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ffp_program.ibo);
  if (g_wasm_d3d9_state.webgl2) {
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(draw_indices->size() * sizeof(uint32_t)),
        draw_indices->data(),
        GL_STREAM_DRAW);
    glDrawElements(draw_mode, static_cast<GLsizei>(draw_indices->size()), GL_UNSIGNED_INT, nullptr);
  } else {
    std::vector<uint16_t> short_indices;
    short_indices.reserve(draw_indices->size());
    for (uint32_t idx : *draw_indices) short_indices.push_back(static_cast<uint16_t>(std::min<uint32_t>(idx, 0xFFFFu)));
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(short_indices.size() * sizeof(uint16_t)),
        short_indices.data(),
        GL_STREAM_DRAW);
    glDrawElements(draw_mode, static_cast<GLsizei>(short_indices.size()), GL_UNSIGNED_SHORT, nullptr);
  }
  TrackGLErrorsPostDraw();

  return true;
}

HRESULT BuildVerticesFromLinearStream(
    const uint8_t* src,
    UINT vertex_count,
    UINT stride,
    DWORD fvf,
    std::vector<FFPVertex>* out_vertices) {
  if (!src || !out_vertices || vertex_count == 0) return D3DERR_INVALIDCALL;
  if (stride == 0) return D3DERR_INVALIDCALL;

  D3DXMATRIX world_view{};
  D3DXMatrixMultiply(
      &world_view,
      reinterpret_cast<const D3DXMATRIX*>(&g_ffp_state.world[0]),
      reinterpret_cast<const D3DXMATRIX*>(&g_ffp_state.view));
  D3DXMATRIX wvp{};
  D3DXMatrixMultiply(&wvp, &world_view, reinterpret_cast<const D3DXMATRIX*>(&g_ffp_state.proj));

  out_vertices->clear();
  out_vertices->reserve(vertex_count);
  const bool has_xyzrhw = PositionHasRHW(fvf);
  bool forced_screen_like_draw = false;
  bool replay_fvf322_screenlike_draw = false;
  const bool suppress_fvf322_screenlike_replay =
      (fvf == 322u) && (CurrentDrawUsesSnowBillboardTexture() || CurrentDrawUsesSkyTexture());
  if (fvf == 322u &&
      !suppress_fvf322_screenlike_replay &&
      ((g_debug_ffp_flags & kDebugDisableFvf322ScreenlikeReplay) == 0u)) {
    UINT strict_screenlike_vertices = 0;
    UINT near_screenlike_vertices = 0;
    for (UINT i = 0; i < vertex_count; ++i) {
      const uint8_t* raw_vertex = src + static_cast<size_t>(i) * stride;
      if (IsFVF322ScreenlikeRawVertex(raw_vertex, stride)) strict_screenlike_vertices += 1;
      if (IsFVF322NearScreenlikeRawVertex(raw_vertex, stride)) near_screenlike_vertices += 1;
    }
    replay_fvf322_screenlike_draw = (strict_screenlike_vertices == vertex_count);
    if (!replay_fvf322_screenlike_draw) {
      // Startup UI quads often have one or two vertices nudged just outside the
      // viewport while still being authored in screen space. Accept those draws
      // when the batch is mostly strict screen-space and every vertex remains
      // near the viewport/depth range.
      const bool mostly_screenlike =
          (strict_screenlike_vertices * 4u) >= (vertex_count * 3u);
      const bool all_vertices_near_screenlike =
          (near_screenlike_vertices == vertex_count);
      replay_fvf322_screenlike_draw =
          mostly_screenlike && all_vertices_near_screenlike;
    }
  } else if (suppress_fvf322_screenlike_replay) {
    g_fvf322_screenlike_replay_suppressed += 1;
  }
  for (UINT i = 0; i < vertex_count; ++i) {
    FFPVertex v{};
    bool forced_screen_like = false;
    if (!DecodeVertexFromFVF(
            src + static_cast<size_t>(i) * stride,
            stride,
            fvf,
            world_view,
            wvp,
            &v,
            &forced_screen_like,
            replay_fvf322_screenlike_draw)) {
      return D3DERR_INVALIDCALL;
    }
    if (forced_screen_like) forced_screen_like_draw = true;
    out_vertices->push_back(v);
    if (!has_xyzrhw) {
      g_fvf_3d_vertices_total += 1;
      const float safe_w = SafeClipW(v.w);
      const float ndc_x = v.x / safe_w;
      const float ndc_y = v.y / safe_w;
      const float ndc_z = v.z / safe_w;
      if (std::fabs(ndc_x) <= 1.25f &&
          std::fabs(ndc_y) <= 1.25f &&
          ndc_z >= -1.25f &&
          ndc_z <= 1.25f) {
        g_fvf_3d_vertices_in_clip += 1;
      }
    }
  }
  if (fvf == 322u && forced_screen_like_draw) g_fvf322_screenlike_draws += 1;
  if (fvf == 322u && replay_fvf322_screenlike_draw) g_fvf322_screenlike_replay_draws += 1;
  return S_OK;
}

void ReleaseBoundResources() {
  if (g_ffp_state.vertex_decl) {
    g_ffp_state.vertex_decl->Release();
    g_ffp_state.vertex_decl = nullptr;
  }
  if (g_ffp_state.vertex_shader) {
    g_ffp_state.vertex_shader->Release();
    g_ffp_state.vertex_shader = nullptr;
  }
  if (g_ffp_state.pixel_shader) {
    g_ffp_state.pixel_shader->Release();
    g_ffp_state.pixel_shader = nullptr;
  }
  if (g_ffp_state.stream0) {
    g_ffp_state.stream0->Release();
    g_ffp_state.stream0 = nullptr;
  }
  if (g_ffp_state.indices) {
    g_ffp_state.indices->Release();
    g_ffp_state.indices = nullptr;
  }
  for (auto& tex : g_ffp_state.textures) {
    if (tex) {
      tex->Release();
      tex = nullptr;
    }
  }
}

HRESULT EnsureDefaultRenderSurfaces() {
  EnsureFFPStateInitialized();
  const UINT width = std::max<UINT>(1, g_wasm_d3d9_state.viewport.Width);
  const UINT height = std::max<UINT>(1, g_wasm_d3d9_state.viewport.Height);

  if (!g_default_color_surface ||
      g_default_color_surface->width != width ||
      g_default_color_surface->height != height) {
    if (g_default_color_surface) g_default_color_surface->Release();
    g_default_color_surface = new DummyDirect3DSurface9();
    if (!g_default_color_surface) return E_OUTOFMEMORY;
    g_default_color_surface->width = width;
    g_default_color_surface->height = height;
    g_default_color_surface->format = D3DFMT_X8R8G8B8;
    g_default_color_surface->pitch = width * 4;
    g_default_color_surface->stand_alone_data.resize(static_cast<size_t>(g_default_color_surface->pitch) * height, 0);
  }

  if (!g_default_depth_surface ||
      g_default_depth_surface->width != width ||
      g_default_depth_surface->height != height) {
    if (g_default_depth_surface) g_default_depth_surface->Release();
    g_default_depth_surface = new DummyDirect3DSurface9();
    if (!g_default_depth_surface) return E_OUTOFMEMORY;
    g_default_depth_surface->width = width;
    g_default_depth_surface->height = height;
    g_default_depth_surface->format = D3DFMT_D16;
    g_default_depth_surface->pitch = width * 2;
    g_default_depth_surface->stand_alone_data.resize(static_cast<size_t>(g_default_depth_surface->pitch) * height, 0);
  }

  return S_OK;
}

}  // namespace

HRESULT WydD3D9Surface_GetDesc(IDirect3DSurface9* surface, D3DSURFACE_DESC* desc) {
  if (!surface || !desc) return E_POINTER;
  auto* s = AsSurface(surface);
  if (!s) return D3DERR_INVALIDCALL;
  std::memset(desc, 0, sizeof(*desc));
  desc->Format = s->format;
  desc->Type = D3DRTYPE_SURFACE;
  desc->Usage = 0;
  desc->Pool = D3DPOOL_MANAGED;
  desc->MultiSampleType = D3DMULTISAMPLE_NONE;
  desc->MultiSampleQuality = 0;
  desc->Width = s->width;
  desc->Height = s->height;
  return S_OK;
}

HRESULT WydD3D9Surface_LockRect(IDirect3DSurface9* surface, D3DLOCKED_RECT* locked_rect, const RECT* rect, DWORD) {
  if (!surface || !locked_rect) return E_POINTER;
  auto* s = AsSurface(surface);
  if (!s) return D3DERR_INVALIDCALL;

  uint8_t* bits = nullptr;
  UINT pitch = s->pitch;

  if (s->owner) {
    bits = s->owner->pixels.data();
    pitch = s->owner->pitch;
  } else if (!s->stand_alone_data.empty()) {
    bits = s->stand_alone_data.data();
  }
  if (!bits) return D3DERR_INVALIDCALL;

  if (rect) {
    const int x = std::max(0, static_cast<int>(rect->left));
    const int y = std::max(0, static_cast<int>(rect->top));
    const int bpp = BytesPerPixelForFormat(s->format);
    bits += static_cast<size_t>(y) * pitch + static_cast<size_t>(x) * static_cast<size_t>(bpp);
  }

  locked_rect->Pitch = static_cast<INT>(pitch);
  locked_rect->pBits = bits;
  s->locked = true;
  return S_OK;
}

HRESULT WydD3D9Surface_UnlockRect(IDirect3DSurface9* surface) {
  if (!surface) return E_POINTER;
  auto* s = AsSurface(surface);
  if (!s) return D3DERR_INVALIDCALL;
  s->locked = false;
  if (s->owner) s->owner->gl_dirty = true;
  return S_OK;
}

HRESULT WydD3D9Texture_GetLevelDesc(IDirect3DBaseTexture9* texture, UINT level, D3DSURFACE_DESC* desc) {
  if (!texture || !desc) return E_POINTER;
  if (level != 0) return D3DERR_INVALIDCALL;
  auto* tex = AsTexture(texture);
  if (!tex) return D3DERR_INVALIDCALL;
  std::memset(desc, 0, sizeof(*desc));
  desc->Format = tex->format;
  desc->Type = D3DRTYPE_SURFACE;
  desc->Usage = 0;
  desc->Pool = tex->pool;
  desc->MultiSampleType = D3DMULTISAMPLE_NONE;
  desc->MultiSampleQuality = 0;
  desc->Width = tex->width;
  desc->Height = tex->height;
  return S_OK;
}

HRESULT WydD3D9Texture_GetSurfaceLevel(IDirect3DTexture9* texture, UINT level, IDirect3DSurface9** pp_surface_level) {
  if (!texture || !pp_surface_level) return E_POINTER;
  *pp_surface_level = nullptr;
  if (level != 0) return D3DERR_INVALIDCALL;
  auto* tex = AsTexture(texture);
  if (!tex) return D3DERR_INVALIDCALL;

  auto* surface = new DummyDirect3DSurface9();
  if (!surface) return E_OUTOFMEMORY;
  surface->owner = tex;
  tex->AddRef();
  surface->width = tex->width;
  surface->height = tex->height;
  surface->format = tex->format;
  surface->pitch = tex->pitch;
  *pp_surface_level = surface;
  return S_OK;
}

HRESULT WydD3D9Texture_LockRect(
    IDirect3DTexture9* texture,
    UINT level,
    D3DLOCKED_RECT* locked_rect,
    const RECT* rect,
    DWORD) {
  if (!texture || !locked_rect) return E_POINTER;
  if (level != 0) return D3DERR_INVALIDCALL;
  auto* tex = AsTexture(texture);
  if (!tex) return D3DERR_INVALIDCALL;

  uint8_t* bits = tex->pixels.data();
  if (!bits) return D3DERR_INVALIDCALL;

  if (rect) {
    const int x = std::max(0, static_cast<int>(rect->left));
    const int y = std::max(0, static_cast<int>(rect->top));
    bits += static_cast<size_t>(y) * tex->pitch + static_cast<size_t>(x) * static_cast<size_t>(tex->bytes_per_pixel);
  }

  locked_rect->Pitch = static_cast<INT>(tex->pitch);
  locked_rect->pBits = bits;
  tex->locked = true;
  return S_OK;
}

HRESULT WydD3D9Texture_UnlockRect(IDirect3DTexture9* texture, UINT level) {
  if (!texture) return E_POINTER;
  if (level != 0) return D3DERR_INVALIDCALL;
  auto* tex = AsTexture(texture);
  if (!tex) return D3DERR_INVALIDCALL;
  tex->locked = false;
  tex->gl_dirty = true;
  return S_OK;
}

HRESULT WydD3D9VertexBuffer_GetDesc(IDirect3DVertexBuffer9* vb, D3DVERTEXBUFFER_DESC* desc) {
  if (!vb || !desc) return E_POINTER;
  auto* buffer = AsVertexBuffer(vb);
  if (!buffer) return D3DERR_INVALIDCALL;
  std::memset(desc, 0, sizeof(*desc));
  desc->Format = D3DFMT_UNKNOWN;
  desc->Type = D3DRTYPE_SURFACE;
  desc->Usage = buffer->usage;
  desc->Pool = buffer->pool;
  desc->Size = static_cast<UINT>(buffer->data.size());
  desc->FVF = buffer->fvf;
  return S_OK;
}

HRESULT WydD3D9VertexBuffer_Lock(
    IDirect3DVertexBuffer9* vb,
    UINT offset_to_lock,
    UINT size_to_lock,
    void** ppb_data,
    DWORD) {
  if (!vb || !ppb_data) return E_POINTER;
  auto* buffer = AsVertexBuffer(vb);
  if (!buffer) return D3DERR_INVALIDCALL;
  *ppb_data = nullptr;
  if (offset_to_lock > buffer->data.size()) return D3DERR_INVALIDCALL;
  if (size_to_lock == 0) size_to_lock = static_cast<UINT>(buffer->data.size() - offset_to_lock);
  if (offset_to_lock + size_to_lock > buffer->data.size()) return D3DERR_INVALIDCALL;
  *ppb_data = buffer->data.data() + offset_to_lock;
  buffer->locked = true;
  return S_OK;
}

HRESULT WydD3D9VertexBuffer_Unlock(IDirect3DVertexBuffer9* vb) {
  if (!vb) return E_POINTER;
  auto* buffer = AsVertexBuffer(vb);
  if (!buffer) return D3DERR_INVALIDCALL;
  buffer->locked = false;
  return S_OK;
}

HRESULT WydD3D9IndexBuffer_GetDesc(IDirect3DIndexBuffer9* ib, D3DINDEXBUFFER_DESC* desc) {
  if (!ib || !desc) return E_POINTER;
  auto* buffer = AsIndexBuffer(ib);
  if (!buffer) return D3DERR_INVALIDCALL;
  std::memset(desc, 0, sizeof(*desc));
  desc->Format = buffer->format;
  desc->Type = D3DRTYPE_SURFACE;
  desc->Usage = buffer->usage;
  desc->Pool = buffer->pool;
  desc->Size = static_cast<UINT>(buffer->data.size());
  return S_OK;
}

HRESULT WydD3D9IndexBuffer_Lock(
    IDirect3DIndexBuffer9* ib,
    UINT offset_to_lock,
    UINT size_to_lock,
    void** ppb_data,
    DWORD) {
  if (!ib || !ppb_data) return E_POINTER;
  auto* buffer = AsIndexBuffer(ib);
  if (!buffer) return D3DERR_INVALIDCALL;
  *ppb_data = nullptr;
  if (offset_to_lock > buffer->data.size()) return D3DERR_INVALIDCALL;
  if (size_to_lock == 0) size_to_lock = static_cast<UINT>(buffer->data.size() - offset_to_lock);
  if (offset_to_lock + size_to_lock > buffer->data.size()) return D3DERR_INVALIDCALL;
  *ppb_data = buffer->data.data() + offset_to_lock;
  buffer->locked = true;
  return S_OK;
}

HRESULT WydD3D9IndexBuffer_Unlock(IDirect3DIndexBuffer9* ib) {
  if (!ib) return E_POINTER;
  auto* buffer = AsIndexBuffer(ib);
  if (!buffer) return D3DERR_INVALIDCALL;
  buffer->locked = false;
  return S_OK;
}

HRESULT WydD3D9Device_SetTransform(IDirect3DDevice9*, D3DTRANSFORMSTATETYPE state, const D3DMATRIX* mat) {
  EnsureFFPStateInitialized();
  if (!mat) return E_POINTER;

  if (state == D3DTS_VIEW) {
    g_ffp_state.view = *mat;
    return S_OK;
  }
  if (state == D3DTS_PROJECTION) {
    g_ffp_state.proj = *mat;
    return S_OK;
  }
  if (state >= D3DTS_TEXTURE0 && state <= D3DTS_TEXTURE7) {
    const int tex_index = static_cast<int>(state - D3DTS_TEXTURE0);
    if (tex_index >= 0 && tex_index < kMaxTextureStages) {
      g_ffp_state.tex[tex_index] = *mat;
      return S_OK;
    }
  }
  if (state >= D3DTS_WORLD) {
    const int index = static_cast<int>(state - D3DTS_WORLD);
    if (index >= 0 && index < kMaxWorldMatrices) {
      g_ffp_state.world[index] = *mat;
      return S_OK;
    }
  }

  return S_OK;
}

HRESULT WydD3D9Device_SetTexture(IDirect3DDevice9*, DWORD stage, IDirect3DBaseTexture9* texture) {
  EnsureFFPStateInitialized();
  if (stage >= kMaxTextureStages) return D3DERR_INVALIDCALL;
  if (TexturePathContains(texture, "sky")) {
    if (stage == 0) g_set_texture_stage0_sky_calls += 1;
    if (stage == 1) g_set_texture_stage1_sky_calls += 1;
  }
  auto& slot = g_ffp_state.textures[stage];
  if (slot == texture) return S_OK;
  if (slot) slot->Release();
  slot = texture;
  if (slot) slot->AddRef();
  return S_OK;
}

HRESULT WydD3D9Device_SetTextureStageState(
    IDirect3DDevice9*,
    DWORD stage,
    D3DTEXTURESTAGESTATETYPE type,
    DWORD value) {
  EnsureFFPStateInitialized();
  if (stage >= kMaxTextureStages) return D3DERR_INVALIDCALL;
  if (stage == 0 && type == D3DTSS_COLOROP) {
    g_set_stage0_colorop_last_value = value;
    if (value == D3DTOP_ADDSIGNED) g_set_stage0_colorop8_calls += 1;
    if (value == D3DTOP_MODULATE) g_set_stage0_colorop4_calls += 1;
  }
  const int idx = static_cast<int>(type);
  if (idx >= 0 && idx < static_cast<int>(g_ffp_state.tex_stage[stage].size())) {
    g_ffp_state.tex_stage[stage][idx] = value;
  }
  return S_OK;
}

HRESULT WydD3D9Device_SetSamplerState(IDirect3DDevice9*, DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD value) {
  EnsureFFPStateInitialized();
  if (sampler >= kMaxTextureStages) return D3DERR_INVALIDCALL;
  const int idx = static_cast<int>(type);
  if (idx >= 0 && idx < static_cast<int>(g_ffp_state.sampler[sampler].size())) {
    g_ffp_state.sampler[sampler][idx] = value;
  }
  return S_OK;
}

HRESULT WydD3D9Device_SetFVF(IDirect3DDevice9*, DWORD fvf) {
  EnsureFFPStateInitialized();
  if (g_ffp_state.vertex_decl) {
    g_ffp_state.vertex_decl->Release();
    g_ffp_state.vertex_decl = nullptr;
  }
  if (g_ffp_state.vertex_shader) {
    g_ffp_state.vertex_shader->Release();
    g_ffp_state.vertex_shader = nullptr;
    g_active_vs_hash = 0;
  }
  g_ffp_state.current_fvf = fvf;
  return S_OK;
}

HRESULT WydD3D9Device_SetStreamSource(
    IDirect3DDevice9*,
    UINT stream,
    IDirect3DVertexBuffer9* vb,
    UINT offset,
    UINT stride) {
  EnsureFFPStateInitialized();
  if (stream != 0) return S_OK;
  if (g_ffp_state.stream0 == vb && g_ffp_state.stream0_offset == offset && g_ffp_state.stream0_stride == stride) return S_OK;
  if (g_ffp_state.stream0) g_ffp_state.stream0->Release();
  g_ffp_state.stream0 = vb;
  if (g_ffp_state.stream0) g_ffp_state.stream0->AddRef();
  g_ffp_state.stream0_offset = offset;
  g_ffp_state.stream0_stride = stride;
  return S_OK;
}

HRESULT WydD3D9Device_SetIndices(IDirect3DDevice9*, IDirect3DIndexBuffer9* ib) {
  EnsureFFPStateInitialized();
  if (g_ffp_state.indices == ib) return S_OK;
  if (g_ffp_state.indices) g_ffp_state.indices->Release();
  g_ffp_state.indices = ib;
  if (g_ffp_state.indices) g_ffp_state.indices->AddRef();
  return S_OK;
}

HRESULT WydD3D9Device_CreateVertexDeclaration(
    IDirect3DDevice9*,
    const D3DVERTEXELEMENT9* declaration,
    IDirect3DVertexDeclaration9** pp_decl) {
  if (!pp_decl) return E_POINTER;
  *pp_decl = new DummyDirect3DVertexDeclaration9(declaration);
  return (*pp_decl) ? S_OK : E_OUTOFMEMORY;
}

HRESULT WydD3D9Device_SetVertexDeclaration(IDirect3DDevice9*, IDirect3DVertexDeclaration9* decl) {
  EnsureFFPStateInitialized();
  if (g_ffp_state.vertex_decl == decl) return S_OK;
  if (g_ffp_state.vertex_decl) g_ffp_state.vertex_decl->Release();
  g_ffp_state.vertex_decl = decl;
  if (g_ffp_state.vertex_decl) g_ffp_state.vertex_decl->AddRef();
  return S_OK;
}

HRESULT WydD3D9Device_CreateVertexShader(
    IDirect3DDevice9*,
    const DWORD* function_code,
    IDirect3DVertexShader9** pp_shader) {
  if (!pp_shader) return E_POINTER;
  auto* shader = new DummyDirect3DVertexShader9(function_code);
  *pp_shader = shader;
  if (!shader) return E_OUTOFMEMORY;
  TrackShaderCreate(&g_vs_telemetry, shader->hash, shader->dword_count, shader->version_token);
  return S_OK;
}

HRESULT WydD3D9Device_SetVertexShader(IDirect3DDevice9*, IDirect3DVertexShader9* shader) {
  EnsureFFPStateInitialized();
  if (g_ffp_state.vertex_shader == shader) return S_OK;
  if (g_ffp_state.vertex_shader) g_ffp_state.vertex_shader->Release();
  g_ffp_state.vertex_shader = shader;
  if (g_ffp_state.vertex_shader) g_ffp_state.vertex_shader->AddRef();
  if (auto* vs = AsVertexShader(shader)) {
    g_active_vs_hash = vs->hash;
    g_vs_bind_calls += 1;
    TrackShaderBind(&g_vs_telemetry, vs->hash);
  } else {
    g_active_vs_hash = 0;
  }
  return S_OK;
}

HRESULT WydD3D9Device_SetVertexShaderConstantF(
    IDirect3DDevice9*,
    UINT start_register,
    const float* constants,
    UINT vector4f_count) {
  EnsureFFPStateInitialized();
  if (!constants && vector4f_count > 0) return E_POINTER;
  const size_t begin = static_cast<size_t>(start_register) * 4u;
  const size_t count = static_cast<size_t>(vector4f_count) * 4u;
  if (begin >= g_ffp_state.vertex_shader_constants.size()) return S_OK;
  const size_t writable = std::min(count, g_ffp_state.vertex_shader_constants.size() - begin);
  if (writable > 0 && constants) {
    std::memcpy(
        g_ffp_state.vertex_shader_constants.data() + begin,
        constants,
        writable * sizeof(float));
  }
  return S_OK;
}

HRESULT WydD3D9Device_CreatePixelShader(
    IDirect3DDevice9*,
    const DWORD* function_code,
    IDirect3DPixelShader9** pp_shader) {
  if (!pp_shader) return E_POINTER;
  auto* shader = new DummyDirect3DPixelShader9(function_code);
  *pp_shader = shader;
  if (!shader) return E_OUTOFMEMORY;
  TrackShaderCreate(&g_ps_telemetry, shader->hash, shader->dword_count, shader->version_token);
  return S_OK;
}

HRESULT WydD3D9Device_SetPixelShader(IDirect3DDevice9*, IDirect3DPixelShader9* shader) {
  EnsureFFPStateInitialized();
  if (g_ffp_state.pixel_shader == shader) return S_OK;
  if (g_ffp_state.pixel_shader) g_ffp_state.pixel_shader->Release();
  g_ffp_state.pixel_shader = shader;
  if (g_ffp_state.pixel_shader) g_ffp_state.pixel_shader->AddRef();
  if (auto* ps = AsPixelShader(shader)) {
    g_active_ps_hash = ps->hash;
    g_ps_bind_calls += 1;
    TrackShaderBind(&g_ps_telemetry, ps->hash);
  } else {
    g_active_ps_hash = 0;
  }
  return S_OK;
}

HRESULT WydD3D9Device_DrawPrimitiveUP(
    IDirect3DDevice9*,
    D3DPRIMITIVETYPE primitive_type,
    UINT primitive_count,
    const void* vertex_stream_zero_data,
    UINT vertex_stream_zero_stride) {
  if (!EnsureWasmContext()) return E_FAIL;
  EnsureFFPStateInitialized();
  if (CurrentDrawUsesTexturePath("sky")) {
    g_draw_attempts_with_sky_texture += 1;
    g_draw_attempts_with_sky_texture_up += 1;
    g_draw_attempts_with_sky_last_fvf = g_ffp_state.current_fvf;
  }

  if (g_ffp_state.vertex_shader || g_ffp_state.pixel_shader) {
    // UP draws do not carry enough declaration metadata to replay arbitrary
    // programmable VS bytecode yet. Indexed VB draws below can CPU-replay the
    // skinning shader path when a D3DVERTEXELEMENT9 declaration is active.
    if (g_active_vs_hash != 0) {
      g_draws_with_vs += 1;
      TrackShaderDrawSkipped(&g_vs_telemetry, g_active_vs_hash);
    }
    if (g_active_ps_hash != 0) {
      g_draws_with_ps += 1;
      TrackShaderDrawSkipped(&g_ps_telemetry, g_active_ps_hash);
    }
    g_ffp_state.shader_draw_skipped += 1;
    g_ffp_state.primitive_count += primitive_count;
    ResetUPStreamState(false);
    return S_OK;
  }

  const UINT vertex_count = PrimitiveToVertexCount(primitive_type, primitive_count);
  if (vertex_count == 0 || !vertex_stream_zero_data || vertex_stream_zero_stride == 0) return D3DERR_INVALIDCALL;

  const DWORD fvf = g_ffp_state.current_fvf;
  TrackDrawFVF(fvf);
  TrackFVF322Path(fvf, 0);
  if (ShouldSkipFVFDraw(fvf)) return S_OK;
  const UINT stride = EffectiveStride(fvf, vertex_stream_zero_stride);
  if (stride == 0) return D3DERR_INVALIDCALL;

  std::vector<FFPVertex> vertices;
  HRESULT hr = BuildVerticesFromLinearStream(
      static_cast<const uint8_t*>(vertex_stream_zero_data),
      vertex_count,
      stride,
      fvf,
      &vertices);
  if (FAILED(hr)) {
    ResetUPStreamState(false);
    return hr;
  }

  if (!UploadAndDraw(PrimitiveToGL(primitive_type), vertices, nullptr)) {
    ResetUPStreamState(false);
    return E_FAIL;
  }
  g_ffp_state.draw_calls += 1;
  g_ffp_state.primitive_count += primitive_count;
  ResetUPStreamState(false);
  return S_OK;
}

HRESULT WydD3D9Device_DrawIndexedPrimitiveUP(
    IDirect3DDevice9*,
    D3DPRIMITIVETYPE primitive_type,
    UINT min_vertex_index,
    UINT num_vertices,
    UINT primitive_count,
    const void* index_data,
    D3DFORMAT index_data_format,
    const void* vertex_stream_zero_data,
    UINT vertex_stream_zero_stride) {
  if (!EnsureWasmContext()) return E_FAIL;
  EnsureFFPStateInitialized();
  if (CurrentDrawUsesTexturePath("sky")) {
    g_draw_attempts_with_sky_texture += 1;
    g_draw_attempts_with_sky_texture_up += 1;
    g_draw_attempts_with_sky_last_fvf = g_ffp_state.current_fvf;
  }
  if (g_ffp_state.vertex_shader || g_ffp_state.pixel_shader) {
    // See DrawPrimitiveUP: declaration-less UP shader draws stay skipped until
    // the bytecode translator path is connected.
    if (g_active_vs_hash != 0) {
      g_draws_with_vs += 1;
      TrackShaderDrawSkipped(&g_vs_telemetry, g_active_vs_hash);
    }
    if (g_active_ps_hash != 0) {
      g_draws_with_ps += 1;
      TrackShaderDrawSkipped(&g_ps_telemetry, g_active_ps_hash);
    }
    g_ffp_state.shader_draw_skipped += 1;
    g_ffp_state.primitive_count += primitive_count;
    ResetUPStreamState(true);
    return S_OK;
  }
  if (!index_data || !vertex_stream_zero_data || num_vertices == 0) return D3DERR_INVALIDCALL;

  const DWORD fvf = g_ffp_state.current_fvf;
  TrackDrawFVF(fvf);
  TrackFVF322Path(fvf, 1);
  if (ShouldSkipFVFDraw(fvf)) return S_OK;
  const UINT stride = EffectiveStride(fvf, vertex_stream_zero_stride);
  if (stride == 0) return D3DERR_INVALIDCALL;

  std::vector<FFPVertex> vertices;
  HRESULT hr = BuildVerticesFromLinearStream(
      static_cast<const uint8_t*>(vertex_stream_zero_data) + static_cast<size_t>(min_vertex_index) * stride,
      num_vertices,
      stride,
      fvf,
      &vertices);
  if (FAILED(hr)) {
    ResetUPStreamState(true);
    return hr;
  }

  const UINT index_count = PrimitiveToVertexCount(primitive_type, primitive_count);
  if (index_count == 0) return D3DERR_INVALIDCALL;
  std::vector<uint32_t> indices;
  indices.reserve(index_count);

  if (index_data_format == D3DFMT_INDEX16) {
    auto* idx16 = static_cast<const uint16_t*>(index_data);
    bool invalid_index = false;
    for (UINT i = 0; i < index_count; ++i) {
      int32_t v = static_cast<int32_t>(idx16[i]) - static_cast<int32_t>(min_vertex_index);
      if (v < 0 || static_cast<UINT>(v) >= num_vertices) {
        invalid_index = true;
        g_invalid_indices_total += 1;
        continue;
      }
      indices.push_back(static_cast<uint32_t>(v));
    }
    if (invalid_index) {
      g_invalid_indexed_draws += 1;
      ResetUPStreamState(true);
      return S_OK;
    }
  } else if (index_data_format == D3DFMT_INDEX32) {
    auto* idx32 = static_cast<const uint32_t*>(index_data);
    bool invalid_index = false;
    for (UINT i = 0; i < index_count; ++i) {
      int64_t v = static_cast<int64_t>(idx32[i]) - static_cast<int64_t>(min_vertex_index);
      if (v < 0 || static_cast<UINT>(v) >= num_vertices) {
        invalid_index = true;
        g_invalid_indices_total += 1;
        continue;
      }
      indices.push_back(static_cast<uint32_t>(v));
    }
    if (invalid_index) {
      g_invalid_indexed_draws += 1;
      ResetUPStreamState(true);
      return S_OK;
    }
  } else {
    return D3DERR_INVALIDCALL;
  }

  if (!UploadAndDraw(PrimitiveToGL(primitive_type), vertices, &indices)) {
    ResetUPStreamState(true);
    return E_FAIL;
  }
  g_ffp_state.draw_calls += 1;
  g_ffp_state.primitive_count += primitive_count;
  ResetUPStreamState(true);
  return S_OK;
}

HRESULT WydD3D9Device_DrawIndexedPrimitive(
    IDirect3DDevice9*,
    D3DPRIMITIVETYPE primitive_type,
    INT base_vertex_index,
    UINT,
    UINT,
    UINT start_index,
    UINT primitive_count) {
  if (!EnsureWasmContext()) return E_FAIL;
  EnsureFFPStateInitialized();
  if (CurrentDrawUsesTexturePath("sky")) {
    g_draw_attempts_with_sky_texture += 1;
    g_draw_attempts_with_sky_texture_indexed += 1;
    g_draw_attempts_with_sky_last_fvf = g_ffp_state.current_fvf;
  }

  if (g_ffp_state.pixel_shader || (g_ffp_state.vertex_shader && !AsVertexDeclaration(g_ffp_state.vertex_decl))) {
    if (g_active_vs_hash != 0) {
      g_draws_with_vs += 1;
      TrackShaderDrawSkipped(&g_vs_telemetry, g_active_vs_hash);
    }
    if (g_active_ps_hash != 0) {
      g_draws_with_ps += 1;
      TrackShaderDrawSkipped(&g_ps_telemetry, g_active_ps_hash);
    }
    g_ffp_state.shader_draw_skipped += 1;
    g_ffp_state.primitive_count += primitive_count;
    return S_OK;
  }

  auto* vb = AsVertexBuffer(g_ffp_state.stream0);
  auto* ib = AsIndexBuffer(g_ffp_state.indices);
  if (!vb || !ib) return D3DERR_INVALIDCALL;

  if (auto* decl = AsVertexDeclaration(g_ffp_state.vertex_decl)) {
    const UINT stride = g_ffp_state.stream0_stride;
    if (stride == 0) return D3DERR_INVALIDCALL;
    if (g_ffp_state.stream0_offset >= vb->data.size()) return D3DERR_INVALIDCALL;

    const uint8_t* vb_data = vb->data.data() + g_ffp_state.stream0_offset;
    const size_t vb_bytes = vb->data.size() - g_ffp_state.stream0_offset;
    const UINT vertex_total = static_cast<UINT>(vb_bytes / stride);
    if (vertex_total == 0) return D3DERR_INVALIDCALL;

    std::vector<FFPVertex> vertices;
    HRESULT hr = BuildVerticesFromDeclarationStream(vb_data, vertex_total, stride, *decl, &vertices);
    if (FAILED(hr)) return hr;

    const UINT index_count = PrimitiveToVertexCount(primitive_type, primitive_count);
    if (index_count == 0) return D3DERR_INVALIDCALL;
    std::vector<uint32_t> indices;
    indices.reserve(index_count);

    const size_t index_size = (ib->format == D3DFMT_INDEX32) ? 4u : 2u;
    const size_t index_start_byte = static_cast<size_t>(start_index) * index_size;
    if (index_start_byte + static_cast<size_t>(index_count) * index_size > ib->data.size()) return D3DERR_INVALIDCALL;

    if (ib->format == D3DFMT_INDEX32) {
      auto* idx32 = reinterpret_cast<const uint32_t*>(ib->data.data() + index_start_byte);
      bool invalid_index = false;
      for (UINT i = 0; i < index_count; ++i) {
        int64_t v = static_cast<int64_t>(idx32[i]) + static_cast<int64_t>(base_vertex_index);
        if (v < 0 || v >= static_cast<int64_t>(vertex_total)) {
          invalid_index = true;
          g_invalid_indices_total += 1;
          continue;
        }
        indices.push_back(static_cast<uint32_t>(v));
      }
      if (invalid_index) {
        g_invalid_indexed_draws += 1;
        return S_OK;
      }
    } else {
      auto* idx16 = reinterpret_cast<const uint16_t*>(ib->data.data() + index_start_byte);
      bool invalid_index = false;
      for (UINT i = 0; i < index_count; ++i) {
        int64_t v = static_cast<int64_t>(idx16[i]) + static_cast<int64_t>(base_vertex_index);
        if (v < 0 || v >= static_cast<int64_t>(vertex_total)) {
          invalid_index = true;
          g_invalid_indices_total += 1;
          continue;
        }
        indices.push_back(static_cast<uint32_t>(v));
      }
      if (invalid_index) {
        g_invalid_indexed_draws += 1;
        return S_OK;
      }
    }

    const DWORD saved_current_fvf = g_ffp_state.current_fvf;
    g_ffp_state.current_fvf = vb->fvf;
    const bool uploaded = UploadAndDraw(PrimitiveToGL(primitive_type), vertices, &indices);
    g_ffp_state.current_fvf = saved_current_fvf;
    if (!uploaded) return E_FAIL;
    if (g_active_vs_hash != 0) {
      g_draws_with_vs += 1;
      TrackShaderDrawUsed(&g_vs_telemetry, g_active_vs_hash);
    }
    if (g_active_ps_hash != 0) {
      g_draws_with_ps += 1;
      TrackShaderDrawUsed(&g_ps_telemetry, g_active_ps_hash);
    }
    g_ffp_state.draw_calls += 1;
    g_ffp_state.primitive_count += primitive_count;
    return S_OK;
  }

  const DWORD fvf = g_ffp_state.current_fvf ? g_ffp_state.current_fvf : vb->fvf;
  TrackDrawFVF(fvf);
  TrackFVF322Path(fvf, 2);
  if (ShouldSkipFVFDraw(fvf)) return S_OK;
  const UINT stride = EffectiveStride(fvf, g_ffp_state.stream0_stride);
  if (stride == 0) return D3DERR_INVALIDCALL;
  if (g_ffp_state.stream0_offset >= vb->data.size()) return D3DERR_INVALIDCALL;
  const uint8_t* vb_data = vb->data.data() + g_ffp_state.stream0_offset;
  const size_t vb_bytes = vb->data.size() - g_ffp_state.stream0_offset;
  const UINT vertex_total = static_cast<UINT>(vb_bytes / stride);
  if (vertex_total == 0) return D3DERR_INVALIDCALL;

  std::vector<FFPVertex> vertices;
  HRESULT hr = BuildVerticesFromLinearStream(vb_data, vertex_total, stride, fvf, &vertices);
  if (FAILED(hr)) return hr;

  const UINT index_count = PrimitiveToVertexCount(primitive_type, primitive_count);
  if (index_count == 0) return D3DERR_INVALIDCALL;
  std::vector<uint32_t> indices;
  indices.reserve(index_count);

  const size_t index_size = (ib->format == D3DFMT_INDEX32) ? 4u : 2u;
  const size_t index_start_byte = static_cast<size_t>(start_index) * index_size;
  if (index_start_byte + static_cast<size_t>(index_count) * index_size > ib->data.size()) return D3DERR_INVALIDCALL;

  if (ib->format == D3DFMT_INDEX32) {
    auto* idx32 = reinterpret_cast<const uint32_t*>(ib->data.data() + index_start_byte);
    bool invalid_index = false;
    for (UINT i = 0; i < index_count; ++i) {
      int64_t v = static_cast<int64_t>(idx32[i]) + static_cast<int64_t>(base_vertex_index);
      if (v < 0 || v >= static_cast<int64_t>(vertex_total)) {
        invalid_index = true;
        g_invalid_indices_total += 1;
        continue;
      }
      indices.push_back(static_cast<uint32_t>(v));
    }
    if (invalid_index) {
      g_invalid_indexed_draws += 1;
      return S_OK;
    }
  } else {
    auto* idx16 = reinterpret_cast<const uint16_t*>(ib->data.data() + index_start_byte);
    bool invalid_index = false;
    for (UINT i = 0; i < index_count; ++i) {
      int64_t v = static_cast<int64_t>(idx16[i]) + static_cast<int64_t>(base_vertex_index);
      if (v < 0 || v >= static_cast<int64_t>(vertex_total)) {
        invalid_index = true;
        g_invalid_indices_total += 1;
        continue;
      }
      indices.push_back(static_cast<uint32_t>(v));
    }
    if (invalid_index) {
      g_invalid_indexed_draws += 1;
      return S_OK;
    }
  }

  if (!UploadAndDraw(PrimitiveToGL(primitive_type), vertices, &indices)) return E_FAIL;
  if (g_active_vs_hash != 0) {
    g_draws_with_vs += 1;
    TrackShaderDrawUsed(&g_vs_telemetry, g_active_vs_hash);
  }
  if (g_active_ps_hash != 0) {
    g_draws_with_ps += 1;
    TrackShaderDrawUsed(&g_ps_telemetry, g_active_ps_hash);
  }
  g_ffp_state.draw_calls += 1;
  g_ffp_state.primitive_count += primitive_count;
  return S_OK;
}

HRESULT WydD3D9Device_CreateVertexBuffer(
    IDirect3DDevice9*,
    UINT length,
    DWORD usage,
    DWORD fvf,
    D3DPOOL pool,
    IDirect3DVertexBuffer9** pp_vb,
    void*) {
  if (!pp_vb) return E_POINTER;
  *pp_vb = nullptr;
  auto* vb = new DummyDirect3DVertexBuffer9(length, usage, fvf, pool);
  if (!vb) return E_OUTOFMEMORY;
  *pp_vb = vb;
  return S_OK;
}

HRESULT WydD3D9Device_CreateIndexBuffer(
    IDirect3DDevice9*,
    UINT length,
    DWORD usage,
    D3DFORMAT format,
    D3DPOOL pool,
    IDirect3DIndexBuffer9** pp_ib,
    void*) {
  if (!pp_ib) return E_POINTER;
  *pp_ib = nullptr;
  auto* ib = new DummyDirect3DIndexBuffer9(length, usage, format, pool);
  if (!ib) return E_OUTOFMEMORY;
  *pp_ib = ib;
  return S_OK;
}

HRESULT WydD3D9Device_GetRenderTarget(IDirect3DDevice9*, DWORD render_target_index, IDirect3DSurface9** pp_surface) {
  if (!pp_surface) return E_POINTER;
  *pp_surface = nullptr;
  if (render_target_index != 0) return D3DERR_INVALIDCALL;
  HRESULT hr = EnsureDefaultRenderSurfaces();
  if (FAILED(hr)) return hr;
  g_default_color_surface->AddRef();
  *pp_surface = g_default_color_surface;
  return S_OK;
}

HRESULT WydD3D9Device_GetDepthStencilSurface(IDirect3DDevice9*, IDirect3DSurface9** pp_z_stencil_surface) {
  if (!pp_z_stencil_surface) return E_POINTER;
  *pp_z_stencil_surface = nullptr;
  HRESULT hr = EnsureDefaultRenderSurfaces();
  if (FAILED(hr)) return hr;
  g_default_depth_surface->AddRef();
  *pp_z_stencil_surface = g_default_depth_surface;
  return S_OK;
}

HRESULT WydD3D9Device_CreateRenderTarget(
    IDirect3DDevice9*,
    UINT width,
    UINT height,
    D3DFORMAT format,
    D3DMULTISAMPLE_TYPE,
    DWORD,
    BOOL,
    IDirect3DSurface9** pp_surface,
    void*) {
  if (!pp_surface) return E_POINTER;
  *pp_surface = nullptr;

  auto* surface = new DummyDirect3DSurface9();
  if (!surface) return E_OUTOFMEMORY;
  surface->width = std::max<UINT>(1, width);
  surface->height = std::max<UINT>(1, height);
  surface->format = format == D3DFMT_UNKNOWN ? D3DFMT_X8R8G8B8 : format;
  surface->pitch = surface->width * static_cast<UINT>(BytesPerPixelForFormat(surface->format));
  surface->stand_alone_data.resize(static_cast<size_t>(surface->pitch) * surface->height, 0);
  *pp_surface = surface;
  return S_OK;
}

HRESULT WydD3D9Device_ColorFill(IDirect3DDevice9*, IDirect3DSurface9* surface, const RECT* rect, D3DCOLOR color) {
  auto* s = AsSurface(surface);
  if (!s) return D3DERR_INVALIDCALL;

  uint8_t* dst = nullptr;
  UINT pitch = s->pitch;
  UINT width = s->width;
  UINT height = s->height;
  int bpp = BytesPerPixelForFormat(s->format);

  if (s->owner) {
    dst = s->owner->pixels.data();
    pitch = s->owner->pitch;
    width = s->owner->width;
    height = s->owner->height;
    bpp = s->owner->bytes_per_pixel;
  } else if (!s->stand_alone_data.empty()) {
    dst = s->stand_alone_data.data();
  }
  if (!dst) return D3DERR_INVALIDCALL;

  int x0 = 0;
  int y0 = 0;
  int x1 = static_cast<int>(width);
  int y1 = static_cast<int>(height);
  if (rect) {
    x0 = std::max(0, static_cast<int>(rect->left));
    y0 = std::max(0, static_cast<int>(rect->top));
    x1 = std::min(static_cast<int>(width), static_cast<int>(rect->right));
    y1 = std::min(static_cast<int>(height), static_cast<int>(rect->bottom));
  }
  if (x0 >= x1 || y0 >= y1) return S_OK;

  const uint8_t a = static_cast<uint8_t>((color >> 24) & 0xFFu);
  const uint8_t r = static_cast<uint8_t>((color >> 16) & 0xFFu);
  const uint8_t g = static_cast<uint8_t>((color >> 8) & 0xFFu);
  const uint8_t b = static_cast<uint8_t>(color & 0xFFu);

  for (int y = y0; y < y1; ++y) {
    uint8_t* row = dst + static_cast<size_t>(y) * pitch;
    for (int x = x0; x < x1; ++x) {
      uint8_t* px = row + static_cast<size_t>(x) * static_cast<size_t>(bpp);
      if (bpp == 2) {
        uint16_t v = static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        std::memcpy(px, &v, sizeof(v));
      } else {
        px[0] = b;
        px[1] = g;
        px[2] = r;
        px[3] = a;
      }
    }
  }

  if (s->owner) s->owner->gl_dirty = true;

  return S_OK;
}

HRESULT WydD3D9Device_Reset(IDirect3DDevice9*, D3DPRESENT_PARAMETERS* params) {
  const HRESULT hr = EnsureWasmContext() ? S_OK : E_FAIL;
  if (FAILED(hr)) return hr;

  if (params) {
    if (params->BackBufferWidth > 0) g_wasm_d3d9_state.viewport.Width = params->BackBufferWidth;
    if (params->BackBufferHeight > 0) g_wasm_d3d9_state.viewport.Height = params->BackBufferHeight;
    ApplyViewport();
  }

  EnsureDefaultRenderSurfaces();
  return S_OK;
}

extern "C" void wyd_d3d9_set_draw_scope(const char* label) {
  g_draw_trace_scope = label ? label : "";
}

extern "C" void wyd_d3d9_clear_draw_scope() {
  g_draw_trace_scope.clear();
}

extern "C" void wyd_d3d9_set_draw_label(const char* label) {
  g_draw_trace_label = label ? label : "";
}

extern "C" void wyd_d3d9_trace_set_enabled(uint32_t enabled) {
  g_draw_trace_enabled = enabled != 0u;
}

extern "C" uint32_t wyd_d3d9_trace_get_enabled() {
  return DrawTraceIsEnabled() ? 1u : 0u;
}

extern "C" void wyd_d3d9_trace_reset() {
  ResetDrawTraceResults(false);
}

extern "C" void wyd_d3d9_trace_clear_probes() {
  ResetDrawTraceResults(true);
}

extern "C" void wyd_d3d9_trace_set_probe(uint32_t index, float x, float y) {
  if (index >= g_draw_trace_probes.size()) return;
  auto& probe = g_draw_trace_probes[index];
  probe.enabled = true;
  probe.x = x;
  probe.y = y;
  probe.depth_valid = false;
  probe.depth = 1.0f;
  probe.draw_serial = 0;
  probe.result.clear();
  probe.hit_count = 0;
  probe.first_hit_draw_serial = 0;
  probe.nearest_hit_draw_serial = 0;
  probe.nearest_zwrite_draw_serial = 0;
  probe.nearest_hit_depth = 1.0f;
  probe.nearest_zwrite_depth = 1.0f;
  probe.first_hit_result.clear();
  probe.nearest_hit_result.clear();
  probe.nearest_zwrite_result.clear();
}

extern "C" uint32_t wyd_d3d9_trace_probe_capacity() {
  return static_cast<uint32_t>(g_draw_trace_probes.size());
}

extern "C" uint32_t wyd_d3d9_trace_probe_enabled(uint32_t index) {
  if (index >= g_draw_trace_probes.size()) return 0u;
  return g_draw_trace_probes[index].enabled ? 1u : 0u;
}

extern "C" uint32_t wyd_d3d9_trace_probe_draw(uint32_t index) {
  if (index >= g_draw_trace_probes.size()) return 0u;
  return static_cast<uint32_t>(g_draw_trace_probes[index].draw_serial);
}

extern "C" const char* wyd_d3d9_trace_probe_result(uint32_t index) {
  if (index >= g_draw_trace_probes.size()) return "";
  return g_draw_trace_probes[index].result.c_str();
}

extern "C" uint32_t wyd_d3d9_trace_probe_hit_count(uint32_t index) {
  if (index >= g_draw_trace_probes.size()) return 0u;
  return static_cast<uint32_t>(g_draw_trace_probes[index].hit_count);
}

extern "C" uint32_t wyd_d3d9_trace_probe_first_hit_draw(uint32_t index) {
  if (index >= g_draw_trace_probes.size()) return 0u;
  return static_cast<uint32_t>(g_draw_trace_probes[index].first_hit_draw_serial);
}

extern "C" const char* wyd_d3d9_trace_probe_first_hit_result(uint32_t index) {
  if (index >= g_draw_trace_probes.size()) return "";
  return g_draw_trace_probes[index].first_hit_result.c_str();
}

extern "C" uint32_t wyd_d3d9_trace_probe_nearest_hit_draw(uint32_t index) {
  if (index >= g_draw_trace_probes.size()) return 0u;
  return static_cast<uint32_t>(g_draw_trace_probes[index].nearest_hit_draw_serial);
}

extern "C" const char* wyd_d3d9_trace_probe_nearest_hit_result(uint32_t index) {
  if (index >= g_draw_trace_probes.size()) return "";
  return g_draw_trace_probes[index].nearest_hit_result.c_str();
}

extern "C" uint32_t wyd_d3d9_trace_probe_nearest_zwrite_draw(uint32_t index) {
  if (index >= g_draw_trace_probes.size()) return 0u;
  return static_cast<uint32_t>(g_draw_trace_probes[index].nearest_zwrite_draw_serial);
}

extern "C" const char* wyd_d3d9_trace_probe_nearest_zwrite_result(uint32_t index) {
  if (index >= g_draw_trace_probes.size()) return "";
  return g_draw_trace_probes[index].nearest_zwrite_result.c_str();
}

extern "C" uint32_t wyd_d3d9_trace_top_count() {
  BuildDrawTraceTopExportCache();
  return static_cast<uint32_t>(g_draw_trace_top_export_cache.size());
}

extern "C" const char* wyd_d3d9_trace_top_sample(uint32_t index) {
  BuildDrawTraceTopExportCache();
  if (index >= g_draw_trace_top_export_cache.size()) return "";
  return g_draw_trace_top_export_cache[index].c_str();
}

extern "C" uint32_t wyd_d3d9_draw_calls() {
  return static_cast<uint32_t>(g_ffp_state.draw_calls);
}

extern "C" uint32_t wyd_d3d9_primitives() {
  return static_cast<uint32_t>(g_ffp_state.primitive_count);
}

extern "C" uint32_t wyd_d3d9_tex_decode_success() {
  return static_cast<uint32_t>(g_tex_decode_success);
}

extern "C" uint32_t wyd_d3d9_tex_decode_fail() {
  return static_cast<uint32_t>(g_tex_decode_fail);
}

extern "C" uint32_t wyd_d3d9_tex_uploads() {
  return static_cast<uint32_t>(g_tex_upload_count);
}

extern "C" uint32_t wyd_d3d9_textured_draws() {
  return static_cast<uint32_t>(g_tex_draw_count);
}

extern "C" uint32_t wyd_d3d9_tex_alpha_promoted_opaque() {
  return static_cast<uint32_t>(g_tex_alpha_promoted_opaque);
}

extern "C" uint32_t wyd_d3d9_shader_draws_skipped() {
  return static_cast<uint32_t>(g_ffp_state.shader_draw_skipped);
}

extern "C" uint32_t wyd_d3d9_vs_unique_shaders() {
  return static_cast<uint32_t>(g_vs_telemetry.size());
}

extern "C" uint32_t wyd_d3d9_ps_unique_shaders() {
  return static_cast<uint32_t>(g_ps_telemetry.size());
}

extern "C" uint32_t wyd_d3d9_vs_bind_calls() {
  return static_cast<uint32_t>(g_vs_bind_calls);
}

extern "C" uint32_t wyd_d3d9_ps_bind_calls() {
  return static_cast<uint32_t>(g_ps_bind_calls);
}

extern "C" uint32_t wyd_d3d9_draws_with_vs() {
  return static_cast<uint32_t>(g_draws_with_vs);
}

extern "C" uint32_t wyd_d3d9_draws_with_ps() {
  return static_cast<uint32_t>(g_draws_with_ps);
}

extern "C" uint32_t wyd_d3d9_active_vs_hash_lo() {
  return static_cast<uint32_t>(g_active_vs_hash & 0xFFFFFFFFull);
}

extern "C" uint32_t wyd_d3d9_active_vs_hash_hi() {
  return static_cast<uint32_t>((g_active_vs_hash >> 32) & 0xFFFFFFFFull);
}

extern "C" uint32_t wyd_d3d9_active_ps_hash_lo() {
  return static_cast<uint32_t>(g_active_ps_hash & 0xFFFFFFFFull);
}

extern "C" uint32_t wyd_d3d9_active_ps_hash_hi() {
  return static_cast<uint32_t>((g_active_ps_hash >> 32) & 0xFFFFFFFFull);
}

extern "C" uint32_t wyd_d3d9_vs_top_hash_lo(uint32_t rank) {
  return static_cast<uint32_t>(TopShaderTelemetryEntry(g_vs_telemetry, static_cast<size_t>(rank)).first & 0xFFFFFFFFull);
}

extern "C" uint32_t wyd_d3d9_vs_top_hash_hi(uint32_t rank) {
  return static_cast<uint32_t>((TopShaderTelemetryEntry(g_vs_telemetry, static_cast<size_t>(rank)).first >> 32) & 0xFFFFFFFFull);
}

extern "C" uint32_t wyd_d3d9_vs_top_binds(uint32_t rank) {
  return static_cast<uint32_t>(TopShaderTelemetryEntry(g_vs_telemetry, static_cast<size_t>(rank)).second.binds);
}

extern "C" uint32_t wyd_d3d9_vs_top_uses(uint32_t rank) {
  return static_cast<uint32_t>(TopShaderTelemetryEntry(g_vs_telemetry, static_cast<size_t>(rank)).second.draws_used);
}

extern "C" uint32_t wyd_d3d9_vs_top_skips(uint32_t rank) {
  return static_cast<uint32_t>(TopShaderTelemetryEntry(g_vs_telemetry, static_cast<size_t>(rank)).second.draws_skipped);
}

extern "C" uint32_t wyd_d3d9_vs_top_version(uint32_t rank) {
  return TopShaderTelemetryEntry(g_vs_telemetry, static_cast<size_t>(rank)).second.version_token;
}

extern "C" uint32_t wyd_d3d9_ps_top_hash_lo(uint32_t rank) {
  return static_cast<uint32_t>(TopShaderTelemetryEntry(g_ps_telemetry, static_cast<size_t>(rank)).first & 0xFFFFFFFFull);
}

extern "C" uint32_t wyd_d3d9_ps_top_hash_hi(uint32_t rank) {
  return static_cast<uint32_t>((TopShaderTelemetryEntry(g_ps_telemetry, static_cast<size_t>(rank)).first >> 32) & 0xFFFFFFFFull);
}

extern "C" uint32_t wyd_d3d9_ps_top_binds(uint32_t rank) {
  return static_cast<uint32_t>(TopShaderTelemetryEntry(g_ps_telemetry, static_cast<size_t>(rank)).second.binds);
}

extern "C" uint32_t wyd_d3d9_ps_top_uses(uint32_t rank) {
  return static_cast<uint32_t>(TopShaderTelemetryEntry(g_ps_telemetry, static_cast<size_t>(rank)).second.draws_used);
}

extern "C" uint32_t wyd_d3d9_ps_top_skips(uint32_t rank) {
  return static_cast<uint32_t>(TopShaderTelemetryEntry(g_ps_telemetry, static_cast<size_t>(rank)).second.draws_skipped);
}

extern "C" uint32_t wyd_d3d9_ps_top_version(uint32_t rank) {
  return TopShaderTelemetryEntry(g_ps_telemetry, static_cast<size_t>(rank)).second.version_token;
}

extern "C" uint32_t wyd_d3d9_shader_file_open_attempts() {
  return static_cast<uint32_t>(g_shader_file_open_attempts);
}

extern "C" uint32_t wyd_d3d9_shader_file_open_success() {
  return static_cast<uint32_t>(g_shader_file_open_success);
}

extern "C" uint32_t wyd_d3d9_shader_file_open_fail() {
  return static_cast<uint32_t>(g_shader_file_open_fail);
}

extern "C" uint32_t wyd_d3d9_shader_file_open_skinmesh() {
  return static_cast<uint32_t>(g_shader_file_open_skinmesh);
}

extern "C" uint32_t wyd_d3d9_shader_file_open_vseffect() {
  return static_cast<uint32_t>(g_shader_file_open_vseffect);
}

extern "C" uint32_t wyd_d3d9_shader_file_open_pseffect() {
  return static_cast<uint32_t>(g_shader_file_open_pseffect);
}

extern "C" uint32_t wyd_d3d9_asset_file_open_attempts() {
  return static_cast<uint32_t>(g_asset_file_open_attempts);
}

extern "C" uint32_t wyd_d3d9_asset_file_open_success() {
  return static_cast<uint32_t>(g_asset_file_open_success);
}

extern "C" uint32_t wyd_d3d9_asset_file_open_fail() {
  return static_cast<uint32_t>(g_asset_file_open_fail);
}

extern "C" uint32_t wyd_d3d9_asset_file_open_fail_mesh() {
  return static_cast<uint32_t>(g_asset_file_open_fail_mesh);
}

extern "C" uint32_t wyd_d3d9_asset_file_open_fail_env() {
  return static_cast<uint32_t>(g_asset_file_open_fail_env);
}

extern "C" uint32_t wyd_d3d9_asset_file_open_fail_ui() {
  return static_cast<uint32_t>(g_asset_file_open_fail_ui);
}

extern "C" uint32_t wyd_d3d9_asset_file_open_fail_texture() {
  return static_cast<uint32_t>(g_asset_file_open_fail_texture);
}

extern "C" uint32_t wyd_d3d9_asset_file_open_fail_sound() {
  return static_cast<uint32_t>(g_asset_file_open_fail_sound);
}

extern "C" uint32_t wyd_d3d9_asset_file_open_fail_sample_count() {
  return static_cast<uint32_t>(g_asset_file_open_fail_samples.size());
}

extern "C" const char* wyd_d3d9_asset_file_open_fail_sample(uint32_t index) {
  if (index >= g_asset_file_open_fail_samples.size()) return "";
  return g_asset_file_open_fail_samples[index].c_str();
}

extern "C" uint32_t wyd_d3d9_asset_path_fallback_attempts() {
  return static_cast<uint32_t>(g_asset_path_fallback_attempts);
}

extern "C" uint32_t wyd_d3d9_asset_path_fallback_hits() {
  return static_cast<uint32_t>(g_asset_path_fallback_hits);
}

extern "C" uint32_t wyd_d3d9_asset_path_fallback_or010_hits() {
  return static_cast<uint32_t>(g_asset_path_fallback_or010_hits);
}

extern "C" uint32_t wyd_d3d9_asset_path_fallback_sample_count() {
  return static_cast<uint32_t>(g_asset_path_fallback_samples.size());
}

extern "C" const char* wyd_d3d9_asset_path_fallback_sample(uint32_t index) {
  if (index >= g_asset_path_fallback_samples.size()) return "";
  return g_asset_path_fallback_samples[index].c_str();
}

extern "C" uint32_t wyd_d3d9_fvf_xyzrhw_draws() {
  return static_cast<uint32_t>(g_draw_fvf_xyzrhw);
}

extern "C" uint32_t wyd_d3d9_fvf_weighted_draws() {
  return static_cast<uint32_t>(g_draw_fvf_weighted);
}

extern "C" uint32_t wyd_d3d9_fvf_tex2plus_draws() {
  return static_cast<uint32_t>(g_draw_fvf_tex2plus);
}

extern "C" uint32_t wyd_d3d9_decl_draw_calls() {
  return static_cast<uint32_t>(g_decl_draw_calls);
}

extern "C" uint32_t wyd_d3d9_decl_skinned_draw_calls() {
  return static_cast<uint32_t>(g_decl_skinned_draw_calls);
}

extern "C" uint32_t wyd_d3d9_decl_vertices_total() {
  return static_cast<uint32_t>(g_decl_vertices_total);
}

extern "C" uint32_t wyd_d3d9_decl_vertices_in_clip() {
  return static_cast<uint32_t>(g_decl_vertices_in_clip);
}

extern "C" uint32_t wyd_d3d9_decl_rgba_index_order_draws() {
  return static_cast<uint32_t>(g_decl_rgba_index_order_draws);
}

extern "C" uint32_t wyd_d3d9_decl_bgra_index_order_draws() {
  return static_cast<uint32_t>(g_decl_bgra_index_order_draws);
}

extern "C" uint32_t wyd_d3d9_invalid_indexed_draws() {
  return static_cast<uint32_t>(g_invalid_indexed_draws);
}

extern "C" uint32_t wyd_d3d9_invalid_indices_total() {
  return static_cast<uint32_t>(g_invalid_indices_total);
}

extern "C" uint32_t wyd_d3d9_clip_w_reject_draws() {
  return static_cast<uint32_t>(g_clip_w_reject_draws);
}

extern "C" uint32_t wyd_d3d9_clip_w_reject_triangles() {
  return static_cast<uint32_t>(g_clip_w_reject_triangles);
}

extern "C" uint32_t wyd_d3d9_clip_w_keep_triangles() {
  return static_cast<uint32_t>(g_clip_w_keep_triangles);
}

extern "C" uint32_t wyd_d3d9_clipw_empty_signature_count() {
  BuildClipWEmptySignatureExportCache();
  return static_cast<uint32_t>(g_clipw_empty_signature_export_cache.size());
}

extern "C" const char* wyd_d3d9_clipw_empty_signature_sample(uint32_t index) {
  BuildClipWEmptySignatureExportCache();
  if (index >= g_clipw_empty_signature_export_cache.size()) return "";
  return g_clipw_empty_signature_export_cache[index].c_str();
}

extern "C" uint32_t wyd_d3d9_fvf_3d_vertices_total() {
  return static_cast<uint32_t>(g_fvf_3d_vertices_total);
}

extern "C" uint32_t wyd_d3d9_fvf_3d_vertices_in_clip() {
  return static_cast<uint32_t>(g_fvf_3d_vertices_in_clip);
}

extern "C" uint32_t wyd_d3d9_stage1_generated_tci_draws() {
  return static_cast<uint32_t>(g_stage1_generated_tci_draws);
}

extern "C" uint32_t wyd_d3d9_stage1_textransform_draws() {
  return static_cast<uint32_t>(g_stage1_textransform_draws);
}

extern "C" uint32_t wyd_d3d9_stage1_tci0_draws() {
  return static_cast<uint32_t>(g_stage1_tci0_draws);
}

extern "C" uint32_t wyd_d3d9_stage1_tci1_draws() {
  return static_cast<uint32_t>(g_stage1_tci1_draws);
}

extern "C" uint32_t wyd_d3d9_stage1_tci_other_draws() {
  return static_cast<uint32_t>(g_stage1_tci_other_draws);
}

extern "C" uint32_t wyd_d3d9_alpha_test_enabled_draws() {
  return static_cast<uint32_t>(g_draw_alpha_test_enabled);
}

extern "C" uint32_t wyd_d3d9_alpha_test_disabled_draws() {
  return static_cast<uint32_t>(g_draw_alpha_test_disabled);
}

extern "C" uint32_t wyd_d3d9_blend_enabled_draws() {
  return static_cast<uint32_t>(g_draw_blend_enabled);
}

extern "C" uint32_t wyd_d3d9_depth_test_disabled_draws() {
  return static_cast<uint32_t>(g_draw_depth_test_disabled);
}

extern "C" uint32_t wyd_d3d9_depth_write_disabled_draws() {
  return static_cast<uint32_t>(g_draw_depth_write_disabled);
}

extern "C" uint32_t wyd_d3d9_depth_write_guard_forced_draws() {
  return static_cast<uint32_t>(g_depth_write_guard_forced_draws);
}

extern "C" uint32_t wyd_d3d9_draw_order_first_sky() {
  return static_cast<uint32_t>(g_draw_order_first_sky);
}

extern "C" uint32_t wyd_d3d9_draw_order_first_skin() {
  return static_cast<uint32_t>(g_draw_order_first_skin);
}

extern "C" uint32_t wyd_d3d9_draw_order_first_terrain594() {
  return static_cast<uint32_t>(g_draw_order_first_terrain594);
}

extern "C" uint32_t wyd_d3d9_draw_order_first_water578() {
  return static_cast<uint32_t>(g_draw_order_first_water578);
}

extern "C" uint32_t wyd_d3d9_draw_order_first_fvf530() {
  return static_cast<uint32_t>(g_draw_order_first_fvf530);
}

extern "C" uint32_t wyd_d3d9_draw_order_first_fvf322() {
  return static_cast<uint32_t>(g_draw_order_first_fvf322);
}

extern "C" uint32_t wyd_d3d9_draw_order_count_sky() {
  return static_cast<uint32_t>(g_draw_order_count_sky);
}

extern "C" uint32_t wyd_d3d9_draw_order_count_skin() {
  return static_cast<uint32_t>(g_draw_order_count_skin);
}

extern "C" uint32_t wyd_d3d9_draw_order_count_terrain594() {
  return static_cast<uint32_t>(g_draw_order_count_terrain594);
}

extern "C" uint32_t wyd_d3d9_draw_order_count_water578() {
  return static_cast<uint32_t>(g_draw_order_count_water578);
}

extern "C" uint32_t wyd_d3d9_draw_order_count_fvf530() {
  return static_cast<uint32_t>(g_draw_order_count_fvf530);
}

extern "C" uint32_t wyd_d3d9_draw_order_count_fvf322() {
  return static_cast<uint32_t>(g_draw_order_count_fvf322);
}

extern "C" uint32_t wyd_d3d9_draw_order_frame_first_sky() {
  return static_cast<uint32_t>(g_draw_order_frame_first_sky);
}

extern "C" uint32_t wyd_d3d9_draw_order_frame_first_skin() {
  return static_cast<uint32_t>(g_draw_order_frame_first_skin);
}

extern "C" uint32_t wyd_d3d9_draw_order_frame_first_terrain594() {
  return static_cast<uint32_t>(g_draw_order_frame_first_terrain594);
}

extern "C" uint32_t wyd_d3d9_draw_order_frame_first_water578() {
  return static_cast<uint32_t>(g_draw_order_frame_first_water578);
}

extern "C" uint32_t wyd_d3d9_draw_order_frame_first_fvf530() {
  return static_cast<uint32_t>(g_draw_order_frame_first_fvf530);
}

extern "C" uint32_t wyd_d3d9_draw_order_frame_first_fvf322() {
  return static_cast<uint32_t>(g_draw_order_frame_first_fvf322);
}

extern "C" uint32_t wyd_d3d9_draw_order_frame_count_sky() {
  return static_cast<uint32_t>(g_draw_order_frame_count_sky);
}

extern "C" uint32_t wyd_d3d9_draw_order_frame_count_skin() {
  return static_cast<uint32_t>(g_draw_order_frame_count_skin);
}

extern "C" uint32_t wyd_d3d9_draw_order_frame_count_terrain594() {
  return static_cast<uint32_t>(g_draw_order_frame_count_terrain594);
}

extern "C" uint32_t wyd_d3d9_draw_order_frame_count_water578() {
  return static_cast<uint32_t>(g_draw_order_frame_count_water578);
}

extern "C" uint32_t wyd_d3d9_draw_order_frame_count_fvf530() {
  return static_cast<uint32_t>(g_draw_order_frame_count_fvf530);
}

extern "C" uint32_t wyd_d3d9_draw_order_frame_count_fvf322() {
  return static_cast<uint32_t>(g_draw_order_frame_count_fvf322);
}

extern "C" uint32_t wyd_d3d9_lighting_enabled_draws() {
  return static_cast<uint32_t>(g_draw_lighting_enabled);
}

extern "C" uint32_t wyd_d3d9_fog_enabled_draws() {
  return static_cast<uint32_t>(g_draw_fog_enabled);
}

extern "C" uint32_t wyd_d3d9_fog_enabled() {
  return g_wasm_d3d9_state.fog_enable ? 1u : 0u;
}

extern "C" float wyd_d3d9_fog_start() {
  return g_wasm_d3d9_state.fog_start;
}

extern "C" float wyd_d3d9_fog_end() {
  return g_wasm_d3d9_state.fog_end;
}

extern "C" uint32_t wyd_d3d9_fog_color() {
  return static_cast<uint32_t>(g_wasm_d3d9_state.fog_color);
}

extern "C" uint32_t wyd_d3d9_wireframe_draws() {
  return static_cast<uint32_t>(g_draw_wireframe);
}

extern "C" uint32_t wyd_d3d9_material_set_calls() {
  return static_cast<uint32_t>(g_material_set_calls);
}

extern "C" uint32_t wyd_d3d9_light_set_calls() {
  return static_cast<uint32_t>(g_light_set_calls);
}

extern "C" uint32_t wyd_d3d9_light_enable_calls() {
  return static_cast<uint32_t>(g_light_enable_calls);
}

extern "C" uint32_t wyd_d3d9_lighted_vertices() {
  return static_cast<uint32_t>(g_lighted_vertices);
}

extern "C" uint32_t wyd_d3d9_stage0_color_selectarg1_draws() {
  return static_cast<uint32_t>(g_draw_stage0_color_selectarg1);
}

extern "C" uint32_t wyd_d3d9_stage0_color_modulate_draws() {
  return static_cast<uint32_t>(g_draw_stage0_color_modulate);
}

extern "C" uint32_t wyd_d3d9_stage0_alpha_selectarg1_draws() {
  return static_cast<uint32_t>(g_draw_stage0_alpha_selectarg1);
}

extern "C" uint32_t wyd_d3d9_stage0_alpha_modulate_draws() {
  return static_cast<uint32_t>(g_draw_stage0_alpha_modulate);
}

extern "C" uint32_t wyd_d3d9_cull_none_draws() {
  return static_cast<uint32_t>(g_draw_cull_none);
}

extern "C" uint32_t wyd_d3d9_cull_cw_draws() {
  return static_cast<uint32_t>(g_draw_cull_cw);
}

extern "C" uint32_t wyd_d3d9_cull_ccw_draws() {
  return static_cast<uint32_t>(g_draw_cull_ccw);
}

extern "C" uint32_t wyd_d3d9_cull_mirror_worldview_draws() {
  return static_cast<uint32_t>(g_cull_mirror_worldview_draws);
}

extern "C" uint32_t wyd_d3d9_cull_frontface_flipped_draws() {
  return static_cast<uint32_t>(g_cull_frontface_flipped_draws);
}

extern "C" uint32_t wyd_d3d9_gl_error_total() {
  return static_cast<uint32_t>(g_gl_error_total);
}

extern "C" uint32_t wyd_d3d9_gl_error_draw_calls() {
  return static_cast<uint32_t>(g_gl_error_draw_calls);
}

extern "C" uint32_t wyd_d3d9_gl_error_last() {
  return g_gl_error_last;
}

extern "C" uint32_t wyd_d3d9_fvf_top_code(uint32_t rank) {
  return TopFVFEntry(static_cast<size_t>(rank)).first;
}

extern "C" uint32_t wyd_d3d9_fvf_top_count(uint32_t rank) {
  return static_cast<uint32_t>(TopFVFEntry(static_cast<size_t>(rank)).second);
}

extern "C" uint32_t wyd_d3d9_fvf_depth_write_enabled_top_code(uint32_t rank) {
  return TopHistogramEntry(g_fvf_depth_write_enabled_histogram, static_cast<size_t>(rank)).first;
}

extern "C" uint32_t wyd_d3d9_fvf_depth_write_enabled_top_count(uint32_t rank) {
  return static_cast<uint32_t>(TopHistogramEntry(g_fvf_depth_write_enabled_histogram, static_cast<size_t>(rank)).second);
}

extern "C" uint32_t wyd_d3d9_fvf_depth_write_disabled_top_code(uint32_t rank) {
  return TopHistogramEntry(g_fvf_depth_write_disabled_histogram, static_cast<size_t>(rank)).first;
}

extern "C" uint32_t wyd_d3d9_fvf_depth_write_disabled_top_count(uint32_t rank) {
  return static_cast<uint32_t>(TopHistogramEntry(g_fvf_depth_write_disabled_histogram, static_cast<size_t>(rank)).second);
}

extern "C" uint32_t wyd_d3d9_debug_skip_fvf_draws() {
  return static_cast<uint32_t>(g_debug_skip_fvf_draws);
}

extern "C" uint32_t wyd_d3d9_fvf322_draw_primitive_up() {
  return static_cast<uint32_t>(g_fvf322_draw_primitive_up);
}

extern "C" uint32_t wyd_d3d9_fvf322_draw_indexed_primitive_up() {
  return static_cast<uint32_t>(g_fvf322_draw_indexed_primitive_up);
}

extern "C" uint32_t wyd_d3d9_fvf322_draw_indexed_primitive() {
  return static_cast<uint32_t>(g_fvf322_draw_indexed_primitive);
}

extern "C" uint32_t wyd_d3d9_fvf322_with_stage0_texture() {
  return static_cast<uint32_t>(g_fvf322_with_stage0_texture);
}

extern "C" uint32_t wyd_d3d9_fvf322_without_stage0_texture() {
  return static_cast<uint32_t>(g_fvf322_without_stage0_texture);
}

extern "C" uint32_t wyd_d3d9_fvf322_screenlike_vertices() {
  return static_cast<uint32_t>(g_fvf322_screenlike_vertices);
}

extern "C" uint32_t wyd_d3d9_fvf322_screenlike_draws() {
  return static_cast<uint32_t>(g_fvf322_screenlike_draws);
}

extern "C" uint32_t wyd_d3d9_fvf322_screenlike_replay_draws() {
  return static_cast<uint32_t>(g_fvf322_screenlike_replay_draws);
}

extern "C" uint32_t wyd_d3d9_fvf322_screenlike_replay_suppressed() {
  return static_cast<uint32_t>(g_fvf322_screenlike_replay_suppressed);
}

extern "C" uint32_t wyd_d3d9_texture_draws_sky() {
  return static_cast<uint32_t>(g_texture_draws_sky);
}

extern "C" uint32_t wyd_d3d9_texture_draws_water() {
  return static_cast<uint32_t>(g_texture_draws_water);
}

extern "C" uint32_t wyd_d3d9_texture_draws_bright() {
  return static_cast<uint32_t>(g_texture_draws_bright);
}

extern "C" uint32_t wyd_d3d9_fvf322_bright_draws() {
  return static_cast<uint32_t>(g_fvf322_bright_draws);
}

extern "C" uint32_t wyd_d3d9_fvf322_class_count(uint32_t class_id) {
  if (class_id >= g_fvf322_class_draws.size()) return 0u;
  return static_cast<uint32_t>(g_fvf322_class_draws[class_id]);
}

extern "C" const char* wyd_d3d9_fvf322_class_name(uint32_t class_id) {
  return Fvf322ClassName(class_id);
}

extern "C" uint32_t wyd_d3d9_skin_suspicious_texture_draws() {
  return static_cast<uint32_t>(g_skin_suspicious_texture_draws);
}

extern "C" uint32_t wyd_d3d9_skin_suspicious_texture_sample_count() {
  return static_cast<uint32_t>(g_skin_suspicious_texture_samples.size());
}

extern "C" const char* wyd_d3d9_skin_suspicious_texture_sample(uint32_t index) {
  if (index >= g_skin_suspicious_texture_samples.size()) return "";
  return g_skin_suspicious_texture_samples[index].c_str();
}

extern "C" uint32_t wyd_d3d9_stage0_colorop8_draws() {
  return static_cast<uint32_t>(g_stage0_colorop8_draws);
}

extern "C" uint32_t wyd_d3d9_stage0_colorop8_with_texture() {
  return static_cast<uint32_t>(g_stage0_colorop8_with_texture);
}

extern "C" uint32_t wyd_d3d9_stage0_colorop8_without_texture() {
  return static_cast<uint32_t>(g_stage0_colorop8_without_texture);
}

extern "C" uint32_t wyd_d3d9_stage0_colorop8_pathless_texture() {
  return static_cast<uint32_t>(g_stage0_colorop8_pathless_texture);
}

extern "C" uint32_t wyd_d3d9_stage0_colorop8_last_fvf() {
  return g_stage0_colorop8_last_fvf;
}

extern "C" uint32_t wyd_d3d9_stage0_colorop8_last_width() {
  return g_stage0_colorop8_last_width;
}

extern "C" uint32_t wyd_d3d9_stage0_colorop8_last_height() {
  return g_stage0_colorop8_last_height;
}

extern "C" uint32_t wyd_d3d9_stage0_colorop8_last_path_len() {
  return g_stage0_colorop8_last_path_len;
}

extern "C" uint32_t wyd_d3d9_set_stage0_colorop8_calls() {
  return static_cast<uint32_t>(g_set_stage0_colorop8_calls);
}

extern "C" uint32_t wyd_d3d9_set_stage0_colorop4_calls() {
  return static_cast<uint32_t>(g_set_stage0_colorop4_calls);
}

extern "C" uint32_t wyd_d3d9_set_stage0_colorop_last_value() {
  return g_set_stage0_colorop_last_value;
}

extern "C" uint32_t wyd_d3d9_set_texture_stage0_sky_calls() {
  return static_cast<uint32_t>(g_set_texture_stage0_sky_calls);
}

extern "C" uint32_t wyd_d3d9_set_texture_stage1_sky_calls() {
  return static_cast<uint32_t>(g_set_texture_stage1_sky_calls);
}

extern "C" uint32_t wyd_d3d9_draw_attempts_with_sky_texture() {
  return static_cast<uint32_t>(g_draw_attempts_with_sky_texture);
}

extern "C" uint32_t wyd_d3d9_draw_attempts_with_sky_texture_indexed() {
  return static_cast<uint32_t>(g_draw_attempts_with_sky_texture_indexed);
}

extern "C" uint32_t wyd_d3d9_draw_attempts_with_sky_texture_up() {
  return static_cast<uint32_t>(g_draw_attempts_with_sky_texture_up);
}

extern "C" uint32_t wyd_d3d9_draw_attempts_with_sky_last_fvf() {
  return g_draw_attempts_with_sky_last_fvf;
}

extern "C" uint32_t wyd_d3d9_sky_clip_draws() {
  return static_cast<uint32_t>(g_sky_clip_draws);
}

extern "C" uint32_t wyd_d3d9_sky_clip_last_vertex_count() {
  return g_sky_clip_last_vertex_count;
}

extern "C" uint32_t wyd_d3d9_sky_clip_last_index_count() {
  return g_sky_clip_last_index_count;
}

extern "C" uint32_t wyd_d3d9_sky_clip_last_stable_w_vertices() {
  return g_sky_clip_last_stable_w_vertices;
}

extern "C" uint32_t wyd_d3d9_sky_clip_last_negative_w_vertices() {
  return g_sky_clip_last_negative_w_vertices;
}

extern "C" uint32_t wyd_d3d9_sky_clip_last_near_w_vertices() {
  return g_sky_clip_last_near_w_vertices;
}

extern "C" uint32_t wyd_d3d9_sky_clip_last_inside_vertices() {
  return g_sky_clip_last_inside_vertices;
}

extern "C" uint32_t wyd_d3d9_sky_clip_last_large_ndc_vertices() {
  return g_sky_clip_last_large_ndc_vertices;
}

extern "C" uint32_t wyd_d3d9_sky_clip_last_triangle_count() {
  return g_sky_clip_last_triangle_count;
}

extern "C" uint32_t wyd_d3d9_sky_clip_last_triangles_all_stable_w() {
  return g_sky_clip_last_triangles_all_stable_w;
}

extern "C" uint32_t wyd_d3d9_sky_clip_last_triangles_any_unstable_w() {
  return g_sky_clip_last_triangles_any_unstable_w;
}

extern "C" uint32_t wyd_d3d9_sky_clip_last_triangles_any_outside() {
  return g_sky_clip_last_triangles_any_outside;
}

extern "C" float wyd_d3d9_sky_clip_last_min_w() {
  return g_sky_clip_last_min_w;
}

extern "C" float wyd_d3d9_sky_clip_last_max_w() {
  return g_sky_clip_last_max_w;
}

extern "C" float wyd_d3d9_sky_clip_last_min_ndc_x() {
  return g_sky_clip_last_min_ndc_x;
}

extern "C" float wyd_d3d9_sky_clip_last_max_ndc_x() {
  return g_sky_clip_last_max_ndc_x;
}

extern "C" float wyd_d3d9_sky_clip_last_min_ndc_y() {
  return g_sky_clip_last_min_ndc_y;
}

extern "C" float wyd_d3d9_sky_clip_last_max_ndc_y() {
  return g_sky_clip_last_max_ndc_y;
}

extern "C" float wyd_d3d9_sky_clip_last_min_ndc_z() {
  return g_sky_clip_last_min_ndc_z;
}

extern "C" float wyd_d3d9_sky_clip_last_max_ndc_z() {
  return g_sky_clip_last_max_ndc_z;
}

extern "C" void wyd_d3d9_mark_next_draw_sky() {
  g_mark_next_draw_sky = true;
}

extern "C" uint32_t wyd_d3d9_fvf322_auto_clipw_draws() {
  return static_cast<uint32_t>(g_fvf322_auto_clipw_draws);
}

extern "C" uint32_t wyd_d3d9_fvf322_auto_clipw_reject_draws() {
  return static_cast<uint32_t>(g_fvf322_auto_clipw_reject_draws);
}

extern "C" uint32_t wyd_d3d9_fvf530_auto_clipw_draws() {
  return static_cast<uint32_t>(g_fvf530_auto_clipw_draws);
}

extern "C" uint32_t wyd_d3d9_fvf530_auto_clipw_reject_draws() {
  return static_cast<uint32_t>(g_fvf530_auto_clipw_reject_draws);
}

extern "C" uint32_t wyd_d3d9_fvf530_draws() {
  return static_cast<uint32_t>(g_fvf530_draws);
}

extern "C" uint32_t wyd_d3d9_fvf530_large_bounds_draws() {
  return static_cast<uint32_t>(g_fvf530_large_bounds_draws);
}

extern "C" uint32_t wyd_d3d9_fvf530_large_bounds_skip_draws() {
  return static_cast<uint32_t>(g_fvf530_large_bounds_skip_draws);
}

extern "C" uint32_t wyd_d3d9_fvf530_large_bound_sample_count() {
  return static_cast<uint32_t>(g_fvf530_large_bound_samples.size());
}

extern "C" const char* wyd_d3d9_fvf530_large_bound_sample(uint32_t index) {
  if (index >= g_fvf530_large_bound_samples.size()) return "";
  return g_fvf530_large_bound_samples[index].c_str();
}

extern "C" uint32_t wyd_d3d9_fvf530_unstable_w_draws() {
  return static_cast<uint32_t>(g_fvf530_unstable_w_draws);
}

extern "C" uint32_t wyd_d3d9_fvf530_vertices() {
  return static_cast<uint32_t>(g_fvf530_vertices);
}

extern "C" uint32_t wyd_d3d9_fvf530_inside_vertices() {
  return static_cast<uint32_t>(g_fvf530_inside_vertices);
}

extern "C" uint32_t wyd_d3d9_fvf530_last_vertex_count() {
  return g_fvf530_last_vertex_count;
}

extern "C" uint32_t wyd_d3d9_fvf530_last_index_count() {
  return g_fvf530_last_index_count;
}

extern "C" uint32_t wyd_d3d9_fvf530_last_stable_w_vertices() {
  return g_fvf530_last_stable_w_vertices;
}

extern "C" uint32_t wyd_d3d9_fvf530_last_inside_vertices() {
  return g_fvf530_last_inside_vertices;
}

extern "C" uint32_t wyd_d3d9_fvf530_last_large_ndc_vertices() {
  return g_fvf530_last_large_ndc_vertices;
}

extern "C" float wyd_d3d9_fvf530_last_min_ndc_x() {
  return g_fvf530_last_min_ndc_x;
}

extern "C" float wyd_d3d9_fvf530_last_max_ndc_x() {
  return g_fvf530_last_max_ndc_x;
}

extern "C" float wyd_d3d9_fvf530_last_min_ndc_y() {
  return g_fvf530_last_min_ndc_y;
}

extern "C" float wyd_d3d9_fvf530_last_max_ndc_y() {
  return g_fvf530_last_max_ndc_y;
}

extern "C" float wyd_d3d9_fvf530_last_min_ndc_z() {
  return g_fvf530_last_min_ndc_z;
}

extern "C" float wyd_d3d9_fvf530_last_max_ndc_z() {
  return g_fvf530_last_max_ndc_z;
}

extern "C" uint32_t wyd_d3d9_fvf594_auto_clipw_draws() {
  return static_cast<uint32_t>(g_fvf594_auto_clipw_draws);
}

extern "C" uint32_t wyd_d3d9_fvf594_auto_clipw_reject_draws() {
  return static_cast<uint32_t>(g_fvf594_auto_clipw_reject_draws);
}

extern "C" uint32_t wyd_d3d9_up_reset_stream0_calls() {
  return static_cast<uint32_t>(g_up_reset_stream0_calls);
}

extern "C" uint32_t wyd_d3d9_up_reset_indices_calls() {
  return static_cast<uint32_t>(g_up_reset_indices_calls);
}

extern "C" void wyd_d3d9_set_debug_flags(uint32_t flags) {
  g_debug_ffp_flags = flags;
}

extern "C" uint32_t wyd_d3d9_get_debug_flags() {
  return g_debug_ffp_flags;
}

extern "C" void wyd_d3d9_set_debug_skip_fvf(uint32_t fvf) {
  g_debug_skip_fvf = fvf;
}

extern "C" uint32_t wyd_d3d9_get_debug_skip_fvf() {
  return g_debug_skip_fvf;
}

extern "C" uint32_t wyd_d3d9_clear_calls() {
  return static_cast<uint32_t>(g_debug_clear_calls);
}

extern "C" uint32_t wyd_d3d9_present_calls() {
  return static_cast<uint32_t>(g_debug_present_calls);
}

extern "C" uint32_t wyd_d3d9_begin_scene_calls() {
  return static_cast<uint32_t>(g_debug_begin_scene_calls);
}

extern "C" uint32_t wyd_d3d9_end_scene_calls() {
  return static_cast<uint32_t>(g_debug_end_scene_calls);
}

extern "C" uint32_t wyd_d3d9_last_clear_color() {
  return g_debug_last_clear_color;
}

extern "C" uint32_t wyd_d3d9_last_clear_flags() {
  return g_debug_last_clear_flags;
}

extern "C" float wyd_d3d9_last_clear_z() {
  return g_debug_last_clear_z;
}

extern "C" uint32_t wyd_d3d9_last_clear_time_ms() {
  return g_debug_last_clear_time_ms;
}

extern "C" uint32_t wyd_d3d9_last_present_time_ms() {
  return g_debug_last_present_time_ms;
}

extern "C" uint32_t wyd_d3d9_last_present_blend_enabled() {
  return g_debug_last_present_blend_enabled;
}

extern "C" uint32_t wyd_d3d9_last_present_depth_enabled() {
  return g_debug_last_present_depth_enabled;
}

extern "C" uint32_t wyd_d3d9_last_present_depth_write() {
  return g_debug_last_present_depth_write;
}

extern "C" uint32_t wyd_d3d9_current_z_func() {
  return g_wasm_d3d9_state.z_func;
}

extern "C" uint32_t wyd_d3d9_last_present_alpha_test() {
  return g_debug_last_present_alpha_test;
}

extern "C" uint32_t wyd_d3d9_last_present_src_blend() {
  return g_debug_last_present_src_blend;
}

extern "C" uint32_t wyd_d3d9_last_present_dst_blend() {
  return g_debug_last_present_dst_blend;
}

extern "C" void wyd_d3d9_reset_debug_counters() {
  g_tex_decode_success = 0;
  g_tex_decode_fail = 0;
  g_tex_upload_count = 0;
  g_tex_draw_count = 0;
  g_tex_alpha_promoted_opaque = 0;
  g_draw_fvf_xyzrhw = 0;
  g_draw_fvf_weighted = 0;
  g_draw_fvf_tex2plus = 0;
  g_stage1_generated_tci_draws = 0;
  g_stage1_textransform_draws = 0;
  g_stage1_tci0_draws = 0;
  g_stage1_tci1_draws = 0;
  g_stage1_tci_other_draws = 0;
  g_decl_draw_calls = 0;
  g_decl_skinned_draw_calls = 0;
  g_decl_vertices_total = 0;
  g_decl_vertices_in_clip = 0;
  g_decl_rgba_index_order_draws = 0;
  g_decl_bgra_index_order_draws = 0;
  g_invalid_indexed_draws = 0;
  g_invalid_indices_total = 0;
  g_clip_w_reject_draws = 0;
  g_clip_w_reject_triangles = 0;
  g_clip_w_keep_triangles = 0;
  g_fvf_3d_vertices_total = 0;
  g_fvf_3d_vertices_in_clip = 0;
  g_draw_alpha_test_enabled = 0;
  g_draw_alpha_test_disabled = 0;
  g_draw_blend_enabled = 0;
  g_draw_depth_test_disabled = 0;
  g_draw_depth_write_disabled = 0;
  g_depth_write_guard_forced_draws = 0;
  g_draw_order_serial = 0;
  g_draw_order_first_sky = 0;
  g_draw_order_first_skin = 0;
  g_draw_order_first_terrain594 = 0;
  g_draw_order_first_water578 = 0;
  g_draw_order_first_fvf530 = 0;
  g_draw_order_first_fvf322 = 0;
  g_draw_order_count_sky = 0;
  g_draw_order_count_skin = 0;
  g_draw_order_count_terrain594 = 0;
  g_draw_order_count_water578 = 0;
  g_draw_order_count_fvf530 = 0;
  g_draw_order_count_fvf322 = 0;
  ResetDrawOrderFrame();
  g_draw_lighting_enabled = 0;
  g_draw_stage0_color_selectarg1 = 0;
  g_draw_stage0_color_modulate = 0;
  g_draw_stage0_alpha_selectarg1 = 0;
  g_draw_stage0_alpha_modulate = 0;
  g_draw_cull_none = 0;
  g_draw_cull_cw = 0;
  g_draw_cull_ccw = 0;
  g_cull_mirror_worldview_draws = 0;
  g_cull_frontface_flipped_draws = 0;
  g_material_set_calls = 0;
  g_light_set_calls = 0;
  g_light_enable_calls = 0;
  g_lighted_vertices = 0;
  g_draw_fog_enabled = 0;
  g_draw_wireframe = 0;
  g_debug_skip_fvf_draws = 0;
  g_fvf322_draw_primitive_up = 0;
  g_fvf322_draw_indexed_primitive_up = 0;
  g_fvf322_draw_indexed_primitive = 0;
  g_fvf322_with_stage0_texture = 0;
  g_fvf322_without_stage0_texture = 0;
  g_fvf322_screenlike_vertices = 0;
  g_fvf322_screenlike_draws = 0;
  g_fvf322_screenlike_replay_draws = 0;
  g_fvf322_screenlike_replay_suppressed = 0;
  g_fvf322_auto_clipw_draws = 0;
  g_fvf322_auto_clipw_reject_draws = 0;
  g_fvf530_auto_clipw_draws = 0;
  g_fvf530_auto_clipw_reject_draws = 0;
  g_fvf530_draws = 0;
  g_fvf530_large_bounds_draws = 0;
  g_fvf530_large_bounds_skip_draws = 0;
  g_fvf530_unstable_w_draws = 0;
  g_fvf530_inside_vertices = 0;
  g_fvf530_vertices = 0;
  g_fvf530_large_bound_samples.clear();
  g_fvf530_large_bound_seen.clear();
  g_fvf530_last_vertex_count = 0;
  g_fvf530_last_index_count = 0;
  g_fvf530_last_stable_w_vertices = 0;
  g_fvf530_last_inside_vertices = 0;
  g_fvf530_last_large_ndc_vertices = 0;
  g_fvf530_last_min_ndc_x = 0.0f;
  g_fvf530_last_max_ndc_x = 0.0f;
  g_fvf530_last_min_ndc_y = 0.0f;
  g_fvf530_last_max_ndc_y = 0.0f;
  g_fvf530_last_min_ndc_z = 0.0f;
  g_fvf530_last_max_ndc_z = 0.0f;
  g_fvf594_auto_clipw_draws = 0;
  g_fvf594_auto_clipw_reject_draws = 0;
  g_up_reset_stream0_calls = 0;
  g_up_reset_indices_calls = 0;
  g_vs_telemetry.clear();
  g_ps_telemetry.clear();
  g_vs_bind_calls = 0;
  g_ps_bind_calls = 0;
  g_draws_with_vs = 0;
  g_draws_with_ps = 0;
  g_active_vs_hash = 0;
  g_active_ps_hash = 0;
  g_shader_file_open_attempts = 0;
  g_shader_file_open_success = 0;
  g_shader_file_open_fail = 0;
  g_shader_file_open_skinmesh = 0;
  g_shader_file_open_vseffect = 0;
  g_shader_file_open_pseffect = 0;
  g_asset_file_open_attempts = 0;
  g_asset_file_open_success = 0;
  g_asset_file_open_fail = 0;
  g_asset_file_open_fail_mesh = 0;
  g_asset_file_open_fail_env = 0;
  g_asset_file_open_fail_ui = 0;
  g_asset_file_open_fail_texture = 0;
  g_asset_file_open_fail_sound = 0;
  g_asset_file_open_fail_samples.clear();
  g_asset_file_open_fail_seen.clear();
  g_asset_path_fallback_attempts = 0;
  g_asset_path_fallback_hits = 0;
  g_asset_path_fallback_or010_hits = 0;
  g_asset_path_fallback_samples.clear();
  g_asset_path_fallback_seen.clear();
  g_fd_debug_source_paths.clear();
  g_last_closed_debug_source_path.clear();
  g_gl_error_total = 0;
  g_gl_error_draw_calls = 0;
  g_gl_error_last = 0;
  g_debug_clear_calls = 0;
  g_debug_present_calls = 0;
  g_debug_begin_scene_calls = 0;
  g_debug_end_scene_calls = 0;
  g_debug_last_clear_color = 0;
  g_debug_last_clear_flags = 0;
  g_debug_last_clear_z = 1.0f;
  g_debug_last_clear_time_ms = 0;
  g_debug_last_present_time_ms = 0;
  g_debug_last_present_blend_enabled = 0;
  g_debug_last_present_depth_enabled = 0;
  g_debug_last_present_depth_write = 0;
  g_debug_last_present_alpha_test = 0;
  g_debug_last_present_src_blend = 0;
  g_debug_last_present_dst_blend = 0;
  g_texture_draws_sky = 0;
  g_texture_draws_water = 0;
  g_texture_draws_bright = 0;
  g_fvf322_bright_draws = 0;
  g_fvf322_class_draws.fill(0);
  g_stage0_colorop8_draws = 0;
  g_stage0_colorop8_with_texture = 0;
  g_stage0_colorop8_without_texture = 0;
  g_stage0_colorop8_pathless_texture = 0;
  g_stage0_colorop8_last_fvf = 0;
  g_stage0_colorop8_last_width = 0;
  g_stage0_colorop8_last_height = 0;
  g_stage0_colorop8_last_path_len = 0;
  g_set_stage0_colorop8_calls = 0;
  g_set_stage0_colorop4_calls = 0;
  g_set_stage0_colorop_last_value = 0;
  g_set_texture_stage0_sky_calls = 0;
  g_set_texture_stage1_sky_calls = 0;
  g_draw_attempts_with_sky_texture = 0;
  g_draw_attempts_with_sky_texture_indexed = 0;
  g_draw_attempts_with_sky_texture_up = 0;
  g_draw_attempts_with_sky_last_fvf = 0;
  g_sky_clip_draws = 0;
  g_sky_clip_last_vertex_count = 0;
  g_sky_clip_last_index_count = 0;
  g_sky_clip_last_stable_w_vertices = 0;
  g_sky_clip_last_negative_w_vertices = 0;
  g_sky_clip_last_near_w_vertices = 0;
  g_sky_clip_last_inside_vertices = 0;
  g_sky_clip_last_large_ndc_vertices = 0;
  g_sky_clip_last_triangle_count = 0;
  g_sky_clip_last_triangles_all_stable_w = 0;
  g_sky_clip_last_triangles_any_unstable_w = 0;
  g_sky_clip_last_triangles_any_outside = 0;
  g_sky_clip_last_min_w = 0.0f;
  g_sky_clip_last_max_w = 0.0f;
  g_sky_clip_last_min_ndc_x = 0.0f;
  g_sky_clip_last_max_ndc_x = 0.0f;
  g_sky_clip_last_min_ndc_y = 0.0f;
  g_sky_clip_last_max_ndc_y = 0.0f;
  g_sky_clip_last_min_ndc_z = 0.0f;
  g_sky_clip_last_max_ndc_z = 0.0f;
  g_mark_next_draw_sky = false;
  g_clipw_empty_signature_histogram.clear();
  g_clipw_empty_signature_export_cache.clear();
  g_skin_suspicious_texture_draws = 0;
  g_skin_suspicious_texture_samples.clear();
  g_skin_suspicious_texture_seen.clear();
  g_fvf_histogram.clear();
  g_fvf_depth_write_enabled_histogram.clear();
  g_fvf_depth_write_disabled_histogram.clear();
  ResetDrawTraceResults(false);
  g_ffp_state.draw_calls = 0;
  g_ffp_state.primitive_count = 0;
  g_ffp_state.shader_draw_skipped = 0;
}
