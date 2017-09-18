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

bool Image::Resize(uint32 size, uint32 channels, uint8 fillColor)
{
    if ((size & (size - 1)) != 0)
    {
        std::cout << "Image dimensions must be power of two" << std::endl;
        return false;
    }

    if (channels != 1 && channels != 3)
    {
        std::cout << "Image channels number must be 1 or 3" << std::endl;
        return false;
    }

    mChannels = channels;
    mSize = size;
    mSizeBits = 0;
    {
        uint32 i = mSize;
        while (i >>= 1) ++mSizeBits;
    }
    mSizeMask = (1 << mSizeBits) - 1;

    mData.resize(size * size * channels);
    memset(mData.data(), fillColor, mData.size());
    return true;
}

void Image::Clear(uint8 fillColor)
{
    memset(mData.data(), fillColor, mData.size());
}

Image Image::Downsample() const
{
    assert(mChannels == 1);

    Image result;
    if (result.Resize(mSize / 2, 1))
    {
        for (uint32 y = 0; y < mSize; y += 2)
        {
            for (uint32 x = 0; x < mSize; x += 2)
            {
                result.mData[(y / 2) * (mSize / 2) + (x / 2)] = SampleDomain(x, y);
            }
        }
    }

    return result;
}

Image Image::Upsample() const
{
    assert(mChannels == 1);

    Image result;
    if (result.Resize(mSize * 2, 1))
    {
        for (uint32 y = 0; y < mSize; y++)
        {
            for (uint32 x = 0; x < mSize; x++)
            {
                const uint8 v = Sample(x, y);
                result.WritePixel(2 * x, 2 * y, v);
                result.WritePixel(2 * x, 2 * y + 1, v);
                result.WritePixel(2 * x + 1, 2 * y, v);
                result.WritePixel(2 * x + 1, 2 * y + 1, v);
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

    // read data
    mData.resize(3 * mSize * mSize);
    if (fread(mData.data(), 3 * mSize * mSize, 1, file) != 1)
    {
        fclose(file);
        std::cout << "Failed to read image data" << std::endl;
        return false;
    }
    fclose(file);

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

    std::vector<uint8> tmpData;
    if (mChannels == 3)
    {
        tmpData = mData;
    }
    else
    {
        // extend grayscale to all the RGB channels
        tmpData.resize(3 * mSize * mSize);
        for (uint32 i = 0; i < mSize * mSize; ++i)
        {
            tmpData[3 * i] = mData[i];
            tmpData[3 * i + 1] = mData[i];
            tmpData[3 * i + 2] = mData[i];
        }
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

    uint32 yTotalError = 0;
    uint32 cbTotalError = 0;
    uint32 crTotalError = 0;

    uint32 maxError = 0;
    const int size = imageA.GetSize() * imageA.GetSize();
    const float invSqrSize = 1.0f / (size * size);

    for (int index = 0; index < size; ++index)
    {
        const int32 rA = imageA.mData[3 * index];
        const int32 gA = imageA.mData[3 * index + 1];
        const int32 bA = imageA.mData[3 * index + 2];

        const int32 yA  = CONVERT_RGB2Y(rA, gA, bA);
        const int32 cbA = CONVERT_RGB2Cb(rA, gA, bA);
        const int32 crA = CONVERT_RGB2Cr(rA, gA, bA);

        const int32 rB = imageB.mData[3 * index];
        const int32 gB = imageB.mData[3 * index + 1];
        const int32 bB = imageB.mData[3 * index + 2];

        const int32 yB = CONVERT_RGB2Y(rB, gB, bB);
        const int32 cbB = CONVERT_RGB2Cb(rB, gB, bB);
        const int32 crB = CONVERT_RGB2Cr(rB, gB, bB);

        int32 yError = (int32)yA - (int32)yB;
        int32 cbError = (int32)cbA - (int32)cbB;
        int32 crError = (int32)crA - (int32)crB;

        yError *= yError;
        cbError *= cbError;
        crError *= crError;

        yTotalError += yError;
        cbTotalError += cbError;
        crTotalError += crError;
    }

    const float lumaWeight = 0.8f;
    const float chromaWeight = 0.1f;

    ImageDifference result;
    result.averageError = ((float)yTotalError * lumaWeight + ((float)cbTotalError + (float)crTotalError) * chromaWeight) / (float)size;
    result.psnr = 10.0f * log10f((float)size / result.averageError);
    return result;
}

bool Image::ToYCbCr(Image& y, Image& cb, Image& cr) const
{
    assert(mChannels == 3);

    if (!y.Resize(mSize, 1) || !cb.Resize(mSize, 1) || !cr.Resize(mSize, 1))
    {
        std::cout << "Failed to resize target images" << std::endl;
        return false;
    }

    for (uint32 j = 0; j < mSize; j++)
    {
        for (uint32 i = 0; i < mSize; i++)
        {
            uint8 r, g, b;
            Sample3(i, j, r, g, b);

            y.WritePixel(i, j, CONVERT_RGB2Y(r, g, b));
            cb.WritePixel(i, j, CONVERT_RGB2Cb(r, g, b));
            cr.WritePixel(i, j, CONVERT_RGB2Cr(r, g, b));
        }
    }

    return true;
}

bool Image::FromYCbCr(const Image& y, const Image& cb, const Image& cr)
{
    assert(y.mSize == cb.mSize);
    assert(y.mSize == cr.mSize);
    assert(y.mChannels == 1);
    assert(cb.mChannels == 1);
    assert(cr.mChannels == 1);

    if (!Resize(y.mSize, 3))
    {
        std::cout << "Failed to resize image" << std::endl;
        return false;
    }

    for (uint32 j = 0; j < mSize; j++)
    {
        for (uint32 i = 0; i < mSize; i++)
        {
            const uint8 yComp = y.Sample(i, j);
            const uint8 cbComp = cb.Sample(i, j);
            const uint8 crComp = cr.Sample(i, j);

            const uint8 r = (uint8)CONVERT_YCbCr2R(yComp, cbComp, crComp);
            const uint8 g = (uint8)CONVERT_YCbCr2G(yComp, cbComp, crComp);
            const uint8 b = (uint8)CONVERT_YCbCr2B(yComp, cbComp, crComp);

            WritePixel3(i, j, r, g, b);
        }
    }

    return true;
}