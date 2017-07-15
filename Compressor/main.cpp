#include "compressor.h"
#include <iostream>
#include <iomanip>


// TODO command line options:
// 1. compress (and compare)
// 2. decompress
// 3. compare two images


//#define DECOMPRESS_EXISTING
#define COMPARE_WITH_ORIGINAL

int main()
{
    Image originalImage;
    if (!originalImage.Load("../Original/lena_256.bmp"))
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

    std::cout << "Downsampling chroma components..." << std::endl;
    cbImage = cbImage.Downsample().Downsample();
    crImage = crImage.Downsample().Downsample();

    Compressor compressorY, compressorCb, compressorCr;
    
#ifdef DECOMPRESS_EXISTING
    if (!compressor.Load("../Encoded/encoded.dat"))
    {
        std::cout << "Failed to load compressed image" << std::endl;
        return 1;
    }
#else
    std::cout << "Compressing Y channel..." << std::endl;
    if (!compressorY.Compress(yImage))
    {
        std::cout << "Failed to compress Y image" << std::endl;
        return 1;
    }
    compressorY.Save("../Encoded/encodedY.dat");

    std::cout << "Compressing Cb channel..." << std::endl;
    if (!compressorCb.Compress(cbImage))
    {
        std::cout << "Failed to compress Cb image" << std::endl;
        return 1;
    }
    compressorCb.Save("../Encoded/encodedCb.dat");

    std::cout << "Compressing Cr channel..." << std::endl;
    if (!compressorCr.Compress(crImage))
    {
        std::cout << "Failed to compress Cr image" << std::endl;
        return 1;
    }
    compressorCr.Save("../Encoded/encodedCr.dat");
#endif // DECOMPRESS_EXISTING

    
#ifdef COMPARE_WITH_ORIGINAL
    std::cout << "Decompressing Y..." << std::endl;
    Image decompressedY;
    if (!compressorY.Decompress(decompressedY))
    {
        std::cout << "Failed to decompress Y image" << std::endl;
        return 1;
    }

    std::cout << "Decompressing Cb..." << std::endl;
    Image decompressedCb;
    if (!compressorCb.Decompress(decompressedCb))
    {
        std::cout << "Failed to decompress Cb image" << std::endl;
        return 1;
    }

    std::cout << "Decompressing Cr..." << std::endl;
    Image decompressedCr;
    if (!compressorCr.Decompress(decompressedCr))
    {
        std::cout << "Failed to decompress Cr image" << std::endl;
        return 1;
    }

    std::cout << "Upsampling chroma components..." << std::endl;
    decompressedCb = decompressedCb.Upsample().Upsample();
    decompressedCr = decompressedCr.Upsample().Upsample();

    std::cout << "Merging into RGB components..." << std::endl;
    Image decompressed;
    if (!decompressed.FromYCbCr(decompressedY, decompressedCb, decompressedCr))
    {
        return 3;
    }

    std::string fileName = "../Encoded/fractal_decompressed.bmp";
    decompressed.Save(fileName);

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
