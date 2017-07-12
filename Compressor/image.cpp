#include "image.h"

#include <Windows.h>
#include <iostream>
#include <string>
#include <algorithm>
#include <assert.h>


#pragma pack(push, 2)
struct BitmapFileHeader
{
    BITMAPFILEHEADER fileHeader;
    BITMAPINFOHEADER infoHeader;
};
#pragma pack(pop)

//////////////////////////////////////////////////////////////////////////

bool Image::Resize(uint32 size)
{
    if ((size & (size - 1)) != 0)
    {
        std::cout << "Image dimensions must be power of two" << std::endl;
        return false;
    }

    mSize = size;
    mSizeBits = 0;
    {
        uint32 i = mSize;
        while (i >>= 1) ++mSizeBits;
    }
    mSizeMask = (1 << mSizeBits) - 1;

    mData.resize(size * size);
    memset(mData.data(), 0, mData.size());
    return true;
}

Image Image::Downsample() const
{
    Image result;
    if (result.Resize(mSize / 2))
    {
        for (uint32 y = 0; y < mSize; y += 2)
        {
            for (uint32 x = 0; x < mSize; x += 2)
            {
                result.mData[(y / 2) * (mSize / 2) + (x / 2)] = SampleDomain(x, y);

                //uint32 color = ((uint32)Sample(x, y) + (uint32)Sample(x + 1, y) + (uint32)Sample(x, y + 1) + (uint32)Sample(x + 1, y + 1)) / 4;
                //result.mData[(y / 2) * (mSize / 2) + (x / 2)] = (uint8)color;
            }
        }
    }

    return result;
}

bool Image::Load(const char* path)
{
    FILE* file = fopen(path, "rb");
    if (!file)
    {
        std::cout << "Failed to load source image: " << stderr << std::endl;
        return false;
    }

    BITMAPFILEHEADER fileHeader;
    if (fread(&fileHeader, sizeof(BITMAPFILEHEADER), 1, file) != 1)
    {
        fclose(file);
        std::cout << "Failed to read BMP file header: " << stderr << std::endl;
        return false;
    }

    BITMAPINFOHEADER infoHeader;
    if (fread(&infoHeader, sizeof(BITMAPINFOHEADER), 1, file) != 1)
    {
        fclose(file);
        std::cout << "Failed to read BMP info header: " << stderr << std::endl;
        return false;
    }

    if (infoHeader.biPlanes != 1 || infoHeader.biCompression != BI_RGB || infoHeader.biBitCount != 24)
    {
        std::cout << "Unsupported file format" << std::endl;
        return false;
    }

    if (infoHeader.biWidth != infoHeader.biHeight)
    {
        std::cout << "Image width and height must be the same" << std::endl;
        return false;
    }

    mSize = infoHeader.biWidth;
    if ((mSize & (mSize - 1)) != 0)
    {
        std::cout << "Image dimensions must be power of two" << std::endl;
        return false;
    }

    // create image size bit mask
    mSizeBits = 0;
    {
        uint32 i = mSize;
        while (i >>= 1) ++mSizeBits;
    }
    mSizeMask = (1 << mSizeBits) - 1;

    // convert to grayscale
    std::vector<uint8> sourceData;
    sourceData.resize(3 * mSize * mSize);
    if (fread(sourceData.data(), 3 * mSize * mSize, 1, file) != 1)
    {
        fclose(file);
        std::cout << "Failed to read image data" << std::endl;
        return false;
    }
    fclose(file);

    mData.resize(mSize * mSize);
    for (uint32 i = 0; i < mSize * mSize; ++i)
    {
        mData[i] = sourceData[3 * i];
    }

    mChannels = 3;

    std::cout << "Image loaded: size=" << mSize << std::endl;
    return true;
}

bool Image::Save(const std::string& name) const
{
    uint32 dataSize = 3 * mSize * mSize;

    const BitmapFileHeader header =
    {
        // BITMAPFILEHEADER
        {
            /* bfType */        0x4D42,
            /* bfSize */        sizeof(BitmapFileHeader) + dataSize,
            /* bfReserved1 */   0,
            /* bfReserved2 */   0,
            /* bfOffBits */     sizeof(BitmapFileHeader),
        },

        // BITMAPINFOHEADER
        {
            sizeof(BITMAPINFOHEADER),
            (LONG)mSize,
            (LONG)mSize,
            1,
            24,
            BI_RGB,
            dataSize,
            96, 96, 0, 0
        },
    };

    FILE* file = fopen(name.c_str(), "wb");
    if (!file)
    {
        std::cout << "Failed to open target image '" << name << "': " << stderr << std::endl;
        return false;
    }

    if (fwrite(&header, sizeof(BitmapFileHeader), 1, file) != 1)
    {
        std::cout << "Failed to write bitmap header: " << stderr << std::endl;
        fclose(file);
        return false;
    }

    // convert to grayscale
    std::vector<uint8> tmpData;
    tmpData.resize(3 * mSize * mSize);
    for (uint32 i = 0; i < mSize * mSize; ++i)
    {
        tmpData[3 * i] = mData[i];
        tmpData[3 * i + 1] = mData[i];
        tmpData[3 * i + 2] = mData[i];

    }

    if (fwrite(tmpData.data(), dataSize, 1, file) != 1)
    {
        std::cout << "Failed to write bitmap image data: " << stderr << std::endl;
        fclose(file);
        return false;
    }

    fclose(file);
    return true;
}

ImageDifference Image::Compare(const Image& imageA, const Image& imageB)
{
    assert(imageA.GetSize() == imageB.GetSize());
    assert(imageA.GetChannelsNum() == imageB.GetChannelsNum());

    uint32 totalError = 0;
    uint32 maxError = 0;

    for (size_t i = 0; i < imageA.GetSize(); ++i)
    {
        int32 error = (int32)imageA.mData[i] - (int32)imageB.mData[i];
        error *= error;

        maxError = std::max<uint32>(maxError, error);
        totalError += error;
    }

    ImageDifference result;
    result.averageError = (float)totalError / (float)(imageA.GetSize());
    result.maxError = (float)maxError / 255.0f;
    result.psnr = 10.0f * log10f(255.0f * 255.0f / result.averageError);
    return result;
}

bool Image::ToYUV(Image& y, Image& u, Image& v) const
{
    // TODO
    return false;
}

bool Image::FromYUV(const Image& y, const Image& u, const Image& v)
{
    // TODO
    return false;
}