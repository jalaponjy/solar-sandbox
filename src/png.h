#pragma once
#include <cstdint>
// Writes an RGBA8 image (rows top-to-bottom) as an uncompressed PNG. Returns false on I/O error.
bool savePNG(const char* path, const uint8_t* rgba, int w, int h);
