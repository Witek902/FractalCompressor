#pragma once

#define WIN32_LEAN_AND_MEAN
#define WIN32_EXTRA_LEAN
#include <windows.h>

//////////////////////////////////////////////////////////////////////////

#define FORCE_INLINE
//#define FORCE_INLINE __forceinline

//////////////////////////////////////////////////////////////////////////

#define IMAGE_SIZE_BITS             9
#define IMAGE_SIZE                  (1 << IMAGE_SIZE_BITS)
#define CHROMA_SUBSAMPLING          2
#define CHROMA_IMAGE_SIZE_BITS      (IMAGE_SIZE_BITS - CHROMA_SUBSAMPLING)
#define CHROMA_IMAGE_SIZE           (IMAGE_SIZE >> CHROMA_SUBSAMPLING)

// TODO
#define MIN_RANGE_SIZE              4
#define MAX_RANGE_SIZE              32

#define DOMAIN_LOCATION_BITS        7
#define DOMAIN_MAX_TRANSFORMS       (1 << (DOMAIN_LOCATION_BITS - 1))
#define DOMAIN_TRANSFORM_BITS       3
#define DOMAIN_SCALE_BITS           7
#define DOMAIN_SCALE_RANGE_BITS     1
#define DOMAIN_SCALE_RANGE (1 << (DOMAIN_SCALE_RANGE_BITS - 1))
#define DOMAIN_OFFSET_BITS          7
#define DOMAIN_OFFSET_RANGE_BITS    9
#define DOMAIN_OFFSET_RANGE (1 << (DOMAIN_OFFSET_RANGE_BITS - 1))

//////////////////////////////////////////////////////////////////////////

#define CLIP(X) ( (X) > 255 ? 255 : (X) < 0 ? 0 : X)

// YCbCr -> RGB
#define CONVERT_YCbCr2R(Y, Cb, Cr) CLIP(Y + (91881 * Cr >> 16) - 179)
#define CONVERT_YCbCr2G(Y, Cb, Cr) CLIP(Y - ((22544 * Cb + 46793 * Cr) >> 16) + 135)
#define CONVERT_YCbCr2B(Y, Cb, Cr) CLIP(Y + (116129 * Cb >> 16) - 226)

//////////////////////////////////////////////////////////////////////////

using uint8 = unsigned char;
using uint32 = unsigned int;
using uint16 = unsigned short;
using int8 = char;
using int32 = int;
using int16 = short;

//////////////////////////////////////////////////////////////////////////

template<typename T>
FORCE_INLINE T Min(const T a, const T b)
{
    return a < b ? a : b;
}

template<typename T>
FORCE_INLINE T Max(const T a, const T b)
{
    return a > b ? a : b;
}

class QuadtreeCode;
class Image;

struct Domain
{
    uint16 x : DOMAIN_LOCATION_BITS;
    uint16 y : DOMAIN_LOCATION_BITS;
    uint16 transform : DOMAIN_TRANSFORM_BITS;
    uint16 offset : DOMAIN_OFFSET_BITS;
    uint16 scale : DOMAIN_SCALE_BITS;
};

struct RangeDecompressContext
{
    uint32 rx0, ry0;
    uint32 rangeSize;
    uint32 domainScaling;
    uint32& domainIndex;
    QuadtreeCode& quadtreeCode;
    const Domain* domains;
    const Image& srcImage;
    Image& destImage;


    RangeDecompressContext(const Image& srcImage, Image& destImage, uint32& domainIndex, QuadtreeCode& quadtreeCode)
        : srcImage(srcImage), destImage(destImage), domainIndex(domainIndex), quadtreeCode(quadtreeCode)
    { }

    RangeDecompressContext(const RangeDecompressContext&) = default;
};

//////////////////////////////////////////////////////////////////////////

// Image data

extern const unsigned int lumaQuadtreeData[];
extern const Domain lumaDomainsData[];
extern const unsigned int cbQuadtreeData[];
extern const Domain cbDomainsData[];
extern const unsigned int crQuadtreeData[];
extern const Domain crDomainsData[];
