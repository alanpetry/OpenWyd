#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

namespace {

constexpr int kFontHeight = 12;
constexpr int kFontWeight = 500;
constexpr DWORD kFontCharSet = DEFAULT_CHARSET;
constexpr DWORD kFontOutPrecision = OUT_TT_PRECIS;
constexpr DWORD kFontClipPrecision = CLIP_DEFAULT_PRECIS;
constexpr DWORD kFontQuality = ANTIALIASED_QUALITY;
constexpr DWORD kFontPitchAndFamily = VARIABLE_PITCH;
constexpr char kFontFace[] = "Tahoma";
constexpr int kAtlasColumns = 16;
constexpr int kAtlasRows = 16;
constexpr int kCellWidth = 32;
constexpr int kCellHeight = 16;
constexpr int kGlyphOriginX = 8;
constexpr int kGlyphOriginY = 0;
constexpr int kAtlasWidth = kAtlasColumns * kCellWidth;
constexpr int kAtlasHeight = kAtlasRows * kCellHeight;
constexpr int kPackedRowBytes = kAtlasWidth / 2;
constexpr std::array<char, 8> kMagic = {'O', 'W', 'G', 'D', 'A', '4', '\r', '\n'};

#pragma pack(push, 1)
struct AtlasHeader {
  char magic[8];
  std::uint32_t version;
  std::uint32_t header_size;
  std::int32_t font_height;
  std::int32_t font_width;
  std::int32_t font_escapement;
  std::int32_t font_orientation;
  std::int32_t font_weight;
  std::uint32_t font_italic;
  std::uint32_t font_underline;
  std::uint32_t font_strikeout;
  std::uint32_t font_charset;
  std::uint32_t font_out_precision;
  std::uint32_t font_clip_precision;
  std::uint32_t font_quality;
  std::uint32_t font_pitch_and_family;
  std::uint32_t atlas_width;
  std::uint32_t atlas_height;
  std::uint32_t packed_row_bytes;
  std::uint32_t cell_width;
  std::uint32_t cell_height;
  std::uint32_t columns;
  std::uint32_t rows;
  std::uint32_t glyph_origin_x;
  std::uint32_t glyph_origin_y;
  std::uint32_t glyph_count;
  std::uint32_t metric_size;
  std::uint32_t metrics_offset;
  std::uint32_t pixels_offset;
  std::uint32_t pixels_size;
  std::int32_t text_height;
  std::int32_t text_ascent;
  std::int32_t text_descent;
  std::int32_t text_internal_leading;
  std::int32_t text_external_leading;
  std::int32_t text_average_width;
  std::int32_t text_maximum_width;
  std::uint32_t windows_ansi_code_page;
  std::uint64_t pixels_fnv1a64;
  std::uint8_t font_data_sha256[32];
};

struct GlyphMetric {
  std::int16_t advance;
  std::int16_t abc_a;
  std::int16_t abc_b;
  std::int16_t abc_c;
  std::int16_t extent_width;
  std::int16_t extent_height;
  std::uint32_t alpha_pixels;
};
#pragma pack(pop)

static_assert(sizeof(GlyphMetric) == 16, "The WASM reader requires 16-byte glyph metrics");

class GdiObjects {
 public:
  GdiObjects() {
    dc = CreateCompatibleDC(nullptr);
    if (!dc) throw std::runtime_error("CreateCompatibleDC failed");

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = kCellWidth;
    info.bmiHeader.biHeight = -kCellHeight;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS,
                              reinterpret_cast<void**>(&pixels), nullptr, 0);
    if (!bitmap || !pixels) throw std::runtime_error("CreateDIBSection failed");
    old_bitmap = SelectObject(dc, bitmap);

    // These are the exact arguments used by TMFont2/RenderDevice.
    font = CreateFontA(kFontHeight, 0, 0, 0, kFontWeight, FALSE, FALSE, FALSE,
                       kFontCharSet, kFontOutPrecision, kFontClipPrecision,
                       kFontQuality, kFontPitchAndFamily, kFontFace);
    if (!font) throw std::runtime_error("CreateFontA(Tahoma) failed");
    old_font = SelectObject(dc, font);
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkColor(dc, RGB(0, 0, 0));
    SetBkMode(dc, OPAQUE);
    SetTextAlign(dc, TA_LEFT | TA_TOP | TA_NOUPDATECP);
  }

  ~GdiObjects() {
    if (dc && old_font) SelectObject(dc, old_font);
    if (dc && old_bitmap) SelectObject(dc, old_bitmap);
    if (font) DeleteObject(font);
    if (bitmap) DeleteObject(bitmap);
    if (dc) DeleteDC(dc);
  }

  HDC dc = nullptr;
  HFONT font = nullptr;
  HBITMAP bitmap = nullptr;
  HGDIOBJ old_font = nullptr;
  HGDIOBJ old_bitmap = nullptr;
  std::uint32_t* pixels = nullptr;
};

std::array<std::uint8_t, 32> Sha256(const void* data, std::size_t size) {
  BCRYPT_ALG_HANDLE algorithm = nullptr;
  BCRYPT_HASH_HANDLE hash = nullptr;
  DWORD object_size = 0;
  DWORD hash_size = 0;
  DWORD received = 0;
  std::vector<std::uint8_t> object;
  std::array<std::uint8_t, 32> digest{};

  auto check = [](NTSTATUS status, const char* operation) {
    if (status < 0) {
      std::ostringstream message;
      message << operation << " failed with NTSTATUS 0x"
              << std::hex << static_cast<unsigned long>(status);
      throw std::runtime_error(message.str());
    }
  };

  check(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0),
        "BCryptOpenAlgorithmProvider");
  try {
    check(BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                            reinterpret_cast<PUCHAR>(&object_size),
                            sizeof(object_size), &received, 0),
          "BCryptGetProperty(BCRYPT_OBJECT_LENGTH)");
    check(BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                            reinterpret_cast<PUCHAR>(&hash_size),
                            sizeof(hash_size), &received, 0),
          "BCryptGetProperty(BCRYPT_HASH_LENGTH)");
    if (hash_size != digest.size())
      throw std::runtime_error("unexpected SHA-256 digest length");
    object.resize(object_size);
    check(BCryptCreateHash(algorithm, &hash, object.data(), object_size,
                           nullptr, 0, 0),
          "BCryptCreateHash");
    if (size != 0) {
      check(BCryptHashData(hash,
                           const_cast<PUCHAR>(
                               static_cast<const unsigned char*>(data)),
                           static_cast<ULONG>(size), 0),
            "BCryptHashData");
    }
    check(BCryptFinishHash(hash, digest.data(),
                           static_cast<ULONG>(digest.size()), 0),
          "BCryptFinishHash");
  } catch (...) {
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    throw;
  }
  BCryptDestroyHash(hash);
  BCryptCloseAlgorithmProvider(algorithm, 0);
  return digest;
}

std::string Hex(const std::uint8_t* data, std::size_t size) {
  std::ostringstream result;
  result << std::hex << std::setfill('0');
  for (std::size_t i = 0; i < size; ++i)
    result << std::setw(2) << static_cast<unsigned>(data[i]);
  return result.str();
}

std::uint64_t Fnv1a64(const std::vector<std::uint8_t>& data) {
  std::uint64_t value = 14695981039346656037ull;
  for (std::uint8_t byte : data) {
    value ^= byte;
    value *= 1099511628211ull;
  }
  return value;
}

void SetPackedPixel(std::vector<std::uint8_t>& pixels, int x, int y,
                    std::uint8_t alpha) {
  const std::size_t offset =
      static_cast<std::size_t>(y * kPackedRowBytes + x / 2);
  if ((x & 1) == 0)
    pixels[offset] = static_cast<std::uint8_t>(
        (pixels[offset] & 0x0f) | ((alpha & 0x0f) << 4));
  else
    pixels[offset] =
        static_cast<std::uint8_t>((pixels[offset] & 0xf0) | (alpha & 0x0f));
}

std::uint8_t PackedPixel(const std::vector<std::uint8_t>& pixels, int x, int y) {
  const std::uint8_t packed =
      pixels[static_cast<std::size_t>(y * kPackedRowBytes + x / 2)];
  return (x & 1) == 0 ? static_cast<std::uint8_t>(packed >> 4)
                      : static_cast<std::uint8_t>(packed & 0x0f);
}

std::vector<std::uint8_t> RealizedFontData(HDC dc) {
  const DWORD size = GetFontData(dc, 0, 0, nullptr, 0);
  if (size == GDI_ERROR || size == 0)
    throw std::runtime_error("GetFontData could not read the realized Tahoma font");
  std::vector<std::uint8_t> data(size);
  if (GetFontData(dc, 0, 0, data.data(), size) == GDI_ERROR)
    throw std::runtime_error("GetFontData failed while reading the realized font");
  return data;
}

struct GeneratedAtlas {
  AtlasHeader header{};
  std::array<GlyphMetric, 256> metrics{};
  std::vector<std::uint8_t> pixels =
      std::vector<std::uint8_t>(
          static_cast<std::size_t>(kPackedRowBytes * kAtlasHeight), 0);
  std::array<std::uint64_t, 16> histogram{};
  std::string realized_face;
};

GeneratedAtlas Generate(GdiObjects& gdi) {
  GeneratedAtlas result;
  std::copy(kMagic.begin(), kMagic.end(), result.header.magic);
  result.header.version = 1;
  result.header.header_size = sizeof(AtlasHeader);
  result.header.font_height = kFontHeight;
  result.header.font_weight = kFontWeight;
  result.header.font_charset = kFontCharSet;
  result.header.font_out_precision = kFontOutPrecision;
  result.header.font_clip_precision = kFontClipPrecision;
  result.header.font_quality = kFontQuality;
  result.header.font_pitch_and_family = kFontPitchAndFamily;
  result.header.atlas_width = kAtlasWidth;
  result.header.atlas_height = kAtlasHeight;
  result.header.packed_row_bytes = kPackedRowBytes;
  result.header.cell_width = kCellWidth;
  result.header.cell_height = kCellHeight;
  result.header.columns = kAtlasColumns;
  result.header.rows = kAtlasRows;
  result.header.glyph_origin_x = kGlyphOriginX;
  result.header.glyph_origin_y = kGlyphOriginY;
  result.header.glyph_count = 256;
  result.header.metric_size = sizeof(GlyphMetric);
  result.header.metrics_offset = sizeof(AtlasHeader);
  result.header.pixels_offset =
      sizeof(AtlasHeader) + sizeof(GlyphMetric) * result.metrics.size();
  result.header.pixels_size = static_cast<std::uint32_t>(result.pixels.size());
  result.header.windows_ansi_code_page = GetACP();

  TEXTMETRICA tm{};
  if (!GetTextMetricsA(gdi.dc, &tm))
    throw std::runtime_error("GetTextMetricsA failed");
  result.header.text_height = tm.tmHeight;
  result.header.text_ascent = tm.tmAscent;
  result.header.text_descent = tm.tmDescent;
  result.header.text_internal_leading = tm.tmInternalLeading;
  result.header.text_external_leading = tm.tmExternalLeading;
  result.header.text_average_width = tm.tmAveCharWidth;
  result.header.text_maximum_width = tm.tmMaxCharWidth;

  char face[LF_FACESIZE]{};
  if (GetTextFaceA(gdi.dc, LF_FACESIZE, face) <= 0)
    throw std::runtime_error("GetTextFaceA failed");
  result.realized_face = face;

  const std::vector<std::uint8_t> font_data = RealizedFontData(gdi.dc);
  const auto font_hash = Sha256(font_data.data(), font_data.size());
  std::copy(font_hash.begin(), font_hash.end(), result.header.font_data_sha256);

  for (unsigned glyph = 0; glyph < 256; ++glyph) {
    std::fill(gdi.pixels, gdi.pixels + kCellWidth * kCellHeight, 0);
    GlyphMetric metric{};
    const char byte = static_cast<char>(glyph);
    SIZE extent{};
    // TMFont2 sends display bytes, not C0/DEL control characters.  GDI's
    // reported advances for those controls depend on mutable DC internals and
    // are not stable across processes, so keep them explicitly empty.
    const bool display_byte = glyph >= 32 && glyph != 127;
    if (display_byte) {
      if (!GetTextExtentPoint32A(gdi.dc, &byte, 1, &extent))
        throw std::runtime_error("GetTextExtentPoint32A failed");
      ABC abc{};
      if (GetCharABCWidthsA(gdi.dc, glyph, glyph, &abc)) {
        metric.abc_a = static_cast<std::int16_t>(abc.abcA);
        metric.abc_b = static_cast<std::int16_t>(abc.abcB);
        metric.abc_c = static_cast<std::int16_t>(abc.abcC);
      }
      if (!TextOutA(gdi.dc, kGlyphOriginX, kGlyphOriginY, &byte, 1))
        throw std::runtime_error("TextOutA failed while generating a glyph");
    }
    metric.advance = static_cast<std::int16_t>(extent.cx);
    metric.extent_width = static_cast<std::int16_t>(extent.cx);
    metric.extent_height = static_cast<std::int16_t>(extent.cy);

    const int cell_x = static_cast<int>(glyph % kAtlasColumns) * kCellWidth;
    const int cell_y = static_cast<int>(glyph / kAtlasColumns) * kCellHeight;
    for (int y = 0; y < kCellHeight; ++y) {
      for (int x = 0; x < kCellWidth; ++x) {
        const std::uint8_t alpha =
            static_cast<std::uint8_t>(gdi.pixels[y * kCellWidth + x] & 0xffu) >> 4;
        SetPackedPixel(result.pixels, cell_x + x, cell_y + y, alpha);
        ++result.histogram[alpha];
        if (alpha != 0) ++metric.alpha_pixels;
      }
    }
    result.metrics[glyph] = metric;
  }
  result.header.pixels_fnv1a64 = Fnv1a64(result.pixels);
  return result;
}

std::vector<std::string> SelfTestCorpus() {
  std::vector<std::string> corpus = {
      "Insira Senha(2)", "Confirmar", "Cancelar", "Alterar", "Voltar",
      "Reino", "Normal", "Cidadao", "PVT", "Guilda", "Nv", "TKNATIVE",
      "0123456789", "000/000", "ABCDEFGHIJKLMNOPQRSTUVWXYZ",
      "abcdefghijklmnopqrstuvwxyz", "                                             ",
  };
  std::string cp1252;
  const unsigned char portuguese[] = {
      'C', 'o', 'n', 'e', 'x', 0xE3, 'o', ' ', 'n', 0xE3, 'o', ' ',
      'e', 's', 't', 0xE1, ' ', 'd', 'i', 's', 'p', 'o', 'n', 0xED,
      'v', 'e', 'l', ':', ' ', 0xC7, 0xE1, ' ', 0xD3, 'r', 'g', 0xE3,
      'o', ' ', 0xFA, 'n', 'i', 'c', 'o', 0};
  cp1252.assign(reinterpret_cast<const char*>(portuguese));
  corpus.push_back(cp1252);

  for (unsigned begin = 32; begin < 127; begin += 32) {
    std::string bytes;
    for (unsigned value = begin; value < std::min(127u, begin + 32); ++value)
      bytes.push_back(static_cast<char>(value));
    corpus.push_back(bytes);
  }
  for (unsigned begin = 128; begin < 256; begin += 32) {
    std::string bytes;
    for (unsigned value = begin; value < begin + 32; ++value)
      bytes.push_back(static_cast<char>(value));
    corpus.push_back(bytes);
  }
  return corpus;
}

void VerifyComposition(GdiObjects& gdi, const GeneratedAtlas& atlas) {
  constexpr int kWidth = 1024;
  constexpr int kHeight = kCellHeight;
  std::vector<std::uint8_t> direct(kWidth * kHeight);
  std::vector<std::uint8_t> composed(kWidth * kHeight);

  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = kWidth;
  info.bmiHeader.biHeight = -kHeight;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  std::uint32_t* pixels = nullptr;
  HBITMAP bitmap = CreateDIBSection(gdi.dc, &info, DIB_RGB_COLORS,
                                    reinterpret_cast<void**>(&pixels), nullptr, 0);
  if (!bitmap || !pixels)
    throw std::runtime_error("self-test CreateDIBSection failed");
  HGDIOBJ previous = SelectObject(gdi.dc, bitmap);
  try {
    auto atlas_width = [&atlas](const std::string& text) {
      int width = 0;
      for (unsigned char byte : text)
        width += atlas.metrics[byte].advance;
      return width;
    };
    auto atlas_text_out = [&](
                              int destination_x,
                              int destination_y,
                              const std::string& text,
                              bool transparent) {
      int width = 0;
      for (unsigned char byte : text)
        width += atlas.metrics[byte].advance;
      if (!transparent) {
        for (int row = 0; row < atlas.header.text_height; ++row) {
          for (int column = 0; column < width; ++column) {
            const int x = destination_x + column;
            const int y = destination_y + row;
            if (x >= 0 && x < kWidth && y >= 0 && y < kHeight)
              composed[y * kWidth + x] = 0;
          }
        }
      }

      int pen_x = destination_x;
      for (unsigned char byte : text) {
        const int cell_x = (byte % kAtlasColumns) * kCellWidth;
        const int cell_y = (byte / kAtlasColumns) * kCellHeight;
        for (int y = 0; y < kCellHeight; ++y) {
          for (int x = 0; x < kCellWidth; ++x) {
            const std::uint8_t source =
                PackedPixel(atlas.pixels, cell_x + x, cell_y + y);
            const int output_x = pen_x + x - kGlyphOriginX;
            const int output_y = destination_y + y - kGlyphOriginY;
            if (!source || output_x < 0 || output_x >= kWidth ||
                output_y < 0 || output_y >= kHeight)
              continue;
            std::uint8_t& destination =
                composed[output_y * kWidth + output_x];
            if (transparent) {
              destination = static_cast<std::uint8_t>(
                  std::min(
                      15,
                      static_cast<int>(source) +
                          (static_cast<int>(destination) * (15 - source) + 7) /
                              15));
            } else {
              destination = std::max(destination, source);
            }
          }
        }
        pen_x += atlas.metrics[byte].advance;
      }
    };

    for (const std::string& text : SelfTestCorpus()) {
      std::fill(pixels, pixels + kWidth * kHeight, 0);
      RECT fill_rect{0, 0, kWidth, kHeight};
      if (!FillRect(
              gdi.dc,
              &fill_rect,
              static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH))))
        throw std::runtime_error("self-test FillRect(BLACK_BRUSH) failed");

      const std::string spaces(45, ' ');
      const std::string displayed = text + " ";
      std::string zero_overlay(displayed.size(), ' ');
      bool has_zero = false;
      for (std::size_t index = 0; index < displayed.size(); ++index) {
        if (displayed[index] == '0') {
          zero_overlay[index] = '/';
          has_zero = true;
        }
      }

      SetBkMode(gdi.dc, OPAQUE);
      if (!TextOutA(gdi.dc, 0, 0, spaces.data(),
                    static_cast<int>(spaces.size())) ||
          !TextOutA(gdi.dc, 0, 0, displayed.data(),
                    static_cast<int>(displayed.size())))
        throw std::runtime_error("TMFont2-sequence OPAQUE TextOutA failed");
      if (has_zero) {
        SetBkMode(gdi.dc, TRANSPARENT);
        if (!TextOutA(gdi.dc, 0, 0, zero_overlay.data(),
                      static_cast<int>(zero_overlay.size())))
          throw std::runtime_error(
              "TMFont2-sequence TRANSPARENT TextOutA failed");
        SetBkMode(gdi.dc, OPAQUE);
      }
      for (int i = 0; i < kWidth * kHeight; ++i)
        direct[i] = static_cast<std::uint8_t>(pixels[i] & 0xffu) >> 4;

      std::fill(composed.begin(), composed.end(), 0);
      atlas_text_out(0, 0, spaces, false);
      atlas_text_out(0, 0, displayed, false);
      if (has_zero)
        atlas_text_out(0, 0, zero_overlay, true);

      SIZE direct_extent{};
      if (!GetTextExtentPoint32A(gdi.dc, text.data(),
                                 static_cast<int>(text.size()), &direct_extent))
        throw std::runtime_error("self-test GetTextExtentPoint32A failed");
      const int composed_extent = atlas_width(text);
      if (direct_extent.cx != composed_extent) {
        std::ostringstream message;
        message << "atlas self-test width mismatch: GDI=" << direct_extent.cx
                << " atlas=" << composed_extent;
        throw std::runtime_error(message.str());
      }
      const auto mismatch =
          std::mismatch(direct.begin(), direct.end(), composed.begin());
      if (mismatch.first != direct.end()) {
        const std::size_t offset =
            static_cast<std::size_t>(mismatch.first - direct.begin());
        std::ostringstream message;
        message << "atlas self-test pixel mismatch at (" << (offset % kWidth)
                << "," << (offset / kWidth) << ") GDI="
                << static_cast<unsigned>(*mismatch.first) << " atlas="
                << static_cast<unsigned>(composed[offset]);
        throw std::runtime_error(message.str());
      }
    }

    auto verify_plain_text_out = [&](const std::string& text,
                                     const char* label) {
      std::fill(pixels, pixels + kWidth * kHeight, 0);
      SetBkMode(gdi.dc, OPAQUE);
      if (!TextOutA(
              gdi.dc, 0, 0, text.data(), static_cast<int>(text.size())))
        throw std::runtime_error(
            std::string(label) + " TextOutA failed");
      for (int index = 0; index < kWidth * kHeight; ++index)
        direct[index] =
            static_cast<std::uint8_t>(pixels[index] & 0xffu) >> 4;

      std::fill(composed.begin(), composed.end(), 0);
      atlas_text_out(0, 0, text, false);
      SIZE direct_extent{};
      if (!GetTextExtentPoint32A(
              gdi.dc, text.data(), static_cast<int>(text.size()),
              &direct_extent))
        throw std::runtime_error(
            std::string(label) + " GetTextExtentPoint32A failed");
      const int composed_extent = atlas_width(text);
      if (direct_extent.cx != composed_extent) {
        std::ostringstream message;
        message << label << " width mismatch for bytes";
        for (unsigned char byte : text)
          message << " 0x" << std::hex << std::setw(2)
                  << std::setfill('0') << static_cast<unsigned>(byte);
        message << std::dec << ": GDI=" << direct_extent.cx
                << " atlas=" << composed_extent;
        throw std::runtime_error(message.str());
      }
      const auto mismatch =
          std::mismatch(direct.begin(), direct.end(), composed.begin());
      if (mismatch.first != direct.end()) {
        const std::size_t offset =
            static_cast<std::size_t>(mismatch.first - direct.begin());
        std::ostringstream message;
        message << label << " pixel mismatch for bytes";
        for (unsigned char byte : text)
          message << " 0x" << std::hex << std::setw(2)
                  << std::setfill('0') << static_cast<unsigned>(byte);
        message << std::dec << " at (" << (offset % kWidth)
                << "," << (offset / kWidth) << ") GDI="
                << static_cast<unsigned>(*mismatch.first) << " atlas="
                << static_cast<unsigned>(composed[offset]);
        throw std::runtime_error(message.str());
      }
    };

    std::vector<unsigned char> display_bytes;
    for (unsigned value = 32; value <= 255; ++value) {
      if (value != 127)
        display_bytes.push_back(static_cast<unsigned char>(value));
    }
    for (unsigned char first : display_bytes) {
      for (unsigned char second : display_bytes) {
        std::string pair;
        pair.push_back(static_cast<char>(first));
        pair.push_back(static_cast<char>(second));
        verify_plain_text_out(pair, "exhaustive display-byte pair");
      }
    }

    const DWORD kerning_count = GetKerningPairsA(gdi.dc, 0, nullptr);
    std::vector<KERNINGPAIR> kerning_pairs(kerning_count);
    if (kerning_count != 0 &&
        GetKerningPairsA(
            gdi.dc, kerning_count, kerning_pairs.data()) != kerning_count)
      throw std::runtime_error("GetKerningPairsA self-test query changed");
    std::vector<std::string> negative_pair_chains;
    for (const KERNINGPAIR& first : kerning_pairs) {
      if (first.iKernAmount >= 0 ||
          first.wFirst < 32 || first.wFirst > 126 ||
          first.wSecond < 32 || first.wSecond > 126)
        continue;
      for (const KERNINGPAIR& second : kerning_pairs) {
        if (second.iKernAmount >= 0 ||
            second.wFirst != first.wSecond ||
            second.wSecond < 32 || second.wSecond > 126)
          continue;
        std::string triple;
        triple.push_back(static_cast<char>(first.wFirst));
        triple.push_back(static_cast<char>(first.wSecond));
        triple.push_back(static_cast<char>(second.wSecond));
        if (std::find(
                negative_pair_chains.begin(),
                negative_pair_chains.end(),
                triple) == negative_pair_chains.end())
          negative_pair_chains.push_back(triple);
        if (negative_pair_chains.size() >= 8) break;
      }
      if (negative_pair_chains.size() >= 8) break;
    }
    if (negative_pair_chains.size() < 3)
      throw std::runtime_error(
          "Tahoma exposed fewer than three chained negative kerning triples");
    for (const std::string& triple : negative_pair_chains)
      verify_plain_text_out(triple, "negative-kerning triple");
  } catch (...) {
    SetBkMode(gdi.dc, OPAQUE);
    SelectObject(gdi.dc, previous);
    DeleteObject(bitmap);
    throw;
  }
  SetBkMode(gdi.dc, OPAQUE);
  SelectObject(gdi.dc, previous);
  DeleteObject(bitmap);
}

std::vector<std::uint8_t> Serialize(const GeneratedAtlas& atlas) {
  std::vector<std::uint8_t> result(
      atlas.header.pixels_offset + atlas.header.pixels_size);
  std::memcpy(result.data(), &atlas.header, sizeof(atlas.header));
  std::memcpy(result.data() + atlas.header.metrics_offset, atlas.metrics.data(),
              sizeof(atlas.metrics));
  std::memcpy(result.data() + atlas.header.pixels_offset, atlas.pixels.data(),
              atlas.pixels.size());
  return result;
}

void WriteBinary(const std::string& path, const std::vector<std::uint8_t>& data) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) throw std::runtime_error("could not open atlas output: " + path);
  output.write(reinterpret_cast<const char*>(data.data()),
               static_cast<std::streamsize>(data.size()));
  if (!output) throw std::runtime_error("could not write atlas output: " + path);
}

void WriteManifest(const std::string& path, const GeneratedAtlas& atlas,
                   const std::array<std::uint8_t, 32>& atlas_hash,
                   std::size_t atlas_size) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) throw std::runtime_error("could not open manifest output: " + path);
  output << "{\n"
         << "  \"schema\": \"openwyd.gdi-font-atlas\",\n"
         << "  \"schemaVersion\": 1,\n"
         << "  \"selfTest\": {\"passed\": true, \"mode\": "
            "\"TMFont2 sequence plus exhaustive display-byte pairs and "
            "negative-kerning triples equal TextOutA\", "
            "\"displayByteCount\": 223, \"orderedPairCount\": 49729, "
            "\"negativeKerningTripleMinimum\": 3},\n"
         << "  \"font\": {\"requestedFace\": \"Tahoma\", \"realizedFace\": \""
         << atlas.realized_face << "\", \"height\": " << kFontHeight
         << ", \"weight\": " << kFontWeight
         << ", \"charset\": " << kFontCharSet
         << ", \"outPrecision\": " << kFontOutPrecision
         << ", \"clipPrecision\": " << kFontClipPrecision
         << ", \"quality\": " << kFontQuality
         << ", \"pitchAndFamily\": " << kFontPitchAndFamily
         << ", \"windowsAnsiCodePage\": " << atlas.header.windows_ansi_code_page
         << ", \"fontDataSha256\": \""
         << Hex(atlas.header.font_data_sha256,
                sizeof(atlas.header.font_data_sha256)) << "\"},\n"
         << "  \"atlas\": {\"format\": \"A4\", \"width\": " << kAtlasWidth
         << ", \"height\": " << kAtlasHeight
         << ", \"cellWidth\": " << kCellWidth
         << ", \"cellHeight\": " << kCellHeight
         << ", \"glyphCount\": 256, \"byteSize\": " << atlas_size
         << ", \"sha256\": \"" << Hex(atlas_hash.data(), atlas_hash.size())
         << "\", \"pixelsFnv1a64\": \"" << std::hex << std::setfill('0')
         << std::setw(16) << atlas.header.pixels_fnv1a64 << std::dec << "\"},\n"
         << "  \"textMetrics\": {\"height\": " << atlas.header.text_height
         << ", \"ascent\": " << atlas.header.text_ascent
         << ", \"descent\": " << atlas.header.text_descent
         << ", \"internalLeading\": " << atlas.header.text_internal_leading
         << ", \"externalLeading\": " << atlas.header.text_external_leading
         << ", \"averageWidth\": " << atlas.header.text_average_width
         << ", \"maximumWidth\": " << atlas.header.text_maximum_width << "},\n"
         << "  \"alphaHistogram\": [";
  for (std::size_t i = 0; i < atlas.histogram.size(); ++i) {
    if (i) output << ", ";
    output << atlas.histogram[i];
  }
  output << "]\n}\n";
  if (!output) throw std::runtime_error("could not write manifest output: " + path);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::fprintf(stderr, "usage: %s <atlas.bin> <manifest.json>\n", argv[0]);
    return 2;
  }
  try {
    if (GetACP() != 1252) {
      std::ostringstream message;
      message << "TextOutA reference requires Windows ANSI code page 1252; current ACP is "
              << GetACP();
      throw std::runtime_error(message.str());
    }
    GdiObjects gdi;
    GeneratedAtlas atlas = Generate(gdi);
    if (_stricmp(atlas.realized_face.c_str(), kFontFace) != 0)
      throw std::runtime_error("GDI did not realize the requested Tahoma face");
    VerifyComposition(gdi, atlas);
    const std::vector<std::uint8_t> serialized = Serialize(atlas);
    const auto atlas_hash = Sha256(serialized.data(), serialized.size());
    WriteBinary(argv[1], serialized);
    WriteManifest(argv[2], atlas, atlas_hash, serialized.size());
    std::printf("generated %s (%zu bytes, sha256=%s); self-test passed\n",
                argv[1], serialized.size(),
                Hex(atlas_hash.data(), atlas_hash.size()).c_str());
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "GDI atlas generation failed: %s\n", error.what());
    return 1;
  }
}
