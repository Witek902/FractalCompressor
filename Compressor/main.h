#pragma once

#include "common.h"
#include "domain.h"

#include <Windows.h>
#include <vector>
#include <string>
#include <immintrin.h>


//////////////////////////////////////////////////////////////////////////

struct Difference
{
    float averageError;
    float maxError;
    float psnr;
};

struct Color
{
    uint8 b;
    uint8 g;
    uint8 r;

    bool IsZero() const
    {
        return r == 0 && g == 0 && b == 0;
    }

    void Set(uint32 color)
    {
        r = color;
        g = color >> 8;
        b = color >> 16;
    }
};

/*
struct ColorYUV
{
    uint8 y;
    uint8 u : 4;
    uint8 v : 4;
};
*/




#pragma pack(push, 2)
struct BitmapFileHeader
{
    BITMAPFILEHEADER fileHeader;
    BITMAPINFOHEADER infoHeader;
};
#pragma pack(pop)

// TODO Color image data vs grayscale image data
using ImageData = std::vector<uint8>;


class Compressor
{
public:
    bool LoadSourceImage(const char* path);
    bool SaveImage(const std::string& name, const ImageData& image) const;
    bool SaveEncoded(const std::string& name) const;

    // compare two images
    static Difference Compare(const ImageData& imageA, const ImageData& imageB);

    void SelfSimilaritySearch();
    float DomainSearch(uint32 rx, uint32 ry, Domain& outDomain) const;
    DomainsStats CalculateDomainStats() const;
    float MatchDomain(uint32 rx0, uint32 ry0, uint32 dx0, uint32 dy0, uint8 transform, float& outScale, float& outOffset) const;

    void Decompress(ImageData& outImage) const;

    uint8 SampleImage(uint32 x, uint32 y) const
    {
        return mReferenceImage[y * mSize + x];
    }

    const ImageData& GetReferenceImage() const
    {
        return mReferenceImage;
    }

private:
    uint32 mSize;
    uint32 mSizeBits;
    uint32 mSizeMask;

    Color mAverageColor;
    ImageData mReferenceImage;

    std::vector<Domain> mDomains;
};
