#include "compressor.h"
#include <iostream>
#include <iomanip>

//#define DECOMPRESS_EXISTING
#define COMPARE_WITH_ORIGINAL

int main()
{
    Image originalImage;
    if (!originalImage.Load("../Original/girl_256.bmp"))
    {
        std::cout << "Failed to load source image" << std::endl;
        return 1;
    }

    std::cout << "Decomposing into YCbCr components..." << std::endl;
    Image yImage, cbImage, crImage;
    if (!originalImage.ToYCbCr(yImage, cbImage, crImage))
    {
        std::cout << "Failed to decompose image into YCbCr components" << std::endl;
        return 2;
    }

    CompressorSettings lumaSettings;
    lumaSettings.minRangeSize = 8;
    lumaSettings.maxRangeSize = 64;
    lumaSettings.mseMultiplier = 50.0f;

    Compressor compressorY(lumaSettings);

    std::cout << "Compressing Y channel..." << std::endl;
    if (!compressorY.Compress(yImage))
    {
        std::cout << "Failed to compress Y image" << std::endl;
        return 1;
    }
    compressorY.Save("../Encoded/encodedY.dat");
    compressorY.SaveAsSourceFile("luma", "../Demo/luma.cpp");


#ifdef COMPARE_WITH_ORIGINAL
    std::cout << "Decompressing Y..." << std::endl;
    Image decompressedY;
    if (!compressorY.Decompress(decompressedY))
    {
        std::cout << "Failed to decompress Y image" << std::endl;
        return 1;
    }
    decompressedY.Save("../Encoded/fractal_decompressed_y.bmp");

    /*
    std::cout << std::endl << "=== COMPRESSED IMAGE STATS ===" << std::endl;
    ImageDifference diff = Image::Compare(originalImage, decompressed);
    std::cout << "MSE      = " << std::setw(8) << std::setprecision(4) << diff.averageError << std::endl;
    std::cout << "PSNR     = " << std::setw(8) << std::setprecision(4) << diff.psnr << " dB" << std::endl;
    std::cout << "maxError = " << std::setw(8) << std::setprecision(4) << diff.maxError << std::endl;
    */
#endif

    system("pause");
    return 0;
}
