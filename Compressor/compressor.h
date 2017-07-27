#pragma once

#include "common.h"
#include "domain.h"
#include "image.h"
#include "quadtree.h"

#include <Windows.h>
#include <vector>
#include <string>
#include <mutex>


//////////////////////////////////////////////////////////////////////////

struct RangeContext
{
    // range location
    uint32 rx0, ry0;

    // image for comparisons
    const Image& image;

    // preallocated arrays for range and domain pixels
    std::vector<uint8>& rangeDataCache;
    std::vector<uint8>& domainDataCache;

    RangeContext(const Image& image, std::vector<uint8>& rangeDataCache, std::vector<uint8>& domainDataCache)
        : image(image), rangeDataCache(rangeDataCache), domainDataCache(domainDataCache)
    { }

    RangeContext(const RangeContext&) = default;
};

struct DomainMatchParams
{
    const RangeContext& rangeContext;

    // domain location
    uint32 dx0, dy0;
    
    uint8 transform;

    DomainMatchParams(const RangeContext& rangeContext)
        : rangeContext(rangeContext)
    { }
};

struct RangeDecompressContext
{
    uint32 rx0, ry0;
    uint32 rangeSize;
    uint32& domainIndex;
    QuadtreeCode& quadtreeCode;
    const Image& srcImage;
    Image& destImage;

    RangeDecompressContext(const Image& srcImage, Image& destImage,
                               uint32& domainIndex, QuadtreeCode& quadtreeCode)
        : srcImage(srcImage), destImage(destImage), domainIndex(domainIndex), quadtreeCode(quadtreeCode)
    { }

    RangeDecompressContext(const RangeDecompressContext&) = default;
};

struct CompressorSettings
{
    float mseMultiplier;
    uint8 minRangeSize;
    uint8 maxRangeSize;
    bool disableImportance;

    CompressorSettings()
        : mseMultiplier(1.0f)
        , minRangeSize(4)
        , maxRangeSize(32)
        , disableImportance(false)
    { }
};

class Compressor
{
public:
    Compressor(const CompressorSettings& settings = CompressorSettings());

    // load compressed image from a file
    bool Load(const std::string& name);

    // save compressed image to a file
    bool Save(const std::string& name) const;

    // save compressed image as C file
    bool SaveAsSourceFile(const std::string& prefix, const std::string& name) const;

    // compress an image
    bool Compress(const Image& image);

    // decompress an image
    bool Decompress(Image& outImage) const;

private:
    // Calculate range block vs. domain block similarity.
    // Returns best MSE + intensity scaling and offset values
    float MatchDomain(const DomainMatchParams& params,
                      uint8 rangeSize, float& outScale, float& outOffset) const;

    // Returns best domain for a given range block
    float DomainSearch(const RangeContext& rangeContext,
                       uint8 rangeSize, Domain& outDomain) const;

    // Compress given root range block
    // Returns list of generated domains and quadtree describing spatial subdivision
    uint32 CompressRootRange(const RangeContext& rangeContext,
                             QuadtreeCode& outQuadtreeCode, std::vector<Domain>& outDomains) const;

    // decompress root range (this will be called recursively)
    void DecompressRange(const RangeDecompressContext& context) const;

    DomainsStats CalculateDomainStats() const;

    mutable std::mutex mMutex;

    // Image info
    uint32 mSize;
    uint32 mSizeBits;
    uint32 mSizeMask;

    // Compression info
    CompressorSettings mSettings;

    using Domains = std::vector<Domain>;

    // compressed data
    QuadtreeCode mQuadtreeCode;
    Domains mDomains;
};
