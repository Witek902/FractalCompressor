#include "compressor.h"
#include <iostream>
#include <iomanip>


// TODO command line options:
// 1. compress (and compare)
// 2. decompress
// 3. compare two images



// #define DECOMPRESS_EXISTING

int main()
{
    Image originalImage;
    if (!originalImage.Load("../Original/lena_256_gray.bmp"))
    {
        std::cout << "Failed to load source image" << std::endl;
        return 1;
    }

    Compressor compressor;
    
#ifdef DECOMPRESS_EXISTING
    if (!compressor.Load("../Encoded/encoded.dat"))
    {
        std::cout << "Failed to load compressed image" << std::endl;
        return 1;
    }
#else
    if (!compressor.Compress(originalImage))
    {
        std::cout << "Failed to compress image" << std::endl;
        return 1;
    }
#endif // DECOMPRESS_EXISTING

    const float bitsPerPixel = compressor.GetCompressedSize() / (float)(originalImage.GetSize() * originalImage.GetSize());
    std::cout << "Compressed size: " << compressor.GetCompressedSize() / 8 << " B (" << 
        std::setw(8) << std::setprecision(4) << bitsPerPixel << " bpp)" << std::endl;
    compressor.Save("../Encoded/encoded.dat");

    Image decompressed;
    if (!compressor.Decompress(decompressed))
    {
        std::cout << "Failed to decompress encoded image" << std::endl;
        return 1;
    }

    std::string fileName = "../Encoded/fractal.bmp";
    decompressed.Save(fileName);

    std::cout << std::endl << "=== COMPRESSED IMAGE STATS ===" << std::endl;
    ImageDifference diff = Image::Compare(originalImage, decompressed);
    std::cout << "MSE      = " << std::setw(8) << std::setprecision(4) << diff.averageError << std::endl;
    std::cout << "PSNR     = " << std::setw(8) << std::setprecision(4) << diff.psnr << " dB" << std::endl;
    std::cout << "maxError = " << std::setw(8) << std::setprecision(4) << diff.maxError << std::endl;

    system("pause");
    return 0;
}