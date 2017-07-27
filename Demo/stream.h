#pragma once

#include "demo.h"


class Stream
{
public:
    FORCE_INLINE Stream(const uint32* code)
        : mCurrentBit(0)
        , mCode(code)
    {}

    FORCE_INLINE void ResetCursor()
    {
        mCurrentBit = 0;
    }

    FORCE_INLINE bool GetBit()
    {
        const uint32 wordIndex = mCurrentBit / 32;
        const uint32 bitIndex = mCurrentBit % 32;

        const uint32 val = mCode[wordIndex] & ((uint32)1 << (uint32)bitIndex);
        mCurrentBit++;
        return val != 0;
    }

    FORCE_INLINE uint32 Get(uint32 bits)
    {
        uint32 value = 0;
        for (uint32 i = 0; i < bits; i++)
        {
            value |= GetBit() << i;
        }
        return value;
    }

    int32 GetInteger()
    {
        const bool isNonZero = GetBit();
        if (!isNonZero)
            return 0;

        int32 len = 1;
        uint32 bits = 0;
        while (GetBit())
        {
            len *= 2;
            bits++;
        }

        int32 value = len + Get(bits);

        const bool isPositive = GetBit();
        if (!isPositive)
            value = -value;

        return value;
    }

private:
    const uint32* mCode;
    uint32 mCurrentBit; // cursor position
};
