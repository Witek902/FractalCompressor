#include "compressor.h"
#include "quadtree.h"

#include <iostream>
#include <assert.h>
#include <algorithm>
#include <iomanip>
#include <thread>
#include <fstream>


//#define DISABLE_QUADTREE_SUBDIVISION

//////////////////////////////////////////////////////////////////////////

#define HEADER_MAGIC 'icf '

struct Header
{
    uint32 magic;
    uint32 imageSize;
    uint32 quadtreeDataSize;    // in bits
    uint32 numDomains;
    CompressorSettings settings;
};

//////////////////////////////////////////////////////////////////////////

namespace {

FORCE_INLINE void TransformLocation(uint32 rangeSize, uint32 x, uint32 y, uint8 transform, uint32& outX, uint32& outY)
{
    const uint32 offset = rangeSize - 1;

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

Compressor::Compressor(const CompressorSettings& settings)
    : mSettings(settings)
{}

//////////////////////////////////////////////////////////////////////////
// Compression
//////////////////////////////////////////////////////////////////////////

float Compressor::MatchDomain(const DomainMatchParams& params, uint8 rangeSize,
                              float& outScale, float& outOffset) const
{
    // number of pixels in the range
    const uint32 k = rangeSize * rangeSize;
    const float invK = 1.0f / (float)k;

    const RangeContext& rangeCtx = params.rangeContext;

    uint32 gh = 0, gSum = 0, gSqrSum = 0, hSum = 0, index = 0;

    for (uint8 y = 0; y < rangeSize; y++)
    {
        for (uint8 x = 0; x < rangeSize; x++)
        {
            // transform range location to domain location and wrap around (if coordinate is too big)
            uint32 tx, ty;
            TransformLocation(rangeSize, x, y, params.transform, tx, ty);

            // sample domain blocks pixel (with downsample)
            // TODO this could be cached
            const uint8 domainPixelColor = rangeCtx.image.SampleDomain(params.dx0 + 2 * tx, params.dy0 + 2 * ty);

            // sample range blocks pixel
            const uint8 rangePixelColor = rangeCtx.image.Sample(rangeCtx.rx0 + x, rangeCtx.ry0 + y);

            // these will be used below
            gh += domainPixelColor * rangePixelColor;
            gSqrSum += domainPixelColor * domainPixelColor;
            gSum += domainPixelColor;
            hSum += rangePixelColor;

            rangeCtx.domainDataCache[index] = domainPixelColor;
            rangeCtx.rangeDataCache[index] = rangePixelColor;
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
        int32 g = (int32)d.TransformColor(rangeCtx.domainDataCache[i]);
        int32 h = (int32)rangeCtx.rangeDataCache[i];
        int32 diff = g - h;
        diffSum += diff * diff;
    }
    return (float)diffSum * invK;
}

float Compressor::DomainSearch(const RangeContext& rangeContext, uint8 rangeSize, Domain& outDomain) const
{
    Domain bestDomain;
    float bestCost = FLT_MAX;

    const uint32 domainScaling = mSizeBits > DOMAIN_LOCATION_BITS ? mSizeBits - DOMAIN_LOCATION_BITS : 0;
    const uint32 maxDomainLocations = std::min<uint32>(mSize, 1 << DOMAIN_LOCATION_BITS);

    DomainMatchParams matchParams(rangeContext);

    // iterate through all possible domains locations
    for (uint32 y = 0; y < maxDomainLocations; y++)
    {
        matchParams.dy0 = y << domainScaling;
        for (uint32 x = 0; x < maxDomainLocations; x++)
        {
            matchParams.dx0 = x << domainScaling;

            // iterate through all possible domains->range transforms
            for (uint8 t = 0; t < DOMAIN_MAX_TRANSFORMS; ++t)
            {
                matchParams.transform = t;

                float scale, offset;
                const float currentCost = MatchDomain(matchParams, rangeSize, scale, offset);
                if (currentCost < bestCost)
                {
                    bestDomain.SetScale(scale);
                    bestDomain.SetOffset(offset);
                    bestDomain.x = x;
                    bestDomain.y = y;
                    bestDomain.transform = t;

                    bestCost = currentCost;
                }
            }
        }
    }

    outDomain = bestDomain;
    return bestCost;
}

uint32 Compressor::CompressRootRange(const RangeContext& rangeContext,
                                     QuadtreeCode& outQuadtreeCode, std::vector<Domain>& outDomains) const
{
    // MSE threshold for the first subdivision level
    const float initialThreshold = mSettings.mseMultiplier;
    const float adaptiveThresholdFactor = 1.0f;    // threshold multiplier for consecutive levels

    uint32 numDomainsInTree = 0;

    std::function<void(uint32, uint32, uint8, float)> compressSubRange;
    compressSubRange = [&](uint32 rx0, uint32 ry0, uint8 rangeSize, float mseThreshold)
    {
        RangeContext subRangeContext(rangeContext);
        subRangeContext.rx0 = rx0;
        subRangeContext.ry0 = ry0;

        Domain domain;
        const float mse = DomainSearch(subRangeContext, rangeSize, domain);

        bool subdivide = false;

#ifndef DISABLE_QUADTREE_SUBDIVISION
        if (rangeSize > mSettings.minRangeSize)
        {
            subdivide = (mse > mseThreshold) && (rangeSize > mSettings.minRangeSize);
            outQuadtreeCode.Push(subdivide); // don't waste quadtree space if this is the lowest possible level
        }
#endif // DISABLE_QUADTREE_SUBDIVISION

        // recursively subdivide range
        if (subdivide)
        {
            const uint8 subRangeSize = rangeSize / 2;
            const float subRangeThreshold = mseThreshold * adaptiveThresholdFactor;

            compressSubRange(rx0,                   ry0,                subRangeSize, subRangeThreshold);
            compressSubRange(rx0 + subRangeSize,    ry0,                subRangeSize, subRangeThreshold);
            compressSubRange(rx0,                   ry0 + subRangeSize, subRangeSize, subRangeThreshold);
            compressSubRange(rx0 + subRangeSize,    ry0 + subRangeSize, subRangeSize, subRangeThreshold);
        }
        else
        {

            /*
            {
                std::lock_guard<std::mutex> lock(mMutex);
                std::cout << "Range " << std::setw(3) << rx0 << ',' << std::setw(3) << ry0 << " (" << std::setw(2) << rangeSize << " px) -> "
                    << "Domain: loc=(" << std::setw(3) << (uint32)domain.x << "," << std::setw(3) << (uint32)domain.y << ")"
                    << ", t=" << (uint32)domain.transform
                    << ", scale=" << std::setw(8) << std::setprecision(3) << domain.GetScale()
                    << ", offset=" << std::setw(8) << std::setprecision(3) << domain.GetOffset() << ", "
                    << "MSE=" << std::setw(8) << std::setprecision(3) << mse << std::endl;
            }
            */

            outDomains.push_back(domain);
            numDomainsInTree++;
        }
    };

    compressSubRange(rangeContext.rx0, rangeContext.ry0, mSettings.maxRangeSize, initialThreshold);
    return numDomainsInTree;
}

bool Compressor::Compress(const Image& image)
{
    const uint32 maxRangeSize = mSettings.maxRangeSize;

    if (image.GetSize() < maxRangeSize)
    {
        std::cout << "Image is too small" << std::endl;
        return false;
    }

    mSize = image.GetSize();
    mSizeBits = image.GetSizeBits();
    mSizeMask = image.GetSizeMask();

    const uint32 numRangesInColumn = image.GetSize() / maxRangeSize;
    const uint32 numThreads = std::min<uint32>(numRangesInColumn, std::thread::hardware_concurrency());
    const uint32 rowsPerThread = numRangesInColumn / numThreads;
    const uint32 totalRangeBlocks = numRangesInColumn * numRangesInColumn;

    uint32 finishedRangeBlocks = 0;

    std::vector<QuadtreeCode> quadtreesPerThread;
    std::vector<std::vector<Domain>> domainsPerThread;
    quadtreesPerThread.resize(numThreads);
    domainsPerThread.resize(numThreads);

    const auto threadCallback = [&](uint32 threadID)
    {
        assert(threadID < numThreads);
        std::vector<Domain>& domains = domainsPerThread[threadID];
        QuadtreeCode& quadtreeCode = quadtreesPerThread[threadID];

        const uint32 numRangePixels = maxRangeSize * maxRangeSize;

        std::vector<uint8> domainDataCache;
        std::vector<uint8> rangeDataCache;
        domainDataCache.resize(numRangePixels);
        rangeDataCache.resize(numRangePixels);

        RangeContext rangeContext(image, rangeDataCache, domainDataCache);

        for (uint32 i = 0; i < rowsPerThread; ++i) // iterate local rows
        {
            // range block Y coordinate
            rangeContext.ry0 = maxRangeSize * (rowsPerThread * threadID + i);

            for (uint32 rx0 = 0; rx0 < image.GetSize(); rx0 += maxRangeSize) // range block X coordinate
            {
                rangeContext.rx0 = rx0;
                const uint32 numDomainsInTree = CompressRootRange(rangeContext, quadtreeCode, domains);

                // progress indicator
                {
                    std::lock_guard<std::mutex> lock(mMutex);
                    finishedRangeBlocks++;
                    std::cout << std::setw(5) << finishedRangeBlocks << " /" << std::setw(5) << totalRangeBlocks << " (" <<
                        std::setw(8) << std::setprecision(3) << (100.0f * (float)finishedRangeBlocks / (float)totalRangeBlocks) << "%)\r";
                }
            }
        }
    };

    // launch threads
    std::vector<std::thread> threads;
    for (uint32 i = 0; i < numThreads; ++i)
    {
        threads.emplace_back(threadCallback, i);
    }

    // wait for threads and merge results (domains + quadtrees)
    mQuadtreeCode.Clear();
    mDomains.clear();
    for (uint32 i = 0; i < numThreads; ++i)
    {
        threads[i].join();

        for (const Domain& domain : domainsPerThread[i])
        {
            mDomains.push_back(domain);
        }

        mQuadtreeCode.Push(quadtreesPerThread[i]);
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

    const size_t domainsDataSize = mDomains.size() * sizeof(Domain);
    const size_t quadtreeElements = mQuadtreeCode.GetNumElements();
    const size_t totalSize = domainsDataSize + sizeof(QuadtreeCode::ElementType) * quadtreeElements;
    const float bitsPerPixel = (float)(totalSize * 8) / (float)(image.GetSize() * image.GetSize());
    std::cout << "Num domains:     " << mDomains.size() << std::endl;
    std::cout << "Quadtree size:   " << mQuadtreeCode.GetSize() << std::endl;
    std::cout << "Compressed size: " << totalSize << " bytes (" << std::setw(8) << std::setprecision(4) << bitsPerPixel << " bpp)" << std::endl;

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

//////////////////////////////////////////////////////////////////////////
// Decompression
//////////////////////////////////////////////////////////////////////////

void Compressor::DecompressRange(const RangeDecompressContext& context) const
{
    assert(context.rangeSize >= mSettings.minRangeSize);
    assert(context.rx0 < mSize);
    assert(context.ry0 < mSize);

    // check if this range should be subdivided
    bool subdivide = false;
    if (context.rangeSize > mSettings.minRangeSize)
    {
        subdivide = context.quadtreeCode.Get();
    }

    if (subdivide)
    {
        RangeDecompressContext childContext(context);
        childContext.rangeSize /= 2;
        for (uint32 i = 0; i < 2; ++i)
        {
            for (uint32 j = 0; j < 2; ++j)
            {
                childContext.rx0 = context.rx0 + j * childContext.rangeSize;
                childContext.ry0 = context.ry0 + i * childContext.rangeSize;
                DecompressRange(childContext);
            }
        }
    }
    else // !subdivide
    {
        const uint32 domainScaling = mSizeBits > DOMAIN_LOCATION_BITS ? mSizeBits - DOMAIN_LOCATION_BITS : 0;
        const Domain& domain = mDomains[context.domainIndex++];

        for (uint32 y = 0; y < context.rangeSize; y++)
        {
            const uint32 ry = context.ry0 + y;

            for (uint32 x = 0; x < context.rangeSize; x++)
            {
                // transform range block location to domain location
                uint32 tx, ty;
                TransformLocation(context.rangeSize, x, y, domain.transform, tx, ty);

                // decode domain location (to picture space)
                const uint32 dx = (domain.x << domainScaling) + 2 * tx;
                const uint32 dy = (domain.y << domainScaling) + 2 * ty;

                // sample domain (with downsampling)
                const uint32 domainPixelColor = context.srcImage.SampleDomain(dx, dy);

                // transform color
                context.destImage.WritePixel(x + context.rx0, ry, domain.TransformColor((uint8)domainPixelColor));
            }
        }
    }
}

bool Compressor::Decompress(Image& outImage) const
{
    if (mDomains.empty())
    {
        std::cout << "There is no encoded data" << std::endl;
        return false;
    }

    const uint32 MAX_ITERATIONS = 100;

    uint32 currentImage = 0;
    Image tempImages[2];
    tempImages[0].Resize(mSize, 1);
    tempImages[1].Resize(mSize, 1);

    QuadtreeCode tmpQuadtreeCode(mQuadtreeCode);

    for (uint32 i = 0; i < MAX_ITERATIONS; ++i)
    {
        // swap images
        currentImage ^= 1;
        const Image& src = tempImages[currentImage ^ 1];
        Image& dest = tempImages[currentImage];

        tmpQuadtreeCode.ResetCursor();

        // iterate through root domains
        uint32 domainIndex = 0;

        RangeDecompressContext context(src, dest, domainIndex, tmpQuadtreeCode);
        context.rangeSize = mSettings.maxRangeSize;

        for (uint32 ry0 = 0; ry0 < mSize; ry0 += mSettings.maxRangeSize)
        {
            for (uint32 rx0 = 0; rx0 < mSize; rx0 += mSettings.maxRangeSize)
            {
                context.rx0 = rx0;
                context.ry0 = ry0;
                DecompressRange(context);
            }
        }

        //assert(domainIndex == (uint32)mDomains.size());
    }

    outImage = std::move(tempImages[currentImage]);
    return true;
}


//////////////////////////////////////////////////////////////////////////
// Input-output
//////////////////////////////////////////////////////////////////////////

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

    if (mSettings.minRangeSize <= 2 || mSettings.maxRangeSize < mSettings.minRangeSize)
    {
        std::cout << "Corrupted/invalid file" << std::endl;
        fclose(file);
        return false;
    }

    // calculate number of elements from number of bits (round up)
    const uint32 quadtreeCodeElements = (header.quadtreeDataSize + 8 * sizeof(QuadtreeCode::ElementType) - 1) / (8 * sizeof(QuadtreeCode::ElementType));
    if (quadtreeCodeElements > 0)
    {
        std::vector<QuadtreeCode::ElementType> code;
        code.resize(quadtreeCodeElements);
        if (fread(code.data(), quadtreeCodeElements * sizeof(QuadtreeCode::ElementType), 1, file) != 1)
        {
            std::cout << "Failed to read quadtree data: " << stderr << std::endl;
            fclose(file);
            return false;
        }
        mQuadtreeCode.Load(code, header.quadtreeDataSize);
    }
    else
    {
        mQuadtreeCode.Clear();
    }

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
    header.quadtreeDataSize = mQuadtreeCode.GetSize();
    header.numDomains = (uint32)mDomains.size();
    header.settings = mSettings;

    if (fwrite(&header, sizeof(Header), 1, file) != 1)
    {
        std::cout << "Failed to write compressed file header: " << stderr << std::endl;
        fclose(file);
        return false;
    }

    if (mQuadtreeCode.GetNumElements() > 0)
    {
        if (fwrite(mQuadtreeCode.GetCode().data(), mQuadtreeCode.GetNumElements() * sizeof(QuadtreeCode::ElementType), 1, file) != 1)
        {
            std::cout << "Failed to write quadtree data: " << stderr << std::endl;
            fclose(file);
            return false;
        }
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

bool Compressor::SaveAsSourceFile(const std::string& prefix, const std::string& name) const
{
    std::ofstream file(name);
    if (!file.good())
    {
        std::cout << "Failed to open target encoded file '" << name << "': " << stderr << std::endl;
        return false;
    }

    file << "#include \"demo.h\"\n\n";

    if (mQuadtreeCode.GetNumElements() > 0)
    {
        file << "const unsigned int " << prefix << "QuadtreeData[] = \n{\n";
        for (uint32 i = 0; i < mQuadtreeCode.GetNumElements(); ++i)
        {
            file << "    0x" << std::hex << mQuadtreeCode.GetCode().data()[i] << ",\n";
        }
        file << "};\n\n";
    }

    {
        file << "const Domain " << prefix << "DomainsData[] = \n{\n";
        for (const Domain& d : mDomains)
        {
            file << std::dec << "    { " << d.x << ", " << d.y << ", " << d.transform << ", " << d.offset << ", " << d.scale << " },\n";
        }
        file << "};\n\n";
    }

    return true;
}