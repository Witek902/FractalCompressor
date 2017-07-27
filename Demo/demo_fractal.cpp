#include "stdafx.h"
#include "demo.h"
#include "image.h"
#include "stream.h"

unsigned char tempBuffer[IMAGE_SIZE * IMAGE_SIZE] = { 0 };

unsigned char lumaBuffer[IMAGE_SIZE * IMAGE_SIZE] = { 0 };
unsigned char cbBufferUpsampled[IMAGE_SIZE * IMAGE_SIZE] = { 0 };
unsigned char crBufferUpsampled[IMAGE_SIZE * IMAGE_SIZE] = { 0 };
// NOTE: these two below should be smaller, but the empty sections compress better if all have the same size
unsigned char cbBuffer[IMAGE_SIZE * IMAGE_SIZE] = { 0 }; // downsampled
unsigned char crBuffer[IMAGE_SIZE * IMAGE_SIZE] = { 0 }; // downsampled

// final color image
unsigned int finalImage[IMAGE_SIZE * IMAGE_SIZE] = { 0 };

//////////////////////////////////////////////////////////////////////////

FORCE_INLINE void TransformLocation(uint32 rangeSize, uint32 x, uint32 y, uint32 transform, uint32& outX, uint32& outY)
{
    const uint32 offset = rangeSize - 1;

    if (transform & 0x1)
        x = offset - x;

    uint32 rotation = transform >> 1;
    while (rotation-- > 0)
    {
        const uint32 temp = x;
        x = offset - y;
        y = temp;
    }

    outX = x;
    outY = y;
}

FORCE_INLINE uint8 TransformColor(const Domain& domain, const uint8 in)
{
    int32 intOffset = (int32)domain.offset;
    intOffset <<= (DOMAIN_OFFSET_RANGE_BITS - DOMAIN_OFFSET_BITS);
    intOffset -= DOMAIN_OFFSET_RANGE;

    int32 intScale = (int32)domain.scale;
    intScale -= (1 << (DOMAIN_SCALE_BITS - 1));

    int32 val = (int32)in;
    val = ((intScale * val) >> (DOMAIN_SCALE_BITS - DOMAIN_SCALE_RANGE_BITS)) + intOffset;
    return static_cast<uint8>(Max<int32>(0, Min<int32>(255, val)));
}

void DecompressRange(const RangeDecompressContext& context)
{
    // check if this range should be subdivided
    bool subdivide = false;
    if (context.rangeSize > MIN_RANGE_SIZE)
    {
        subdivide = context.quadtreeCode.GetBit();
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
        const Domain& domain = context.domains[context.domainIndex++];
        for (uint32 y = 0; y < context.rangeSize; y++)
        {
            const uint32 ry = context.ry0 + y;

            for (uint32 x = 0; x < context.rangeSize; x++)
            {
                // transform range block location to domain location
                uint32 tx, ty;
                TransformLocation(context.rangeSize, x, y, domain.transform, tx, ty);

                // decode domain location (to picture space)
                const uint32 dx = (domain.x << context.domainScaling) + 2 * tx;
                const uint32 dy = (domain.y << context.domainScaling) + 2 * ty;

                // sample domain (with downsampling)
                const uint32 domainPixelColor = context.srcImage.SampleDomain(dx, dy);

                // transform color
                context.destImage.WritePixel(x + context.rx0, ry, TransformColor(domain, (uint8)domainPixelColor));
            }
        }
    }
}

void Decompress(uint32 size, uint32 domainScaling, const Domain domains[], const uint32 quadTreeCode[], uint8* outputBuffer)
{
    uint32 currentImage = 0;
    Image tempImages[2] = { { outputBuffer, size, size - 1 }, { tempBuffer, size, size - 1 } };

    Stream tmpQuadtreeCode(quadTreeCode);

    for (uint32 i = 0; i < 128; ++i)
    {
        // swap images
        const Image& src = tempImages[currentImage];
        currentImage ^= 1;
        Image& dest = tempImages[currentImage];

        tmpQuadtreeCode.ResetCursor();

        // iterate through root domains
        uint32 domainIndex = 0;

        RangeDecompressContext context(src, dest, domainIndex, tmpQuadtreeCode);
        context.rangeSize = MAX_RANGE_SIZE;
        context.domains = domains;
        context.domainScaling = domainScaling;  // TODO this depends on image size

        for (uint32 ry0 = 0; ry0 < size; ry0 += MAX_RANGE_SIZE)
        {
            for (uint32 rx0 = 0; rx0 < size; rx0 += MAX_RANGE_SIZE)
            {
                context.rx0 = rx0;
                context.ry0 = ry0;
                DecompressRange(context);
            }
        }
    }
}

FORCE_INLINE static void Decompress()
{
    const uint32 lumaDomainScaling = IMAGE_SIZE_BITS > DOMAIN_LOCATION_BITS ? IMAGE_SIZE_BITS - DOMAIN_LOCATION_BITS : 0;
    Decompress(IMAGE_SIZE, lumaDomainScaling, lumaDomainsData, lumaQuadtreeData, lumaBuffer);

    const uint32 chromaDomainScaling = CHROMA_IMAGE_SIZE_BITS > DOMAIN_LOCATION_BITS ? CHROMA_IMAGE_SIZE_BITS - DOMAIN_LOCATION_BITS : 0;
    Decompress(CHROMA_IMAGE_SIZE, chromaDomainScaling, cbDomainsData, cbQuadtreeData, cbBuffer);
    Decompress(CHROMA_IMAGE_SIZE, chromaDomainScaling, crDomainsData, crQuadtreeData, crBuffer);

    // upsample chroma
    {
        const uint32 chromaSubsampling = 4;
        for (uint32 y = 0; y < CHROMA_IMAGE_SIZE * chromaSubsampling; ++y)
        {
            for (uint32 x = 0; x < CHROMA_IMAGE_SIZE * chromaSubsampling; ++x)
            {
                const uint32 srcLoc = CHROMA_IMAGE_SIZE * (y / chromaSubsampling) + (x / chromaSubsampling);
                const uint32 targetLoc = chromaSubsampling * CHROMA_IMAGE_SIZE * y + x;
                cbBufferUpsampled[targetLoc] = cbBuffer[srcLoc];
                crBufferUpsampled[targetLoc] = crBuffer[srcLoc];
            }
        }
    }

    for (uint32 i = 0; i < IMAGE_SIZE * IMAGE_SIZE; ++i)
    {
        const int32 y = lumaBuffer[i];
        const int32 cb = cbBufferUpsampled[i];
        const int32 cr = crBufferUpsampled[i];
        finalImage[i] = (CONVERT_YCbCr2B(y, cb, cr) << 16) | (CONVERT_YCbCr2G(y, cb, cr) << 8) | (CONVERT_YCbCr2R(y, cb, cr));
        //finalImage[i] = (cb << 16) | (cb << 8) | (cb);
    }
}

//////////////////////////////////////////////////////////////////////////

static const BITMAPINFO bmi =
{
    { sizeof(BITMAPINFOHEADER), IMAGE_SIZE, IMAGE_SIZE, 1, 32, BI_RGB, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0 },
};

void entrypoint(void)
{
    Decompress();

    RECT rect;
    GetClientRect(GetDesktopWindow(), &rect);

    HDC hDC = GetDC(CreateWindowExA(WS_EX_TOPMOST, "static", 0, WS_VISIBLE | WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0, rect.right, rect.bottom, 0, 0, 0, 0));

    SetStretchBltMode(hDC, STRETCH_HALFTONE);
    //StretchDIBits(hDC, 0, 0, rect.right, rect.bottom, 0, 0, 1, 1, finalImage, &bmi, DIB_RGB_COLORS, SRCCOPY);
    StretchDIBits(hDC, (rect.right - rect.bottom) / 2, 0, rect.bottom, rect.bottom, 0, 0, IMAGE_SIZE, IMAGE_SIZE, finalImage, &bmi, DIB_RGB_COLORS, SRCCOPY);

    do { } while (!GetAsyncKeyState(VK_ESCAPE));
}
