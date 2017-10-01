#include "stdafx.h"
#include "common.h"
#include "points.h"

#define IMAGE_SCALING 4

static unsigned int finalImage[IMAGE_SCALING * IMAGE_SCALING * IMAGE_WIDTH * IMAGE_HEIGHT] = { 0 };

static const BITMAPINFO bmi =
{
    { sizeof(BITMAPINFOHEADER), IMAGE_SCALING * IMAGE_WIDTH, IMAGE_SCALING * IMAGE_HEIGHT, 1, 32, BI_RGB, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0 },
};

int entrypoint(void)
{
    for (int y = 0; y < IMAGE_SCALING * IMAGE_WIDTH; y++)
    {
        for (int x = 0; x < IMAGE_SCALING * IMAGE_HEIGHT; x++)
        {
            int32 color;
            int32 minDistance = 0x08000000;

            uint8 prevX = 0, prevY = 0;

            for (size_t j = 0; j < NUM_POINTS; ++j)
            {
                prevX += pointsX[j];
                prevY += pointsY[j];
                const int32 px = prevX * IMAGE_SCALING;
                const int32 py = prevY * IMAGE_SCALING;

                const int32 d = (px - x) * (px - x) + (py - y) * (py - y);
                if (d < minDistance)
                {
                    color = pointsColors[j] << 3;
                    minDistance = d;
                }
            }

            finalImage[IMAGE_WIDTH * IMAGE_SCALING * y + x] = color;
        }
    }

    RECT rect;
    GetClientRect(GetDesktopWindow(), &rect);
    HDC hDC = GetDC(CreateWindowExA(0, (LPSTR)0xC019, 0, WS_VISIBLE | WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0, rect.right, rect.bottom, 0, 0, 0, 0));

    SetStretchBltMode(hDC, STRETCH_HALFTONE);

    StretchDIBits(hDC,
                  0, 0, rect.right, rect.bottom,      // target rect
                  0, 0, IMAGE_SCALING * IMAGE_WIDTH, IMAGE_SCALING * IMAGE_HEIGHT,  // source rect
                  finalImage, &bmi, DIB_RGB_COLORS, SRCCOPY);

    do { } while (!GetAsyncKeyState(VK_ESCAPE));

    return 0;
}
