// Minimal PNG writer so the smoke test has no image library dependency.
//
// Uses stored (uncompressed) deflate blocks: larger files than a real encoder,
// but the format is entirely defined by the PNG/zlib specs, so this stays a
// hundred lines with no zlib and no Qt.

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace modern
{

inline std::uint32_t crc32Of(const unsigned char* data, std::size_t length)
{
    static std::uint32_t table[256];
    static bool initialised = false;
    if (!initialised)
    {
        for (std::uint32_t i = 0; i < 256; ++i)
        {
            std::uint32_t value = i;
            for (int bit = 0; bit < 8; ++bit)
                value = (value & 1u) ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
            table[i] = value;
        }
        initialised = true;
    }

    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < length; ++i)
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

inline std::uint32_t adler32Of(const unsigned char* data, std::size_t length)
{
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (std::size_t i = 0; i < length; ++i)
    {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

inline void appendBigEndian(std::vector<unsigned char>& target, std::uint32_t value)
{
    target.push_back(static_cast<unsigned char>((value >> 24) & 0xFF));
    target.push_back(static_cast<unsigned char>((value >> 16) & 0xFF));
    target.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
    target.push_back(static_cast<unsigned char>(value & 0xFF));
}

inline void appendChunk(std::vector<unsigned char>& png, const char type[4],
                        const std::vector<unsigned char>& payload)
{
    appendBigEndian(png, std::uint32_t(payload.size()));

    std::vector<unsigned char> typed(type, type + 4);
    typed.insert(typed.end(), payload.begin(), payload.end());

    png.insert(png.end(), typed.begin(), typed.end());
    appendBigEndian(png, crc32Of(typed.data(), typed.size()));
}

// `rgb` is top-down, 3 bytes per pixel.
inline bool writePng(const char* path, const unsigned char* rgb, int width, int height)
{
    if (path == nullptr || rgb == nullptr || width <= 0 || height <= 0)
        return false;

    // Raw zlib payload: one filter byte (0 = none) followed by the scanline.
    std::vector<unsigned char> raw;
    raw.reserve(std::size_t(height) * (1 + std::size_t(width) * 3));
    for (int y = 0; y < height; ++y)
    {
        raw.push_back(0);
        const unsigned char* row = rgb + std::size_t(y) * std::size_t(width) * 3;
        raw.insert(raw.end(), row, row + std::size_t(width) * 3);
    }

    std::vector<unsigned char> zlib;
    zlib.push_back(0x78); // deflate, 32k window, default compression
    zlib.push_back(0x01);

    std::size_t offset = 0;
    while (offset < raw.size())
    {
        const std::size_t blockLength = std::min<std::size_t>(65535, raw.size() - offset);
        const bool last = (offset + blockLength) >= raw.size();
        zlib.push_back(last ? 1 : 0);
        zlib.push_back(static_cast<unsigned char>(blockLength & 0xFF));
        zlib.push_back(static_cast<unsigned char>((blockLength >> 8) & 0xFF));
        zlib.push_back(static_cast<unsigned char>((~blockLength) & 0xFF));
        zlib.push_back(static_cast<unsigned char>(((~blockLength) >> 8) & 0xFF));
        zlib.insert(zlib.end(), raw.begin() + std::ptrdiff_t(offset),
                    raw.begin() + std::ptrdiff_t(offset + blockLength));
        offset += blockLength;
    }
    appendBigEndian(zlib, adler32Of(raw.data(), raw.size()));

    std::vector<unsigned char> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

    std::vector<unsigned char> header;
    appendBigEndian(header, std::uint32_t(width));
    appendBigEndian(header, std::uint32_t(height));
    header.push_back(8); // bit depth
    header.push_back(2); // colour type: truecolour
    header.push_back(0); // compression
    header.push_back(0); // filter
    header.push_back(0); // interlace
    appendChunk(png, "IHDR", header);

    appendChunk(png, "IDAT", zlib);
    appendChunk(png, "IEND", {});

    std::FILE* file = std::fopen(path, "wb");
    if (file == nullptr)
        return false;
    const std::size_t written = std::fwrite(png.data(), 1, png.size(), file);
    std::fclose(file);
    return written == png.size();
}

} // namespace modern
