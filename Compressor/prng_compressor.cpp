#include "image.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <string>
#include <thread>
#include <condition_variable>
#include <fstream>

#include <Windows.h>

//////////////////////////////////////////////////////////////////////////

#define BITS_PER_PIXEL 5
#define COLOR_MASK ((1 << BITS_PER_PIXEL) -1)

uint32 xorshift32(uint32& seed)
{
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
}

struct Point
{
    uint16 x;
    uint16 y;

    uint8 r;
    uint8 g;
    uint8 b;
};

using Points = std::vector<Point>;

struct Chromosome
{
    Points points;
    float error;
};

using Population = std::vector<Chromosome>;

// patch ID for each image pixel
using VoronoiMap = std::vector<uint16>;

//////////////////////////////////////////////////////////////////////////
// UTILITIES
//////////////////////////////////////////////////////////////////////////

void rot(int n, int& x, int& y, int rx, int ry)
{
    if (ry == 0)
    {
        if (rx == 1)
        {
            x = n - 1 - x;
            y = n - 1 - y;
        }

        int t = x;
        x = y;
        y = t;
    }
}

int MapHilbert(int n, int x, int y)
{
    int rx, ry, s, d = 0;
    for (s = n / 2; s > 0; s /= 2)
    {
        rx = (x & s) > 0;
        ry = (y & s) > 0;
        d += s * s * ((3 * rx) ^ ry);
        rot(s, x, y, rx, ry);
    }
    return d;
}

uint32 log2(uint32 val)
{
    if (val <= 1)
        return 0;

    uint32 ret = 0;
    while (val > 1)
    {
        val >>= 1;
        ret++;
    }

    return ret;
}

uint16 InterleaveBits(const uint8 x, const uint8 y)
{
    uint32 z = 0;
    for (int i = 0; i < sizeof(uint8) * CHAR_BIT; i++)
    {
        z |= (x & 1U << i) << i | (y & 1U << i) << (i + 1);
    }
    return z;
}

__forceinline uint8 DecodeColor(uint8 x)
{
    return x << (8 - BITS_PER_PIXEL);
}

__forceinline uint8 EnodeColor(uint8 x)
{
    return (x >> (8 - BITS_PER_PIXEL));
}

//////////////////////////////////////////////////////////////////////////

//static const uint32 gNumChildrenPerParent = 1;
static const uint32 gPopulationSize = 8;
static const uint32 gNumPoints = 1500;

//////////////////////////////////////////////////////////////////////////

// calculate Voronoi map
__declspec(noinline)
void CalculatePatches(const Chromosome& chromosome, const Image& image, VoronoiMap& outPatchIndices)
{
    const uint32 numGridCells = 32;
    const int32 size = (int32)image.GetSize();
    const int32 pixelsPerGridCell = size / numGridCells;

    outPatchIndices.clear();
    outPatchIndices.reserve(size * size);

    using GridCell = std::vector<uint16>;
    GridCell grid[numGridCells][numGridCells];

    // insert points to the grid
    for (size_t i = 0; i < chromosome.points.size(); ++i)
    {
        const Point& p = chromosome.points[i];
        const int32 gridCellX = p.x / pixelsPerGridCell;
        const int32 gridCellY = p.y / pixelsPerGridCell;
        assert(gridCellX < numGridCells);
        assert(gridCellY < numGridCells);
        grid[gridCellX][gridCellY].push_back(i);
    }

    for (int32 iy = 0; iy < size; iy++)
    {
        for (int32 ix = 0; ix < size; ix++)
        {
            size_t nearestPoint = 0;
            int32 minDistance = INT32_MAX;

            // iterate grid cell
            const auto iterateCell = [&](const GridCell& cell)
            {
                for (const uint16 i : cell)
                {
                    const Point& p = chromosome.points[i];
                    const int32 d = (p.x - ix) * (p.x - ix) + (p.y - iy) * (p.y - iy);
                    if (d < minDistance)
                    {
                        minDistance = d;
                        nearestPoint = i;
                    }
                }
            };

            int x0 = ix / pixelsPerGridCell;
            int y0 = iy / pixelsPerGridCell;
            int x = 0;
            int y = 0;
            int d = 0; // current direction; 0=RIGHT, 1=DOWN, 2=LEFT, 3=UP
            int c = 0; // counter
            int s = 1; // chain size

            while (c < numGridCells * numGridCells)
            {
                for (int j = 0; j < 2; j++)
                {
                    for (int i = 0; i < s; i++)
                    {
                        int cellX = x0 + x;
                        int cellY = y0 + y;

                        if (cellX >= 0 && cellY >= 0 && cellX < numGridCells && cellY < numGridCells)
                        {
                            iterateCell(grid[cellX][cellY]);
                            c++;
                        }
                        switch (d)
                        {
                        case 0: y++; break;
                        case 1: x++; break;
                        case 2: y--; break;
                        case 3: x--; break;
                        }
                    }
                    d = (d + 1) % 4;
                }
                s++;

                if (x > 0)
                {
                    int range = x - 1;
                    if (range * range * pixelsPerGridCell * pixelsPerGridCell > minDistance)
                    {
                        break;
                    }
                }

            }

            outPatchIndices.push_back(nearestPoint);
        }
    }

    /*
    for (int32 y = 0; y < size; y++)
    {
        for (int32 x = 0; x < size; x++)
        {
            size_t nearestPoint = 0;
            int32 minDistance = INT32_MAX;

            for (size_t i = 0; i < chromosome.points.size(); ++i)
            {
                const Point& p = chromosome.points[i];
                const int32 d = (p.x - x) * (p.x - x) + (p.y - y) * (p.y - y);
                if (d < minDistance)
                {
                    minDistance = d;
                    nearestPoint = i;
                }
            }

            outPatchIndices.push_back(nearestPoint);
        }
    }
    */
}

// calculate optimal color for each patch
void AdjustColors(const Image& refImage, const VoronoiMap& voronoiMap, Chromosome& chromosome)
{
    // summed color for each patch
    std::vector<uint32> averagePatchColors(3 * chromosome.points.size(), 0);

    // number of pixels in each patch
    std::vector<uint32> patchSizes(chromosome.points.size(), 0);

    int32 size = (int32)refImage.GetSize();
    for (int32 y = 0; y < size; y++)
    {
        for (int32 x = 0; x < size; x++)
        {
            const uint16 pointID = voronoiMap[y * size + x];

            uint8 r, g, b;
            refImage.Sample3(x, y, r, g, b);

            averagePatchColors[3 * pointID] += r;
            averagePatchColors[3 * pointID + 1] += g;
            averagePatchColors[3 * pointID + 2] += b;
            patchSizes[pointID]++;
        }
    }

    // update points colors
    for (size_t i = 0; i < chromosome.points.size(); ++i)
    {
        const uint32 numPixels = patchSizes[i];
        if (numPixels > 0)
        {
            uint32 r = averagePatchColors[3 * i] / numPixels;
            uint32 g = averagePatchColors[3 * i + 1] / numPixels;
            uint32 b = averagePatchColors[3 * i + 2] / numPixels;

            chromosome.points[i].r = EnodeColor(r);
            chromosome.points[i].g = EnodeColor(g);
            chromosome.points[i].b = EnodeColor(b);
        }
    }
}

// convert chromosome to an image
void DrawImage(Image& image, const VoronoiMap& voronoiMap, const Chromosome& chromosome)
{
    int32 size = (int32)image.GetSize();
    for (int32 y = 0; y < size; y++)
    {
        for (int32 x = 0; x < size; x++)
        {
            const uint16 pointID = voronoiMap[y * size + x];
            const uint8 r = DecodeColor(chromosome.points[pointID].r);
            const uint8 g = DecodeColor(chromosome.points[pointID].g);
            const uint8 b = DecodeColor(chromosome.points[pointID].b);
            image.WritePixel3(x, y, r, g, b);
        }
    }
}

//////////////////////////////////////////////////////////////////////////

void SortChromosome(Chromosome& chromosome)
{
    std::sort(chromosome.points.begin(), chromosome.points.end(), [](const Point& a, const Point& b)
    {
        uint32 pa = MapHilbert(256, a.x, a.y);
        uint32 pb = MapHilbert(256, b.x, b.y);
        return pa > pb;
    });
}


Point GeneratePoint(const Image& image, uint32& seed)
{
    Point point;
    point.x = (uint16)(xorshift32(seed) & (image.GetSize() - 1));
    point.y = (uint16)(xorshift32(seed) & (image.GetSize() - 1));
    point.r = (uint8)(xorshift32(seed));
    point.g = (uint8)(xorshift32(seed));
    point.b = (uint8)(xorshift32(seed));
    return point;
}

void GenerateChromosome(uint32 numPoints, uint32& seed, const Image& image, Chromosome& outChromosome)
{
    outChromosome.points.clear();
    outChromosome.error = FLT_MAX;

    for (uint32 i = 0; i < numPoints; ++i)
    {
        outChromosome.points.push_back(GeneratePoint(image, seed));
    }

    SortChromosome(outChromosome);
}

void GenerateInitialPopulation(Population& population, const Image& image, uint32 numChromosomes, uint32 numPoints, uint32& seed)
{
    if (population.size() != numChromosomes)
    {
        population.resize(numChromosomes);
    }

    for (Chromosome& c : population)
    {
        c.error = FLT_MAX;

        if (c.points.size() > numPoints)
        {
            c.points.erase(c.points.begin() + numPoints, c.points.end());
        }
        else if (c.points.size() < numPoints)
        {
            // generate missing points
            for (uint32 i = c.points.size(); i < numPoints; ++i)
            {
                c.points.push_back(GeneratePoint(image, seed));
            }
        }

        // fix points
        for (Point& p : c.points)
        {
            p.x &= image.GetSizeMask();
            p.y &= image.GetSizeMask();
        }

        // TODO make sure there is no points duplicates

        VoronoiMap voronoiMap;
        CalculatePatches(c, image, voronoiMap);
        AdjustColors(image, voronoiMap, c);
    }
}

void Mutate(Chromosome& chromosome, const Image& image, uint32& seed)
{
    uint32 index = xorshift32(seed) % (uint32)(chromosome.points.size() - 1);
    Point& point = chromosome.points[index];

    for (;;)
    {
        uint32 component = xorshift32(seed);
        int32 v = xorshift32(seed);

        if (v & 0x8000000)
        {
            // fine adjustment
            v &= 0x7;
            v -= 4;
            if (v >= 0) v++;
        }

        // TODO higher probability for lower bits
        if (component % 2 == 0)
        {
            point.x += v;
            point.x &= image.GetSizeMask();
        }
        else
        {
            point.y += v;
            point.y &= image.GetSizeMask();
        }

        // make sure that new point do not overlaps with another
        bool collision = false;
        for (uint32 i = 0; i < chromosome.points.size(); ++i)
        {
            if (index != i)
            {
                Point& otherPoint = chromosome.points[i];
                if (point.x == otherPoint.x && point.y == otherPoint.y)
                {
                    collision = false;
                    break;
                }
            }
        }

        if (!collision)
            break;
    }
}

void Crossover(Chromosome& outChromosome, const Chromosome& parentA, const Chromosome& parentB)
{
    assert(parentA.points.size() == parentB.points.size(), "Parents chromosomes lengths must match");
}

void GenerateChild(const Chromosome& parent, Chromosome& child, const Image& image, uint32& seed)
{
    child = parent;

    const uint32 u = xorshift32(seed);
    const uint32 numMutations = 32 - log2(u);
    for (uint32 i = 0; i < numMutations; ++i)
    {
        Mutate(child, image, seed);
    }

    SortChromosome(child);
}

static std::mutex g_minErrorMutex;
static float g_minError = FLT_MAX;

void ReportBestChromosome(const Chromosome& chromosome, Image& image)
{
    for (const Point& p : chromosome.points)
    {
        image.WritePixel3(p.x, p.y, 0, 0, 255);
    }

    image.Save("../Encoded/encoded.bmp");

    FILE* binaryFile = fopen("../Encoded/points.dat", "wb");
    if (binaryFile != nullptr)
    {
        fwrite(chromosome.points.data(), sizeof(Point), chromosome.points.size(), binaryFile);
        fclose(binaryFile);
    }
}

bool ReadPopulation(Population& outPopulation)
{
    uint32 size;
    uint32 numPoints;

    FILE* binaryFile = fopen("../Encoded/population.dat", "rb");
    if (!binaryFile)
        return false;

    fread(&size, sizeof(size), 1, binaryFile);
    fread(&numPoints, sizeof(size), 1, binaryFile);

    outPopulation.resize(size);
    for (uint32 i = 0; i < size; ++i)
    {
        outPopulation[i].points.resize(numPoints);
        fread(outPopulation[i].points.data(), sizeof(Point), numPoints, binaryFile);
    }

    fclose(binaryFile);
    return true;
}

bool SavePopulation(const Population& population)
{
    uint32 size = population.size();
    uint32 numPoints = population.front().points.size();

    FILE* binaryFile = fopen("../Encoded/population.dat", "wb");
    if (!binaryFile)
        return false;

    fwrite(&size, sizeof(size), 1, binaryFile);
    fwrite(&numPoints, sizeof(size), 1, binaryFile);

    for (uint32 i = 0; i < size; ++i)
    {
        fwrite(population[i].points.data(), sizeof(Point), numPoints, binaryFile);
    }

    fclose(binaryFile);
    return true;
}

void RemoveDuplicates(Population& population)
{
    std::sort(population.begin(), population.end(), [](const Chromosome& a, const Chromosome& b)
    {
        assert(a.points.size() == b.points.size());
        return memcmp(a.points.data(), b.points.data(), a.points.size() * sizeof(Point)) < 0;
    });

    const auto iter = std::unique(population.begin(), population.end(), [](const Chromosome& a, const Chromosome& b)
    {
        return memcmp(a.points.data(), b.points.data(), a.points.size() * sizeof(Point)) == 0;
    });

    population.erase(iter, population.end());
}

bool SavePointsAsSourceFile(const std::string& path, const std::vector<Point>& points, const Image& image)
{
    std::ofstream file(path);
    if (!file.good())
    {
        std::cout << "Failed to open target encoded file '" << path << "': " << stderr << std::endl;
        return false;
    }

    uint8 prevX = 0, prevY = 0;

    file << "#pragma once\n\n";
    file << "#define NUM_POINTS " << points.size() << "\n";
    file << "#define IMAGE_WIDTH " << image.GetSize() << "\n";
    file << "#define IMAGE_HEIGHT " << image.GetSize() << "\n\n";

    file << "static const uint8 pointsX[] =\n{\n";
    for (const Point& p : points)
    {
        // delta encoding
        uint8 diffX = (uint8)p.x - prevX;
        prevX = p.x;
        file << "\t" << (uint32)diffX << ",\n";
    }
    file << "};\n\n";

    file << "static const uint8 pointsY[] =\n{\n";
    for (const Point& p : points)
    {
        // delta encoding
        uint8 diffY = (uint8)p.y - prevY;
        prevY = p.y;
        file << "\t" << (uint32)diffY << ",\n";
    }
    file << "};\n\n";


    file << "static const uint32 pointsColors[] =\n{\n";
    for (const Point& p : points)
    {
        uint32 r = (uint32)p.r & COLOR_MASK;
        uint32 g = (uint32)p.g & COLOR_MASK;
        uint32 b = (uint32)p.b & COLOR_MASK;

        uint32 color = ((uint32)b << 16) | ((uint32)g << 8) | (uint32)r;
        file << "\t0x" << std::hex << color << ",\n";
    }
    file << "};\n\n";

    return true;
}

//////////////////////////////////////////////////////////////////////////

// multithreading hacks
using TaskFunc = std::function<void()>;
static std::condition_variable g_newTaskCV[gPopulationSize];
static std::condition_variable g_taskCompletedCV[gPopulationSize];
static std::mutex g_mutex[gPopulationSize];
static TaskFunc g_tasks[gPopulationSize];
static bool g_taskCompleted[gPopulationSize];
static uint32 g_seeds[gPopulationSize];

void ThreadCallback(uint32 index)
{
    TaskFunc& task = g_tasks[index];

    for (;;)
    {
        // wait for new task
        {
            std::unique_lock<std::mutex> lock(g_mutex[index]);
            while (!task)
            {
                g_newTaskCV[index].wait(lock);
            }
        }

        task();
        task = TaskFunc();

        // notify about completion
        {
            std::unique_lock<std::mutex> lock(g_mutex[index]);
            g_taskCompleted[index] = true;
            g_taskCompletedCV[index].notify_all();
        }
    }
}

void ProcessEpoch(Population& population, const Image& originalImage, Image tempImages[])
{
    Chromosome childChromosomes[gPopulationSize];

    for (uint32 i = 0; i < gPopulationSize; ++i)
    {
        g_taskCompleted[i] = false;
        const Chromosome& parent = population[i];

        auto taskFunc = [i, &childChromosomes, &parent, &originalImage, &tempImages]()
        {
            Chromosome& child = childChromosomes[i];
            GenerateChild(parent, child, originalImage, g_seeds[i]);

            std::vector<uint16> voronoiMap;
            CalculatePatches(child, originalImage, voronoiMap);
            AdjustColors(originalImage, voronoiMap, child);
            DrawImage(tempImages[i], voronoiMap, child);

            const ImageDifference diff = Image::Compare(originalImage, tempImages[i]);
            child.error = diff.averageError;

            // best solution found
            std::unique_lock<std::mutex> lock(g_minErrorMutex);
            if (child.error < g_minError)
            {
                g_minError = child.error;
                ReportBestChromosome(child, tempImages[i]);
            }
        };

        // start processing
        std::unique_lock<std::mutex> lock(g_mutex[i]);
        g_tasks[i] = taskFunc;
        g_newTaskCV[i].notify_all();
    }

    // wait for tasks completion
    for (uint32 i = 0; i < gPopulationSize; ++i)
    {
        std::unique_lock<std::mutex> lock(g_mutex[i]);
        while (!g_taskCompleted[i])
            g_taskCompletedCV[i].wait(lock);
    }

    // merge population
    for (uint32 i = 0; i < gPopulationSize; ++i)
    {
        population.push_back(std::move(childChromosomes[i]));
    }
}

int main()
{
    Image originalImage;
    if (!originalImage.Load("../Original/lena_256.bmp"))
    {
        std::cout << "Failed to load source image" << std::endl;
        return 1;
    }

    std::cout << "Source image size:     " << originalImage.GetSize() << std::endl;
    std::cout << "Source image channels: " << originalImage.GetChannelsNum() << std::endl;

    Image tempImages[gPopulationSize];
    for (Image& image : tempImages)
        image.Resize(originalImage.GetSize(), 3);

    // launch threads
    std::vector<std::thread> threads;
    for (uint32 i = 0; i < gPopulationSize; ++i)
    {
        threads.emplace_back(ThreadCallback, i);
        g_seeds[i] = 0x1234 + i;
    }

    Population population;
    ReadPopulation(population);
    GenerateInitialPopulation(population, originalImage, gPopulationSize, gNumPoints, g_seeds[0]);

    LARGE_INTEGER start, stop, freq;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);

    for (int epoch = 0; epoch < 1; epoch++)
    {
        ProcessEpoch(population, originalImage, tempImages);

        // remove duplicated solutions
        RemoveDuplicates(population);

        // sort by fitness
        std::sort(population.begin(), population.end(), [](const Chromosome& a, const Chromosome& b)
        {
            return a.error < b.error;
        });

        // discard worst solutions
        if (population.size() > gPopulationSize)
        {
            population.erase(population.begin() + gPopulationSize, population.end());
        }

        const int size = originalImage.GetSize() * originalImage.GetSize() * originalImage.GetChannelsNum();
        float psnr = 10.0f * log10f((float)size / population.front().error);

        // print current fitness
        if (epoch % 100 == 0)
        {
            QueryPerformanceCounter(&stop);
            float t = (float)(stop.QuadPart - start.QuadPart) / (float)freq.QuadPart;
            start = stop;
            std::cout << epoch << "\t" << std::setprecision(7) << psnr << std::endl;

            // SavePopulation(population);
            SavePointsAsSourceFile("../Demo/points.h", population.front().points, originalImage);
        }
    }

    system("pause");
    return 0;
}
