#pragma once

#include "demo.h"


class QuadtreeCode
{
public:
    using ElementType = uint32;

    FORCE_INLINE QuadtreeCode(const ElementType* code)
        : mBitsUsed(0)
        , mCurrentBit(0)
        , mCode(code)
    {}

    FORCE_INLINE void ResetCursor()
    {
        mCurrentBit = 0;
    }

    FORCE_INLINE bool Get()
    {
        const uint32 wordIndex = mCurrentBit / 32;
        const uint32 bitIndex = mCurrentBit % 32;

        const ElementType val = mCode[wordIndex] & ((ElementType)1 << (ElementType)bitIndex);
        mCurrentBit++;
        return val != 0;
    }

private:
    const ElementType* mCode;
    uint32 mBitsUsed;
    uint32 mCurrentBit; // cursor position
};
