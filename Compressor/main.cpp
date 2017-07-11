#include "main.h"
#include <iostream>
#include <assert.h>
#include <algorithm>
#include <iomanip>

//////////////////////////////////////////////////////////////////////////

const uint32 RANGE_BLOCK_SIZE = 8;

//////////////////////////////////////////////////////////////////////////

namespace {

uint32 gSeed = 0x582c6298;

template<typename T>
T Diff(T a, T b)
{
    if (a > b)
        return a - b;

    return b - a;
}

uint32 RandomInt()
{
    uint32 x = gSeed;
    const uint32 result = x;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    gSeed = x;
    return result;
}

void TransformLocation(uint8 x, uint8 y, uint8 transform, uint8& outX, uint8& outY)
{
    const uint8 offset = RANGE_BLOCK_SIZE - 1;

    if (transform & 0x1)
        x = offset - x;

    switch (transform >> 1)
    {
    case 0:
        outX = x;
        outY = y;
        break;
    case 1:
        outX = offset - y;
        outY = x;
        break;
    case 2:
        outX = offset - x;
        outY = offset - y;
        break;
    case 3:
        outX = y;
        outY = offset - x;
        break;
    }
}

} // namespace

//////////////////////////////////////////////////////////////////////////

bool Compressor::LoadSourceImage(const char* path)
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

    mReferenceImage.resize(mSize * mSize);
    for (uint32 i = 0; i < mSize * mSize; ++i)
    {
        mReferenceImage[i] = sourceData[3 * i];
    }

    // sanity check
    Difference selfDiff = Compare(mReferenceImage, mReferenceImage);
    assert(selfDiff.averageError == 0.0f);
    assert(selfDiff.maxError == 0.0f);

    std::cout << "Image loaded: size=" << mSize << std::endl;

    return true;
}

bool Compressor::SaveImage(const std::string& name, const ImageData& image) const
{
    assert(image.size() == mSize * mSize);

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
        tmpData[3 * i] = image[i];
        tmpData[3 * i + 1] = image[i];
        tmpData[3 * i + 2] = image[i];

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

Difference Compressor::Compare(const ImageData& imageA, const ImageData& imageB)
{
    assert(imageA.size() == imageB.size());

    uint32 totalError = 0;
    uint32 maxError = 0;
    
    for (size_t i = 0; i < imageA.size(); ++i)
    {
        uint32 error = Diff(imageA[i], imageB[i]);
        error *= error;

        maxError = std::max<uint32>(maxError, error);
        totalError += error;
    }

    const float scale = 255.0f * 255.0f;

    Difference result;
    result.averageError = (float)totalError / (float)(imageA.size()) / scale;
    result.maxError = (float)maxError / 255.0f;
    result.psnr = 10.0f * log10f(scale / result.averageError);
    return result;
}

float Compressor::MatchDomain(uint32 rx0, uint32 ry0, uint32 dx0, uint32 dy0, uint8 transform, float& outScale, float& outOffset) const
{
    const uint32 k = RANGE_BLOCK_SIZE * RANGE_BLOCK_SIZE;
    const float invK = 1.0f / (float)k;

    uint32 gh = 0;
    uint32 gSum = 0;
    uint32 gSqrSum = 0;
    uint32 hSum = 0;

    uint8 rangeData[k];
    uint8 domainData[k];

    uint32 index = 0;
    for (uint32 y = 0; y < RANGE_BLOCK_SIZE; y++)
    {
        const uint32 ry = ry0 + y;

        for (uint32 x = 0; x < RANGE_BLOCK_SIZE; x++)
        {
            uint8 tx, ty;
            TransformLocation(x, y, transform, tx, ty);

            const uint32 dyA = mSizeMask & (dy0 + 2 * ty);
            const uint32 dyB = mSizeMask & (dy0 + 2 * ty + 1);
            const uint32 dxA = mSizeMask & (dx0 + 2 * tx);
            const uint32 dxB = mSizeMask & (dx0 + 2 * tx + 1);

            // scaled down domain pixel
            const uint32 g =
                ((uint32)SampleImage(dxA, dyA) + (uint32)SampleImage(dxA, dyB) +
                (uint32)SampleImage(dxB, dyA) + (uint32)SampleImage(dxB, dyB) + 1) / 4; // TODO remove division to improve accuracy

            // range block pixel
            const uint32 h = SampleImage(rx0 + x, ry);

            gh += g * h;
            gSum += g;
            gSqrSum += g * g;
            hSum += h;

            domainData[index] = g;
            rangeData[index] = h;
            index++;
        }
    }

    float term0 = (float)k * (float)gh - (float)gSum * (float)hSum;
    float term1 = (float)k * (float)gSqrSum - (float)gSum * (float)gSum;
    
    if (abs(term1) < 0.0001f)
    {
        outScale = 0.0f;
        outOffset = (float)hSum * invK;
    }
    else
    {
        outScale = term0 / term1;
        outOffset = ((float)hSum - outScale * (float)gSum) * invK;
    }

    Domain d;
    d.SetScale(outScale);
    d.SetOffset(outOffset);

    // calculate MSE (including color scaling and offset)
    float diffSum = 0.0f;
    for (uint32 i = 0; i < k; ++i)
    {
        float g = (float)d.TransformColor(domainData[i]); //outScale * domainData[i] + outOffset;
        float h = (float)rangeData[i];
        float diff = g - h;
        diffSum += diff * diff;
    }
    return diffSum * invK / (255.0f * 255.0f);
}

float Compressor::DomainSearch(uint32 rx, uint32 ry, Domain& outDomain) const
{
    Domain bestDomain;
    float bestCost = FLT_MAX;

    const uint32 DOMAIN_MAX_LOCATIONS = (1 << DOMAIN_LOCATION_BITS);
    const uint32 DOMAIN_LOC_STEP = 1 << (mSizeBits - DOMAIN_LOCATION_BITS);

    for (uint32 dy = 0; dy < DOMAIN_MAX_LOCATIONS; dy++)
    {
        for (uint32 dx = 0; dx < DOMAIN_MAX_LOCATIONS; dx++)
        {
            for (uint8 t = 0; t < 8; ++t)
            {
                float scale, offset;
                const float currentCost = MatchDomain(rx, ry, dx * DOMAIN_LOC_STEP, dy * DOMAIN_LOC_STEP, t, scale, offset);

                if (currentCost < bestCost)
                {
                    // scale domain location so it covers tightly whole picture
                    bestDomain.x = dx;
                    bestDomain.y = dy;
                    bestDomain.transform = t;
                    bestDomain.SetOffset(offset);
                    bestDomain.SetScale(scale);

                    bestCost = currentCost;
                }
            }
        }
    }

    outDomain = bestDomain;
    return bestCost;
}

void Compressor::SelfSimilaritySearch()
{
    mDomains.clear();
    uint32 rangeId = 0;
    for (uint32 y = 0; y < mSize; y += RANGE_BLOCK_SIZE)   
    {
        for (uint32 x = 0; x < mSize; x += RANGE_BLOCK_SIZE)
        {
            std::cout << "Range (" << std::setw(3) << x << "," << std::setw(3) << y << ") -> ";

            Domain domain;
            float mse = DomainSearch(x, y, domain);
            float psnr = 10.0f * logf(1.0f / mse);

            mDomains.push_back(domain);

            std::cout << "Domain: loc=(" << std::setw(3) << (uint32)domain.x << "," << std::setw(3) << (uint32)domain.y << "), ";
            std::cout << "t=" << (uint32)domain.transform;
            std::cout << ", scale=" << std::setw(8) << std::setprecision(3) << domain.GetScale();
            std::cout << ", offset=" << std::setw(8) << std::setprecision(3) << domain.GetOffset() << ", ";
            std::cout << "MSE=" << std::setw(8) << std::setprecision(3) << mse << " ("
                << std::setw(8) << std::setprecision(3) << psnr << " dB)" << std::endl;

            rangeId++;
        }
    }

    {
        DomainsStats domainStats = CalculateDomainStats();
        std::cout << std::endl << "=== DOMAINS STATS ===" << std::endl;
        std::cout << "Average offset:   " << domainStats.averageOffset << std::endl;
        std::cout << "Offset variance:  " << domainStats.offsetVariance << std::endl;
        std::cout << "Min. offset:      " << domainStats.minOffset << std::endl;
        std::cout << "Max. offset:      " << domainStats.maxOffset << std::endl;
        std::cout << "Average scale:    " << domainStats.averageScale << std::endl;
        std::cout << "Scale variance:   " << domainStats.scaleVariance << std::endl;
        std::cout << "Min. scale:       " << domainStats.minScale << std::endl;
        std::cout << "Max. scale:       " << domainStats.maxScale << std::endl;

        std::cout << "Transform distr.: ";
        for (int i = 0; i < 8; ++i)
            std::cout << i << "(" << domainStats.transformDistribution[i] << ") ";
        std::cout << std::endl;
    }
}

DomainsStats Compressor::CalculateDomainStats() const
{
    const float invNumOfDomains = 1.0f / (float)mDomains.size();
    DomainsStats stats;

    for (const Domain& d : mDomains)
    {
        const float offset = (float)d.GetOffset();
        const float scale = (float)d.GetScale();

        stats.averageOffset += offset;
        stats.averageScale += scale;
        stats.minOffset = std::min<float>(stats.minOffset, offset);
        stats.maxOffset = std::max<float>(stats.maxOffset, offset);
        stats.minScale = std::min<float>(stats.minScale, scale);
        stats.maxScale = std::max<float>(stats.maxScale, scale);

        assert(d.transform < 8);
        stats.transformDistribution[d.transform]++;
    }
    stats.averageOffset *= invNumOfDomains;
    stats.averageScale *= invNumOfDomains;

    for (const Domain& d : mDomains)
    {
        const float offset = (float)d.GetOffset();
        const float scale = (float)d.GetScale();

        stats.offsetVariance += (stats.averageOffset - offset) * (stats.averageOffset - offset);
        stats.scaleVariance += (stats.averageScale - scale) * (stats.averageScale - scale);
    }
    stats.offsetVariance *= invNumOfDomains;
    stats.scaleVariance *= invNumOfDomains;

    return stats;
}

void Compressor::Decompress(ImageData& outImage) const
{
    const uint32 MAX_ITERATIONS = 8;

    uint32 currentImage = 0;
    ImageData tempImages[2];

    tempImages[0].resize(mReferenceImage.size());
    tempImages[1].resize(mReferenceImage.size());

    memset(tempImages[0].data(), 0x80, tempImages[0].size());

    for (uint32 i = 0; i < MAX_ITERATIONS; ++i)
    {
        // swap images
        currentImage ^= 1;
        ImageData& src = tempImages[currentImage ^ 1];
        ImageData& dest = tempImages[currentImage];

        uint32 domainIndex = 0;
        for (uint32 ry0 = 0; ry0 < mSize; ry0 += RANGE_BLOCK_SIZE)
        {
            for (uint32 rx0 = 0; rx0 < mSize; rx0 += RANGE_BLOCK_SIZE)
            {
                const Domain& domain = mDomains[domainIndex++];

                // decode location (to picture space)
                const uint32 dx = domain.x << (mSizeBits - DOMAIN_LOCATION_BITS);
                const uint32 dy = domain.y << (mSizeBits - DOMAIN_LOCATION_BITS);

                for (uint32 y = 0; y < RANGE_BLOCK_SIZE; y++)
                {
                    const uint32 ry = ry0 + y;

                    for (uint32 x = 0; x < RANGE_BLOCK_SIZE; x++)
                    {
                        uint8 tx, ty;
                        TransformLocation(x, y, domain.transform, tx, ty);

                        const uint32 dxA = mSizeMask & (dx + 2 * tx);
                        const uint32 dxB = mSizeMask & (dx + 2 * tx + 1);
                        const uint32 dyA = mSizeMask & (dy + 2 * ty);
                        const uint32 dyB = mSizeMask & (dy + 2 * ty + 1);

                        // scaled down domain pixel
                        const int32 d =
                            ((int32)src[dxA + mSize * dyA] + (int32)src[dxA + mSize * dyB] +
                            (int32)src[dxB + mSize * dyA] + (int32)src[dxB + mSize * dyB] + 1) / 4;

                        // transform color
                        //int32 h = g * scale + offset;
                        //h = std::min<int32>(255, std::max<int32>(0, h));
                        dest[mSize * ry + rx0 + x] = domain.TransformColor((uint8)d);
                    }
                }
            }
        }
    }

    outImage = std::move(tempImages[currentImage]);
}

bool Compressor::SaveEncoded(const std::string& name) const
{
    FILE* file = fopen(name.c_str(), "wb");
    if (!file)
    {
        std::cout << "Failed to open target encoded file '" << name << "': " << stderr << std::endl;
        return false;
    }

    if (fwrite(mDomains.data(), mDomains.size() * sizeof(Domain), 1, file) != 1)
    {
        std::cout << "Failed to write domains data: " << stderr << std::endl;
        fclose(file);
        return false;
    }

    fclose(file);
    return true;
}

//////////////////////////////////////////////////////////////////////////

int main()
{
    Compressor compressor;

    if (!compressor.LoadSourceImage("../city.bmp"))
    {
        std::cout << "Failed to load source image" << std::endl;
        system("pause");
        return 1;
    }

    compressor.SelfSimilaritySearch();

    ImageData decompressed;
    compressor.Decompress(decompressed);
    compressor.SaveEncoded("../encoded.dat");

    std::string fileName = "../fractal.bmp";
    compressor.SaveImage(fileName, decompressed);

    std::cout << std::endl << "=== COMPRESSED IMAGE STATS ===" << std::endl;
    Difference diff = Compressor::Compare(compressor.GetReferenceImage(), decompressed);
    std::cout << "MSE      = " << std::setw(8) << std::setprecision(6) << diff.averageError << std::endl;
    std::cout << "PSNR     = " << std::setw(8) << std::setprecision(6) << diff.psnr << "dB" << std::endl;
    std::cout << "maxError = " << std::setw(8) << std::setprecision(6) << diff.maxError << std::endl;

    system("pause");
    return 0;
}
