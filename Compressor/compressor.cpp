#include "compressor.h"
#include <iostream>
#include <assert.h>
#include <algorithm>
#include <iomanip>
#include <thread>
#include <mutex>


#define HEADER_MAGIC 'icf '

//////////////////////////////////////////////////////////////////////////

const uint32 RANGE_BLOCK_SIZE = 8;

//////////////////////////////////////////////////////////////////////////

struct Header
{
    uint32 magic;
    uint32 imageSize;
    uint32 numDomains;
};

//////////////////////////////////////////////////////////////////////////

namespace {

void TransformLocation(uint32 x, uint32 y, uint8 transform, uint32& outX, uint32& outY)
{
    const uint32 offset = RANGE_BLOCK_SIZE - 1;

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


float Compressor::MatchDomain(const Image& rangeImage, const Image& domainImage,
                              const DomainMatchParams& params, float& outScale, float& outOffset) const
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
        for (uint32 x = 0; x < RANGE_BLOCK_SIZE; x++)
        {
            // transform range location to domain location and wrap around (if coordinate is too big)
            uint32 tx, ty;
            TransformLocation(x, y, params.transform, tx, ty);

            // sample domain block (it's already downsampled)
            const uint8 domainPixelColor = domainImage.SampleWrapped(params.dx0 + tx, params.dy0 + ty);

            // sample range blocks pixel
            const uint8 rangePixelColor = rangeImage.Sample(params.rx0 + x, params.ry0 + y);

            // these will be used below
            gh += domainPixelColor * rangePixelColor;
            gSqrSum += domainPixelColor * domainPixelColor;
            gSum += domainPixelColor;
            hSum += rangePixelColor;

            domainData[index] = domainPixelColor;
            rangeData[index] = rangePixelColor;
            index++;
        }
    }

    // find pixel value scaling and offset coefficients that minimizes MSE
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

    // quantize coefficients
    Domain d;
    d.SetScale(outScale);
    d.SetOffset(outOffset);

    // calculate MSE (including color scaling and offset)
    uint32 diffSum = 0;
    for (uint32 i = 0; i < k; ++i)
    {
        int32 g = (int32)d.TransformColor(domainData[i]); //outScale * domainData[i] + outOffset;
        int32 h = (int32)rangeData[i];
        int32 diff = g - h;
        diffSum += diff * diff;
    }
    return (float)diffSum * invK;
}

float Compressor::DomainSearch(const Image& rangeImage, const Image& domainImage, uint32 rx0, uint32 ry0, Domain& outDomain) const
{
    Domain bestDomain;
    float bestCost = FLT_MAX;

    const uint32 DOMAIN_MAX_LOCATIONS = (1 << DOMAIN_LOCATION_BITS);
    const uint32 DOMAIN_LOC_STEP = 1 << (domainImage.GetSizeBits() - DOMAIN_LOCATION_BITS);

    DomainMatchParams params;
    params.rx0 = rx0;
    params.ry0 = ry0;

    // iterate through all possible domains locations
    for (uint32 y = 0; y < DOMAIN_MAX_LOCATIONS; y++)
    {
        params.dy0 = y * DOMAIN_LOC_STEP;
        for (uint32 x = 0; x < DOMAIN_MAX_LOCATIONS; x++)
        {
            params.dx0 = x * DOMAIN_LOC_STEP;

            // iterate through all possible domains->range transforms
            for (uint8 t = 0; t < DOMAIN_MAX_TRANSFORMS; ++t)
            {
                params.transform = t;

                float scale, offset;
                const float currentCost = MatchDomain(rangeImage, domainImage, params, scale, offset);
                if (currentCost < bestCost)
                {
                    bestDomain.x = x;
                    bestDomain.y = y;
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

bool Compressor::Compress(const Image& image)
{
    if (image.GetSize() < RANGE_BLOCK_SIZE)
    {
        std::cout << "Image is too small" << std::endl;
        return false;
    }

    const Image downsampledImage = image.Downsample();

    mSize = image.GetSize();
    mSizeBits = image.GetSizeBits();
    mSizeMask = image.GetSizeMask();

    const uint32 numRangesInColumn = image.GetSize() / RANGE_BLOCK_SIZE;
    const uint32 numThreads = std::min<uint32>(numRangesInColumn, std::thread::hardware_concurrency());
    const uint32 rowsPerThread = numRangesInColumn / numThreads;
    const uint32 totalRangeBlocks = numRangesInColumn * numRangesInColumn;

    std::mutex mutex;
    uint32 finishedRangeBlocks = 0;

    std::vector<std::vector<Domain>> domainsPerThread;
    domainsPerThread.resize(numThreads);

    const auto threadCallback = [&](uint32 threadID)
    {
        assert(threadID < numThreads);
        std::vector<Domain>& domains = domainsPerThread[threadID];

        for (uint32 i = 0; i < rowsPerThread; ++i) // iterate local rows
        {
            // range block Y coordinate
            uint32 ry0 = RANGE_BLOCK_SIZE * (rowsPerThread * threadID + i);

            for (uint32 rx0 = 0; rx0 < image.GetSize(); rx0 += RANGE_BLOCK_SIZE) // range block X coordinate
            {
                Domain domain;
                float mse = DomainSearch(image, downsampledImage, rx0, ry0, domain);
                float psnr = 10.0f * log10f(255.0f * 255.0f / mse);

                domains.push_back(domain);

                // progress indicator
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    finishedRangeBlocks++;
                    std::cout << std::setw(5) << finishedRangeBlocks << " /" << std::setw(5) << totalRangeBlocks << " (" <<
                        std::setw(8) << std::setprecision(3) << (100.0f * (float)finishedRangeBlocks / (float)totalRangeBlocks) << "%)\r";

                    /*
                    std::cout << std::setw(5) << finishedRangeBlocks << " / " << std::setw(5) << totalRangeBlocks
                        << " Domain: loc=(" << std::setw(3) << (uint32)domain.x << "," << std::setw(3) << (uint32)domain.y << ")"
                        << ", t=" << (uint32)domain.transform
                        << ", scale=" << std::setw(8) << std::setprecision(3) << domain.GetScale()
                        << ", offset=" << std::setw(8) << std::setprecision(3) << domain.GetOffset() << ", "
                        << "MSE=" << std::setw(8) << std::setprecision(3) << mse << " ("
                        << std::setw(8) << std::setprecision(3) << psnr << " dB)" << std::endl;
                        */

                }
            }
        }
    };

    std::cout << "Compressing using " << numThreads << " threads... " << std::endl;

    // launch threads
    std::vector<std::thread> threads;
    for (uint32 i = 0; i < numThreads; ++i)
    {
        threads.emplace_back(threadCallback, i);
    }

    // wait for threads and merge results (domains)
    for (uint32 i = 0; i < numThreads; ++i)
    {
        threads[i].join();

        for (const Domain& domain : domainsPerThread[i])
        {
            mDomains.push_back(domain);
        }
    }

    std::cout << std::endl;

    // print domains stats
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

    return true;
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

bool Compressor::Decompress(Image& outImage) const
{
    if (mDomains.empty())
    {
        std::cout << "There is no encoded data" << std::endl;
        return false;
    }

    const uint32 MAX_ITERATIONS = 40;
    const uint32 domainScaling = mSizeBits - DOMAIN_LOCATION_BITS;

    std::cout << "Decompressing..." << std::endl;

    uint32 currentImage = 0;
    Image tempImages[2];
    tempImages[0].Resize(mSize);
    tempImages[1].Resize(mSize);

    for (uint32 i = 0; i < MAX_ITERATIONS; ++i)
    {
        // swap images
        currentImage ^= 1;
        const Image& src = tempImages[currentImage ^ 1];
        Image& dest = tempImages[currentImage];

        uint32 domainIndex = 0;
        for (uint32 ry0 = 0; ry0 < mSize; ry0 += RANGE_BLOCK_SIZE)
        {
            for (uint32 rx0 = 0; rx0 < mSize; rx0 += RANGE_BLOCK_SIZE)
            {
                const Domain& domain = mDomains[domainIndex++];

                for (uint32 y = 0; y < RANGE_BLOCK_SIZE; y++)
                {
                    const uint32 ry = ry0 + y;

                    for (uint32 x = 0; x < RANGE_BLOCK_SIZE; x++)
                    {
                        // transform range block location to domain location
                        uint32 tx, ty;
                        TransformLocation(x, y, domain.transform, tx, ty);

                        // decode domain location (to picture space)
                        const uint32 dx = (domain.x + tx) << domainScaling;
                        const uint32 dy = (domain.y + ty) << domainScaling;
   
                        // sample domain (with downsampling)
                        const uint32 domainPixelColor = src.SampleDomain(dx, dy);

                        // transform color
                        dest.WritePixel(x + rx0, ry, domain.TransformColor((uint8)domainPixelColor));
                    }
                }
            }
        }
    }

    outImage = std::move(tempImages[currentImage]);
    return true;
}

bool Compressor::Load(const std::string& name)
{
    FILE* file = fopen(name.c_str(), "rb");
    if (!file)
    {
        std::cout << "Failed to open compressed file '" << name << "': " << stderr << std::endl;
        return false;
    }

    Header header;
    if (fread(&header, sizeof(Header), 1, file) != 1)
    {
        std::cout << "Failed to read compressed file header: " << stderr << std::endl;
        fclose(file);
        return false;
    }

    if (header.magic != HEADER_MAGIC)
    {
        std::cout << "Corrupted/invalid file" << std::endl;
        fclose(file);
        return false;
    }

    if ((header.imageSize & (header.imageSize - 1)) != 0 || header.numDomains == 0)
    {
        std::cout << "Corrupted file" << std::endl;
        return false;
    }

    // read file size
    mSize = header.imageSize;
    mSizeBits = 0;
    {
        uint32 i = mSize;
        while (i >>= 1) ++mSizeBits;
    }
    mSizeMask = (1 << mSizeBits) - 1;

    // read domains
    mDomains.resize(header.numDomains);
    if (fread(mDomains.data(), mDomains.size() * sizeof(Domain), 1, file) != 1)
    {
        std::cout << "Failed to read domains data: " << stderr << std::endl;
        fclose(file);
        return false;
    }

    fclose(file);
    return true;
}

bool Compressor::Save(const std::string& name) const
{
    FILE* file = fopen(name.c_str(), "wb");
    if (!file)
    {
        std::cout << "Failed to open target encoded file '" << name << "': " << stderr << std::endl;
        return false;
    }

    Header header;
    header.magic = HEADER_MAGIC;
    header.imageSize = mSize;
    header.numDomains = (uint32)mDomains.size();

    if (fwrite(&header, sizeof(Header), 1, file) != 1)
    {
        std::cout << "Failed to write compressed file header: " << stderr << std::endl;
        fclose(file);
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