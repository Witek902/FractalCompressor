#pragma once

#include "common.h"
#include "domain.h"
#include "image.h"

#include <Windows.h>
#include <vector>
#include <string>
#include <immintrin.h>


//////////////////////////////////////////////////////////////////////////

struct DomainMatchParams
{
    // range location
    uint32 rx0, ry0;

    // domain location
    uint32 dx0, dy0;
    
    uint8 transform;
};


class Compressor
{
public:
    // load compressed image from a file
    bool Load(const std::string& name);

    // save compressed image to a file
    bool Save(const std::string& name) const;

    // compress an image
    bool Compress(const Image& image);

    // decompress an image
    bool Decompress(Image& outImage) const;

    // get compressed image in bits
    size_t GetCompressedSize() const
    {
        return mDomains.size() * (2 * DOMAIN_LOCATION_BITS + DOMAIN_TRANSFORM_BITS + DOMAIN_SCALE_BITS + DOMAIN_OFFSET_BITS);
    }

private:
    // Calculate range block vs. domain block similarity.
    // Returns best MSE + intensity scaling and offset values
    float MatchDomain(const Image& rangeImage, const Image& domainImage,
                      const DomainMatchParams& params, float& outScale, float& outOffset) const;

    // search best domain for given range block
    float DomainSearch(const Image& rangeImage, const Image& domainImage,
                       uint32 rx, uint32 ry, Domain& outDomain) const;

    DomainsStats CalculateDomainStats() const;

    uint32 mSize;
    uint32 mSizeBits;
    uint32 mSizeMask;

    // compressed data
    std::vector<Domain> mDomains;
};
