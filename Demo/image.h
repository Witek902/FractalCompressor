#pragma once

#include "demo.h"


// Grayscale or color image
class Image
{
public:
    FORCE_INLINE uint32 GetSize() const
    {
        return mSize;
    }

    FORCE_INLINE uint32 GetSizeMask() const
    {
        return mSizeMask;
    }

    // get single pixel (monochromatic)
    FORCE_INLINE uint8 Sample(uint32 x, uint32 y) const
    {
        return mData[y * mSize + x];
    }

    // write single pixel (monochromatic)
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

    uint8* mData;
    uint32 mSize;
    uint32 mSizeMask;
};
