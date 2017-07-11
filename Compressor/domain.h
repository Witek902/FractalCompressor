#pragma once

#include "common.h"
#include "settings.h"

#include <float.h>
#include <algorithm>


//////////////////////////////////////////////////////////////////////////

/**
* Structure describing domain block to range block mapping.
* This is the core of compressed image information - it drives the IFS during decompression.
*/
struct Domain
{
    // domain size (0 - 2x, 1 - 4x, 2 - 8x, 3 - 16x)
    uint16 size : 2;

    // domain location
    uint16 x : DOMAIN_LOCATION_BITS;
    uint16 y : DOMAIN_LOCATION_BITS;

    // bit 0:       domain flip (in local X axis)
    // bits 1-2:    domain rotation (0 - normal, 1 - 90 degree CCW, etc.)
    uint16 transform : 3;

    // color intensity mapping (scale and offset)
    uint16 offset : DOMAIN_OFFSET_BITS;
    uint16 scale : DOMAIN_SCALE_BITS;

    void SetOffset(float val)
    {
        const float maxValue = (float)((1 << DOMAIN_OFFSET_BITS) - 1);
        val /= (float)(DOMAIN_OFFSET_RANGE * 2);    // -0.5 ... 0.5
        val += 0.5f;                                //  0.0 ... 1.0
        val *= maxValue;
        val = std::max<float>(0.0f, std::min<float>(maxValue, val + 0.5f));
        offset = (uint16)val;
    }

    void SetScale(float val)
    {
        const float maxValue = (float)((1 << DOMAIN_SCALE_BITS) - 1);
        val /= (float)(DOMAIN_SCALE_RANGE * 2); // -0.5 ... 0.5
        val += 0.5f;                            //  0.0 ... 1.0
        val *= maxValue;
        val = std::max<float>(0.0f, std::min<float>(maxValue, val + 0.5f));
        scale = (uint16)val;

        //scale = (uint16)(64.0f * (0.5f + scale / 2.0f));
    }

    float GetOffset() const
    {
        const float maxValue = (float)((1 << DOMAIN_OFFSET_BITS) - 1);
        return ((float)offset / maxValue - 0.5f) * (float)(DOMAIN_OFFSET_RANGE * 2);
    }

    float GetScale() const
    {
        const float maxValue = (float)((1 << DOMAIN_SCALE_BITS) - 1);
        return ((float)scale / maxValue - 0.5f) * (float)(DOMAIN_SCALE_RANGE * 2);
    }

    uint8 TransformColor(uint8 in) const
    {
        int32 intOffset = (int32)offset;
        intOffset <<= (DOMAIN_OFFSET_RANGE_BITS - DOMAIN_OFFSET_BITS);
        intOffset -= DOMAIN_OFFSET_RANGE;

        int32 intScale = (int32)scale;
        intScale -= (1 << (DOMAIN_SCALE_BITS - 1));

        int32 val = (int32)in;
        val = ((intScale * val) >> (DOMAIN_SCALE_BITS - DOMAIN_SCALE_RANGE_BITS)) + intOffset; // TODO
        return std::max<int32>(0, std::min<int32>(255, val));
    }

};

static_assert(sizeof(Domain) == 4, "Invalid domain size");

//////////////////////////////////////////////////////////////////////////

struct DomainsStats
{
    float averageScale;
    float scaleVariance;
    float minScale;
    float maxScale;

    float averageOffset;
    float offsetVariance;
    float minOffset;
    float maxOffset;

    uint32 transformDistribution[8];

    DomainsStats()
        : averageScale(0.0f)
        , scaleVariance(0.0f)
        , minScale(FLT_MAX)
        , maxScale(FLT_MIN)
        , averageOffset(0.0f)
        , offsetVariance(0.0f)
        , minOffset(FLT_MAX)
        , maxOffset(FLT_MIN)
        , transformDistribution{0,0,0,0,0,0,0,0}
    { }
};
