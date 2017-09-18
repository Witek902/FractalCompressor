#include "image.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <fstream>

#define IMAGE_SIZE 512
#define IMAGE_LEVELS 10

__forceinline void Haar(int32& a, int32& b, int32& c, int32& d)
{
    const int32 at = a + b + c + d;
    const int32 bt = a - b + c - d;
    const int32 ct = a + b - c - d;
    const int32 dt = a - b - c + d;

    a = at;
    b = bt;
    c = ct;
    d = dt;
}

static const int32 QUANTIZATION_FACTOR = 256 * 1024;

void WaveletCompress(const int32* input, int32* output, uint32 level)
{
    memcpy(output, input, IMAGE_SIZE * IMAGE_SIZE * sizeof(int32));

    const int32 quantization = QUANTIZATION_FACTOR >> (level * 3 / 2);
    const uint32 size = 1 << level;
    const uint32 halfSize = size / 2;

    for (uint32 j = 0; j < size / 2; ++j)
    {
        for (uint32 k = 0; k < size / 2; ++k)
        {
            int32 a = input[(2 * j) * IMAGE_SIZE + (2 * k)];
            int32 b = input[(2 * j) * IMAGE_SIZE + (2 * k + 1)];
            int32 c = input[(2 * j + 1) * IMAGE_SIZE + (2 * k)];
            int32 d = input[(2 * j + 1) * IMAGE_SIZE + (2 * k + 1)];

            Haar(a, b, c, d);

            b /= quantization;
            c /= quantization;
            d /= quantization;

            output[j * IMAGE_SIZE + k] = a;
            output[j * IMAGE_SIZE + k + halfSize] = b;
            output[(j + halfSize) * IMAGE_SIZE + k] = c;
            output[(j + halfSize) * IMAGE_SIZE + k + halfSize] = d;
        }
    }
}

void WaveletDecompress(const int32* input, int32* output, uint32 level)
{
    const uint32 size = 1 << level;
    const uint32 halfSize = size / 2;
    const int32 quantization = QUANTIZATION_FACTOR >> (level * 3 / 2);

    for (uint32 j = 0; j < size / 2; ++j)
    {
        for (uint32 k = 0; k < size / 2; ++k)
        {
            int32 a = input[j * IMAGE_SIZE + k];
            int32 b = input[j * IMAGE_SIZE + k + halfSize];
            int32 c = input[(j + halfSize) * IMAGE_SIZE + k];
            int32 d = input[(j + halfSize) * IMAGE_SIZE + k + halfSize];

            b *= quantization;
            c *= quantization;
            d *= quantization;

            Haar(a, b, c, d);

            a /= 4;
            b /= 4;
            c /= 4;
            d /= 4;

            output[(2 * j) * IMAGE_SIZE + (2 * k)] = a;
            output[(2 * j) * IMAGE_SIZE + (2 * k + 1)] = b;
            output[(2 * j + 1) * IMAGE_SIZE + (2 * k)] = c;
            output[(2 * j + 1) * IMAGE_SIZE + (2 * k + 1)] = d;
        }
    }
}

void ImageToInt32(const Image& input, int32* output)
{
    for (uint32 y = 0; y < IMAGE_SIZE; ++y)
    {
        for (uint32 x = 0; x < IMAGE_SIZE; ++x)
        {
            output[y * IMAGE_SIZE + x] = input.Sample(x, y);
        }
    }
}

void ImageFromInt32(const int32* input, Image& output)
{
    for (uint32 y = 0; y < IMAGE_SIZE; ++y)
    {
        for (uint32 x = 0; x < IMAGE_SIZE; ++x)
        {
            int32 color = input[y * IMAGE_SIZE + x];
            if (color < 0) color = 0;
            if (color > 255) color = 255;
            output.WritePixel(x, y, (uint8)color);
        }
    }
}

static int32 bufferA[IMAGE_SIZE * IMAGE_SIZE];
static int32 bufferB[IMAGE_SIZE * IMAGE_SIZE];
static int32* buffers[] = { bufferA, bufferB };

void Compress()
{
    for (uint32 i = IMAGE_LEVELS; i-- > 0;)
    {
        const int32* input = buffers[(i & 1) ^ 1];
        int32* output = buffers[(i & 1)];
        WaveletCompress(input, output, i);

        const uint32 size = 1 << i;
        const uint32 halfSize = size / 2;

        int numZeros = 0;
        int numCoeffs = 0;
        int coeffSum = 0;
        int maxCoeff = INT_MIN;
        int minCoeff = INT_MAX;
        for (uint32 y = 0; y < size; ++y)
        {
            for (uint32 x = 0; x < size; ++x)
            {
                if (x >= halfSize || y >= halfSize)
                {
                    const int32 coeff = output[y * IMAGE_SIZE + x];
                    minCoeff = std::min<int>(minCoeff, coeff);
                    maxCoeff = std::max<int>(maxCoeff, coeff);
                    coeffSum += coeff;
                    numZeros += (coeff == 0) ? 1 : 0;
                    numCoeffs++;
                }
            }
        }

        std::cout << "Level " << i <<
            ": min = " << minCoeff <<
            ", max = " << maxCoeff <<
            ", avg = " << (float)coeffSum / (float)numCoeffs <<
            ", zero = " << 100.0f * (float)numZeros / (float)numCoeffs << "%" << std::endl;
    }

    for (int i = 0; i < IMAGE_SIZE * IMAGE_SIZE; ++i)
    {
        if (bufferA[i] > 63) bufferA[i] = 63;
        if (bufferA[i] < -64) bufferA[i] = -64;
    }
}

void Decompress()
{
    for (uint32 i = 0; i < IMAGE_LEVELS; ++i)
    {
        int32* output = buffers[(i & 1) ^ 1];
        const int32* input = buffers[i & 1];
        WaveletDecompress(input, output, i);
    }
}

bool SaveAsSourceFile(const char* path, const int32* data)
{
    std::ofstream file(path);
    if (!file.good())
    {
        std::cout << "Failed to open target encoded file '" << path << "': " << stderr << std::endl;
        return false;
    }

    std::vector<uint8> rawData;

    // RLE
    {
        int numZeros = 0;

        const auto flushZeros = [&rawData, &numZeros]()
        {
            if (numZeros > 0)
            {
                rawData.push_back(numZeros | 0x80);
                numZeros = 0;
            }
        };

        for (int i = 0; i < IMAGE_SIZE * IMAGE_SIZE; ++i)
        {
            int32 value = data[i];
            if (value != 0)
            {
                flushZeros();

                if (value > 31) value = 31;
                if (value < -31) value = -31;
                value += 64;

                rawData.push_back(value);
            }
            else
            {
                if (numZeros >= 127)
                {
                    flushZeros();
                }

                numZeros++;
            }
        }

        flushZeros();
    }

    {
        FILE* binaryFile = fopen("../Encoded/wavelet.dat", "wb");
        fwrite(rawData.data(), 1, rawData.size(), binaryFile);
        fclose(binaryFile);
    }

    file << "#include \"wavelet.h\"\n\n";
    {
        file << "const uint8 waveletData[] =\n{\n";
        for (int i = 0; i < rawData.size(); ++i)
        {
            file << std::dec << "    " << (uint32)(rawData[i]) << ",\n";
        }
        file << "};\n\n";
    }

    return true;
}

int main()
{
    Image originalImage;
    if (!originalImage.Load("../Original/lena_512.bmp"))
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

    assert(yImage.GetSize() == IMAGE_SIZE);

    // convert uint8 -> int32
    ImageToInt32(yImage, bufferA);

    Compress();

    SaveAsSourceFile("../Demo/wavelet.cpp", bufferA);

    Decompress();

    // convert int32 -> uint8
    Image encodedImage;
    encodedImage.Resize(IMAGE_SIZE, 1);
    ImageFromInt32(bufferA, encodedImage);
    encodedImage.Save("../Encoded/wavelet.bmp");

    const ImageDifference diff = Image::Compare(yImage, encodedImage);
    std::cout << "PSNR = " << diff.psnr << std::endl;

    system("pause");
    return 0;
}