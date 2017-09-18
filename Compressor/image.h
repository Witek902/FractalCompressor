#pragma once

#include "common.h"

#include <vector>
#include <assert.h>

//////////////////////////////////////////////////////////////////////////


#define CLIP(X) ( (X) > 255 ? 255 : (X) < 0 ? 0 : X)

// RGB -> YCbCr
#define CONVERT_RGB2Y(R, G, B)  CLIP((1 * R + 2 * G + 1 * B) >> 2)
#define CONVERT_RGB2Cb(R, G, B) CLIP(((B - G) >> 1) + 128)
#define CONVERT_RGB2Cr(R, G, B) CLIP(((R - G) >> 1) + 128)

// YCbCr -> RGB
#define CONVERT_YCbCr2R(Y, Cb, Cr) CLIP(Y + ((3 * (Cr - 128) - (Cb - 128)) >> 1))
#define CONVERT_YCbCr2G(Y, Cb, Cr) CLIP(Y - ((1 * (Cr - 128) + (Cb - 128)) >> 1))
#define CONVERT_YCbCr2B(Y, Cb, Cr) CLIP(Y + ((3 * (Cb - 128) - (Cr - 128)) >> 1))

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
    bool Resize(uint32 size, uint32 channels, uint8 fillColor = 0);

    // fill with color
    void Clear(uint8 fillColor);

    // create 2x downsampled image
    Image Downsample() const;

    // create 2x upsampled image
    Image Upsample() const;

    // load image from a BMP file
    bool Load(const char* path);

    // save image to a BMP file
    bool Save(const std::string& name) const;

    // decompose RBG image to YCbCr image
    bool ToYCbCr(Image& y, Image& cb, Image& cr) const;

    // convert separate YCbCr images into one RGB image
    bool FromYCbCr(const Image& y, const Image& cb, const Image& cr);

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

    // access image pixel
    FORCE_INLINE uint8& At(uint32 x, uint32 y)
    {
        assert(mChannels == 1);
        assert(x < mSize);
        assert(y < mSize);

        return mData[y * mSize + x];
    }

    // get single pixel (monochromatic)
    FORCE_INLINE uint8 Sample(uint32 x, uint32 y) const
    {
        assert(mChannels == 1);
        assert(x < mSize);
        assert(y < mSize);

        return mData[y * mSize + x];
    }

    // get single pixel (RGB)
    FORCE_INLINE void Sample3(uint32 x, uint32 y, uint8& r, uint8& g, uint8& b) const
    {
        assert(mChannels == 3);
        assert(x < mSize);
        assert(y < mSize);

        const uint8* data = mData.data() + 3 * (y * mSize + x);
        r = data[0];
        g = data[1];
        b = data[2];
    }

    // get single pixel (wrap around)
    FORCE_INLINE uint8 SampleWrapped(uint32 x, uint32 y) const
    {
        assert(mChannels == 1);

        x &= mSizeMask;
        y &= mSizeMask;
        return mData[y * mSize + x];
    }

    // write single pixel (monochromatic)
    FORCE_INLINE void WritePixel(uint32 x, uint32 y, uint8 value)
    {
        assert(mChannels == 1);
        assert(x < mSize);
        assert(y < mSize);

        mData[y * mSize + x] = value;
    }

    // write single pixel (RGB)
    FORCE_INLINE void WritePixel3(uint32 x, uint32 y, uint8 r, uint8 g, uint8 b)
    {
        assert(mChannels == 3);
        assert(x < mSize);
        assert(y < mSize);

        uint8* data = mData.data() + 3 * (y * mSize + x);
        data[0] = r;
        data[1] = g;
        data[2] = b;
    }

    // write single pixel (RGB)
    FORCE_INLINE void WritePixel3(uint32 x, uint32 y, uint32 color)
    {
        assert(mChannels == 3);
        assert(x < mSize);
        assert(y < mSize);

        uint8* data = mData.data() + 3 * (y * mSize + x);
        data[0] = (uint8)(color);
        data[1] = (uint8)(color >> 8);
        data[2] = (uint8)(color >> 16);
    }

    // get and downsampled 2x2 region of pixels
    FORCE_INLINE uint8 SampleDomain(uint32 x, uint32 y) const
    {
        assert(mChannels == 1);

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
