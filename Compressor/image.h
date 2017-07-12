#pragma once

#include "common.h"

#include <vector>

//////////////////////////////////////////////////////////////////////////

struct ImageDifference
{
    float averageError;
    float maxError;
    float psnr;
};

/*
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
*/

// Grayscale or color image
class Image
{
public:
    Image()
        : mSize(0)
        , mSizeBits(0)
        , mSizeMask(0)
    { }

    Image(const Image&) = default;
    Image(Image&&) = default;
    Image& operator = (const Image&) = default;
    Image& operator = (Image&&) = default;

    // resize and init with zeros
    bool Resize(uint32 size);

    // create 2x downsampled image
    Image Downsample() const;

    // load image from a BMP file
    bool Load(const char* path);

    // save image to a BMP file
    bool Save(const std::string& name) const;

    // decompose RBG image to YUV image
    bool ToYUV(Image& y, Image& u, Image& v) const;

    // convert separate YUV images into one RGB image
    bool FromYUV(const Image& y, const Image& u, const Image& v);

    // compare two images
    static ImageDifference Compare(const Image& imageA, const Image& imageB);

    uint32 GetSize() const
    {
        return mSize;
    }

    uint32 GetSizeBits() const
    {
        return mSizeBits;
    }

    uint32 GetSizeMask() const
    {
        return mSizeMask;
    }

    uint32 GetChannelsNum() const
    {
        return mChannels;
    }

    // get single pixel
    FORCE_INLINE uint8 Sample(uint32 x, uint32 y) const
    {
        return mData[y * mSize + x];
    }

    // get single pixel (wrap around)
    FORCE_INLINE uint8 SampleWrapped(uint32 x, uint32 y) const
    {
        x &= mSizeMask;
        y &= mSizeMask;
        return mData[y * mSize + x];
    }

    // get single pixel
    FORCE_INLINE void WritePixel(uint32 x, uint32 y, uint8 value)
    {
        mData[y * mSize + x] = value;
    }

    // get and downsampled 2x2 region of pixels
    FORCE_INLINE uint8 SampleDomain(uint32 x, uint32 y) const
    {
        const uint32 xa = x & mSizeMask;
        const uint32 xb = (x + 1) & mSizeMask;
        const uint32 ya = y & mSizeMask;
        const uint32 yb = (y + 1) & mSizeMask;

        const uint32 result =
            (uint32)mData[ya * mSize + xa] +
            (uint32)mData[ya * mSize + xb] +
            (uint32)mData[yb * mSize + xa] +
            (uint32)mData[yb * mSize + xb] + 1; // +1 for better rounding

        return (uint8)(result / 4);
    }

private:
    std::vector<uint8> mData;
    uint32 mChannels;
    uint32 mSize;
    uint32 mSizeBits;
    uint32 mSizeMask;
};
