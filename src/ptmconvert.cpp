/* 
 * ptmconvert by Tobias Alexander Franke (tob@cyberhead.de) 2012 
 * For copyright and license see LICENSE
 */

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <map>
#include <stdexcept>

#include "stb/stb_image.h"

// Details:
// http://www.hpl.hp.com/research/ptm/downloads/PtmFormat12.pdf

enum PTMFormat
{
    PTM_FORMAT_RGB,
    PTM_FORMAT_LUM,
    PTM_FORMAT_LRGB,
    PTM_FORMAT_JPEG_RGB,
    PTM_FORMAT_JPEG_LRGB,
    PTM_FORMAT_JPEGLS_RGB,
    PTM_FORMAT_JPEGLS_LRGB
};

enum PTMTransform
{
    NOTHING,
    PLANE_INVERSION,
    MOTION_COMPENSATION
};

struct CompressionInfo
{
    unsigned int compressionParameter;
    std::vector<PTMTransform> transforms;
    std::vector<int> motionVectors;
    std::vector<int> order;
    std::vector<int> referencePlanes;
    std::vector<unsigned int> compressedSize;
    std::vector<unsigned int> sideInformation;
};

struct PTMHeader12
{
    PTMFormat format;
    size_t width;
    size_t height;
    float scale[6];
    int bias[6];

    CompressionInfo ci;

    std::vector<unsigned char> coefficients;
};

/**
 * Returns true if the PTM has been compressed with JPEG
 */
bool is_compressed(PTMHeader12* ptm)
{
    return ptm->format == PTM_FORMAT_JPEG_RGB ||
           ptm->format == PTM_FORMAT_JPEG_LRGB ||
           ptm->format == PTM_FORMAT_JPEGLS_RGB ||
           ptm->format == PTM_FORMAT_JPEGLS_LRGB;
}

/**
 * Returns true if the PTM is LRGB, i.e. has a block of RGB data
 */
bool is_lrgb(PTMHeader12* ptm)
{
    return ptm->format == PTM_FORMAT_JPEG_LRGB ||
           ptm->format == PTM_FORMAT_LRGB ||
           ptm->format == PTM_FORMAT_JPEGLS_LRGB;
}

/**
 * Returns the number of Entries Per Pixel (RGB + coefficients or just coefficients)
 */
size_t get_epp(PTMHeader12* ptm)
{
    if (is_lrgb(ptm))
        return 9;
    else
        return 18;
}

/**
 * Helper function to initialize the CompressionInfo field in a PTM
 */
void init_ci(PTMHeader12* ptm)
{
    size_t epp = get_epp(ptm);
    ptm->ci.transforms.resize(epp);
    ptm->ci.motionVectors.resize(epp*2);
    ptm->ci.order.resize(epp);
    ptm->ci.referencePlanes.resize(epp);
    ptm->ci.compressedSize.resize(epp);
    ptm->ci.sideInformation.resize(epp);
}

/**
 * Read a PTM file into a structure
 *
 * This is the main function to read a PTM file. The field ptm->coefficients is filled with
 * whatever data was read from the PTM (after decompression, if necessary). The coefficients
 * field therefore may contain either three blocks (high order coefficients, low order coefficients
 * and rgb data) in case of LRGB PTMs, or raw RGB coefficients for each pixel in one big chunk.
 *
 * Currently, only LRGB PTMs are supported.
 */
void ptm_read(const char* file, PTMHeader12* ptm)
{
    std::ifstream stream(file, std::ios::binary);

    if (!stream.good())
        throw std::runtime_error("Can't open file");

    std::string version;
    stream >> version;

    if (version != "PTM_1.2")
        throw std::runtime_error("Wrong version");

    std::string format;
    stream >> format;

    if (format == "PTM_FORMAT_LRGB")
        ptm->format = PTM_FORMAT_LRGB;
    else if (format == "PTM_FORMAT_JPEG_LRGB")
        ptm->format = PTM_FORMAT_JPEG_LRGB;
    else
        throw std::runtime_error("Unknown format: " + format);

    stream >> ptm->width;
    stream >> ptm->height;

    for (size_t i = 0; i < 6; ++i)
        stream >> ptm->scale[i];

    for (size_t i = 0; i < 6; ++i)
        stream >> ptm->bias[i];

    size_t epp = get_epp(ptm);

    if (is_compressed(ptm))
    {
        init_ci(ptm);

        stream >> ptm->ci.compressionParameter;
        
        // TODO: enum
        for (size_t i = 0; i < epp; ++i)
        {
            int v;
            stream >> v;
            switch(v)
            {
            default:
            case 0:
                ptm->ci.transforms[i] = NOTHING;
                break;
            case 1:
                ptm->ci.transforms[i] = PLANE_INVERSION;
                break;
            case 2:
                ptm->ci.transforms[i] = MOTION_COMPENSATION;
                break;
            }
        }
        
        for (size_t i = 0; i < epp * 2; ++i) 
            stream >> ptm->ci.motionVectors[i];

        for (size_t i = 0; i < epp; ++i)
            stream >> ptm->ci.order[i];

        for (size_t i = 0; i < epp; ++i)
            stream >> ptm->ci.referencePlanes[i];

        for (size_t i = 0; i < epp; ++i)
            stream >> ptm->ci.compressedSize[i];

        for (size_t i = 0; i < epp; ++i)
            stream >> ptm->ci.sideInformation[i];
    }
    
    // search for newline
    char temp;
    do { stream.read(&temp, 1); } while (temp != '\n');

    ptm->coefficients.clear();

    size_t size = ptm->width * ptm->height * epp;
    ptm->coefficients.resize(size);

    if (ptm->format == PTM_FORMAT_LRGB)
    {
        stream.read(reinterpret_cast<char*>(&ptm->coefficients[0]), size);
    } 
    else if (ptm->format == PTM_FORMAT_JPEG_LRGB)
    {
        std::vector<unsigned char*> planes(epp);
        std::vector< std::vector<unsigned char> > side_info(epp);

        std::map<size_t, size_t> order;

        // first pass: extract all planes
        for (size_t p = 0; p < epp; ++p)
        {
            int w = ptm->width;
            int h = ptm->height;
            int comp = 1;

            // read jpeg buffer
            size_t bufs = ptm->ci.compressedSize[p];
            std::vector<char> jpegbuf(bufs);        
            stream.read(&jpegbuf[0], bufs);

            size_t sides = ptm->ci.sideInformation[p];
            if (sides > 0) 
            {
                side_info[p].resize(sides);
                stream.read(reinterpret_cast<char*>(&side_info[p][0]), sides);
            }

            // convert to char values
            planes[p] = stbi_load_from_memory(reinterpret_cast<unsigned char*>(&jpegbuf[0]), bufs, &w, &h, &comp, 1);
            
            // check errors
            if (stbi_failure_reason())
            {
                std::stringstream ss;
                ss << "(stb) " << stbi_failure_reason() << std::endl;
                throw std::runtime_error(ss.str().c_str());
            }

            if (comp != 1)
                throw std::runtime_error("Too many components in LRGB image");

            if (w != ptm->width || h != ptm->height)
                throw std::runtime_error("Incompatible image size found");

            order[ptm->ci.order[p]] = p;
        }

        // second pass: apply predicition and transformation
        for (size_t n = 0; n < epp; ++n)
        {
            // query actual plane number according to order map
            size_t i = order[n];
            size_t j = ptm->ci.referencePlanes[i];

            unsigned char* iPlane = planes[i];

            size_t numPixels = ptm->width * ptm->height;

            // prediction if plane index j is not -1
            if (j != -1)
            {
                unsigned char* jPlane = planes[j];
                
                for (size_t x = 0; x < numPixels; ++x)
                {
                    unsigned char jpx = jPlane[x];

                    // apply transformation
                    // TODO: add motion vector transformation!
                    if (ptm->ci.transforms[i] == PLANE_INVERSION)
                        jpx = 255 - jpx;

                    iPlane[x] = (jpx + iPlane[x] - 128)%255;
                }
            }

            // apply correction from sideinformation
            if (side_info[i].size() > 0)
            {
                size_t x = 0;
                while(x < side_info[i].size())
                {
                    unsigned char p3 = side_info[i][x++];
                    unsigned char p2 = side_info[i][x++];
                    unsigned char p1 = side_info[i][x++];
                    unsigned char p0 = side_info[i][x++];
                    unsigned char v  = side_info[i][x++];

                    int index = p3 << 24 | p2 << 16 | p1 << 8 | p0;
                    
                    int w = ptm->width;
                    int h = ptm->height;

                    int width = index % w;
                    int height = index / w;
                    int h2 = h - height - 1;
                    int index2 = h2 * w + width;
                    
                    iPlane[index2] = v;
                }
            }
        }

        size_t numPixels = ptm->width * ptm->height;

        for (size_t y = 0; y < ptm->height; ++y)
        for (size_t x = 0; x < ptm->width; ++x)
        {
            size_t index = (x + y*ptm->width);
            size_t invin = numPixels - index - 1;

            for (size_t p = 0; p < 6; ++p)
                ptm->coefficients[index*6 + p] = planes[p][invin];

            for (size_t p = 0; p < 3; ++p)
                ptm->coefficients[numPixels*6 + index*3 + p] = planes[6 + p][invin];
        }
            
        std::for_each(planes.begin(), planes.end(), stbi_image_free);
    }
    else
        throw std::runtime_error("Can't read format, not implemented");

    stream.close();
}

/**
 * Dump a PTM structure into three image files.
 *
 * This function converts a PTM into three files that are written as PNGs to the disk.
 * In case of LRGB PTMs, the three images contain: 
 * - high oder coefficients (i.e. coefficients 0, 1 and 2)
 * - low order coefficients (i.e. coefficients 3, 4 and 5)
 * - RGB data
 *
 * To reassemble a PTM, you'll need to read all three files and read the luminance coefficients
 * from the first two images and add the result of the PTM polynomial calculation to the color read
 * from the third image. Before doing so, you'll need to adjust the luminance coefficients by their
 * scale and bias parameters!
 *
 * Currently, only LRGB PTMs are supported.
 */
void ptm_dump(PTMHeader12* ptm)
{
    if (ptm->format != PTM_FORMAT_LRGB && ptm->format != PTM_FORMAT_JPEG_LRGB)
        throw std::runtime_error("Can't dump format");

    std::vector<unsigned char> coeffHdata;
    std::vector<unsigned char> coeffLdata;
    std::vector<unsigned char> rgbdata;

    const size_t num_pixels = ptm->width * ptm->height;
    const size_t size = num_pixels * 3;
    coeffHdata.resize(size);
    coeffLdata.resize(size);
    rgbdata.resize(size);

    for (size_t y = 0; y < ptm->height; ++y)
    for (size_t x = 0; x < ptm->width; ++x)
    for (size_t c = 0; c < 3; ++c)
    {
        size_t p = ((y * ptm->width) + x);
        size_t index = p*3;
        
        // flip image upside down if format LRGB
        if (ptm->format == PTM_FORMAT_LRGB)
            index = (((ptm->height - 1 - y) * ptm->width) + x) * 3;

        // flip image horizontally if format JPEG
        if (ptm->format == PTM_FORMAT_JPEG_LRGB)
            index = ((y * ptm->width) + (ptm->width - 1 - x)) * 3;
        
        // coefficients: first wxhx6 block
        coeffHdata[index + c] = ptm->coefficients[p*6 + c];
        coeffLdata[index + c] = ptm->coefficients[p*6 + c + 3];
    
        // rgb: second wxhx3 block
        rgbdata[index + c] = ptm->coefficients[num_pixels*6 + p*3 + c];
    }

    if (!stbi_write_png("coeffH.png", ptm->width, ptm->height, 3, &coeffHdata[0], 0))
        throw std::runtime_error("Couldn't write PNG file");

    if (!stbi_write_png("coeffL.png", ptm->width, ptm->height, 3, &coeffLdata[0], 0))
        throw std::runtime_error("Couldn't write PNG file");

    if (!stbi_write_png("rgb.png", ptm->width, ptm->height, 3, &rgbdata[0], 0))
        throw std::runtime_error("Couldn't write PNG file");
}

/**
 * Print some basic info about the PTM
 */
void print_info(PTMHeader12* ptm)
{
    std::cout << "Width: " << ptm->width << std::endl;
    std::cout << "Height: " << ptm->height << std::endl;
    
    std::cout << "Scale coefficients: ";
    for (size_t i = 0; i < 6; ++i)
        std::cout << ptm->scale[i] << " ";
    
    std::cout << std::endl << "Bias coefficients: ";
    for (size_t i = 0; i < 6; ++i)
        std::cout << ptm->bias[i] << " ";
    
    std::cout << std::endl;
}

int main(int argc, char** argv)
{ 
    try 
    {
        if (argc < 2)
            throw std::runtime_error("No input file");
        
        PTMHeader12 ptm;
        
        // read ptm file into ptm pointer
        ptm_read(argv[1], &ptm);
        
        // write ptm coefficients and image data to disk
        ptm_dump(&ptm);
        
        // print width/height and scale/bias coefficients
        print_info(&ptm);
    }
    catch (std::exception& e)
    {
        std::cout << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}