#pragma once

#include "common.h"

#include <vector>
#include <assert.h>


class QuadtreeCode
{
public:
    using ElementType = uint32;

    QuadtreeCode()
        : mBitsUsed(0)
        , mCurrentBit(0)
    {}

    const std::vector<ElementType>& GetCode() const
    {
        return mCode;
    }

    uint32 GetSize() const
    {
        return mBitsUsed;
    }

    uint32 GetNumElements() const
    {
        return (uint32)mCode.size();
    }

    void Clear()
    {
        mCode.clear();
        mCurrentBit = 0;
        mBitsUsed = 0;
    }

    void Push(bool bit)
    {
        if (mBitsUsed + 1 > mCode.size() * 32)
        {
            mCode.push_back(0);
        }

        const uint32 wordIndex = mBitsUsed / 32;
        const uint32 bitIndex = mBitsUsed % 32;

        // write single bit to the code
        if (bit)
            mCode[wordIndex] |= (ElementType)1 << (ElementType)bitIndex;

        mBitsUsed++;
    }

    void ResetCursor()
    {
        mCurrentBit = 0;
    }

    bool Get()
    {
        assert(mCurrentBit < mBitsUsed);

        const uint32 wordIndex = mCurrentBit / 32;
        const uint32 bitIndex = mCurrentBit % 32;

        const ElementType val = mCode[wordIndex] & ((ElementType)1 << (ElementType)bitIndex);
        mCurrentBit++;
        return val != 0;
    }

    void Push(QuadtreeCode& other)
    {
        other.ResetCursor();
        for (uint32 i = 0; i < other.GetSize(); ++i)
        {
            Push(other.Get());
        }
    }

    void Load(const std::vector<ElementType>& code, uint32 numBits)
    {
        assert(code.size() * sizeof(ElementType) * 8 >= numBits);
        mCode = code;
        mBitsUsed = numBits;
        mCurrentBit = 0;
    }

private:
    std::vector<ElementType> mCode;
    uint32 mBitsUsed;
    uint32 mCurrentBit; // cursor position
};
