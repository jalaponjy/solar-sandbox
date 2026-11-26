#include "png.h"
#include <cstdio>
#include <algorithm>
#include <vector>

namespace {
uint32_t crcTable[256];
bool crcInit = false;
uint32_t crc(const uint8_t* d, size_t n, uint32_t c = 0xffffffffu) {
  if (!crcInit) {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t x = i;
      for (int k = 0; k < 8; ++k) x = (x & 1) ? 0xedb88320u ^ (x >> 1) : x >> 1;
      crcTable[i] = x;
    }
    crcInit = true;
  }
  for (size_t i = 0; i < n; ++i) c = crcTable[(c ^ d[i]) & 0xff] ^ (c >> 8);
  return c;
}
void be32(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x);
}
void chunk(FILE* f, const char* type, const std::vector<uint8_t>& data) {
  std::vector<uint8_t> out;
  be32(out, (uint32_t)data.size());
  std::vector<uint8_t> body(type, type + 4);
  body.insert(body.end(), data.begin(), data.end());
  out.insert(out.end(), body.begin(), body.end());
  be32(out, crc(body.data(), body.size()) ^ 0xffffffffu);
  fwrite(out.data(), 1, out.size(), f);
}
}  // namespace

bool savePNG(const char* path, const uint8_t* rgba, int w, int h) {
  FILE* f = fopen(path, "wb");
  if (!f) return false;
  const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  fwrite(sig, 1, 8, f);
  std::vector<uint8_t> ihdr;
  be32(ihdr, w); be32(ihdr, h);
  ihdr.push_back(8); ihdr.push_back(6); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
  chunk(f, "IHDR", ihdr);

  // raw scanlines with filter byte 0, wrapped in stored (uncompressed) deflate blocks
  std::vector<uint8_t> raw;
  raw.reserve((size_t)h * (w * 4 + 1));
  for (int y = 0; y < h; ++y) {
    raw.push_back(0);
    raw.insert(raw.end(), rgba + (size_t)y * w * 4, rgba + (size_t)(y + 1) * w * 4);
  }
  std::vector<uint8_t> z{0x78, 0x01};
  size_t pos = 0;
  while (pos < raw.size()) {
    const size_t n = std::min<size_t>(65535, raw.size() - pos);
    z.push_back(pos + n >= raw.size() ? 1 : 0);
    z.push_back(n & 0xff); z.push_back(n >> 8);
    z.push_back(~n & 0xff); z.push_back((~n >> 8) & 0xff);
    z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
    pos += n;
  }
  uint32_t a = 1, b = 0;
  for (uint8_t c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
  be32(z, (b << 16) | a);
  chunk(f, "IDAT", z);
  chunk(f, "IEND", {});
  fclose(f);
  return true;
}
