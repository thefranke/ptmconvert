/*
 * taf_ptm.h - http://www.tobias-franke.eu/projects/ptm/
 *
 * Copyright (c) 2012 Tobias Alexander Franke (tobias.franke@siggraph.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * To use this library start with:
 *
 *     #define TAF_PTM_IMPLEMENTATION
 *     #include "taf_ptm.h"
 *
 * Basic usage:
 *
 *     taf::uchar_vec coeff_h, coeff_l, rgb;
 *     auto ptmh = taf::ptm_load(filename, &coeff_h, &coeff_l, &rgb);
 *
 * If you prefer raw pointers:
 *
 *     unsigned char *coeff_h, *coeff_l, *rgb;
 *     auto ptmh = taf::ptm_load(filename, &coeff_h, &coeff_l, &rgb);
 *
 *     ...
 *
 *     taf::ptm_cleanup(&coeff_h, &coeff_l, &rgb);
 *
 * Each vector/pointer will hold one regular RGB image with coefficients. The
 * returned structure "ptmh" contains scale and bias coefficients as well as
 * the width and height of all three images.
 *
 * If you need different error handling than regular exceptions, you may simply 
 * redefine:
 *
 *     #define TAF_ASSERT(COND, MSG) myErrorFunc(COND, MSG)
 *
 * before including the header.
 *
 * Details on Polynomial Texture Maps can be found in the original paper:
 * http://www.hpl.hp.com/research/ptm/downloads/PtmFormat12.pdf
 *
 */

#ifndef TAF_PTM_H
#define TAF_PTM_H

#ifndef TAF_ASSERT
#define TAF_ASSERT(COND, MSG) if(!(COND)) throw std::runtime_error((MSG))
#endif

#include <vector>

namespace taf
{
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
        std::vector<int> motion_vectors;
        std::vector<int> order;
        std::vector<int> reference_planes;
        std::vector<unsigned int> compressed_size;
        std::vector<unsigned int> side_information;
    };
    
    struct PTMHeader12
    {
        PTMFormat format;
        size_t width;
        size_t height;
        float scale[6];
        int bias[6];
        
        CompressionInfo ci;
    };
    
    struct PTM12
    {
        PTMHeader12 header;
        std::vector<unsigned char> coefficients;
    };
    
    using uchar_vec = std::vector<unsigned char>;
    
    namespace detail
    {
        void init_ci(PTMHeader12* ptm);
        void ptm_allocate(uchar_vec* coeff_h, uchar_vec* coeff_l, uchar_vec* rgb, size_t size);
        void ptm_allocate(unsigned char** coeff_h, unsigned char** coeff_l, unsigned char** rgb, size_t size);
    }
    
    /**
     * Returns true if the PTM has been compressed with JPEG
     */
    bool is_compressed(const PTMHeader12* ptm);
    
    /**
     * Returns true if the PTM is LRGB, i.e. it has a block of RGB data
     */
    bool is_lrgb(const PTMHeader12* ptm);
    
    /**
     * Returns the number of Entries Per Pixel (RGB + coefficients or just coefficients)
     */
    size_t get_epp(const PTMHeader12* ptm);
    
    /**
     * Helper function to cleanup reserved memory after usage
     */
    void ptm_cleanup(unsigned char** coeff_h, unsigned char** coeff_l, unsigned char** rgb);
    
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
    void ptm_load(const char* file, PTM12* ptm);
    
    /**
     * Convert a PTM to regular RGB images
     *
     * Converts a PTM to three regular RGB images. The coefficients ptm->coefficients are transformed
     * and separated into three arrays coeff_h, coeff_l and rgb.
     */
    void ptm_load(const PTM12* ptm, unsigned char** coeff_h, unsigned char** coeff_l, unsigned char** rgb);
    
    /**
     * Read and convert a PTM to regular RGB images
     *
     * This function loads a PTM from a given filename and converts it into three RGB arrays. This
     * template accepts either unsigned char** or taf::uchar_vec* as types for coeff_h,
     * coeff_l or rgb.
     */
    template<typename Container>
    PTMHeader12 ptm_load(const char* file, Container coeff_h, Container coeff_l, Container rgb)
    {
        PTM12 ptm;
        
        ptm_load(file, &ptm);
        
        const size_t size = ptm.header.width * ptm.header.height * 3;
        
        detail::ptm_allocate(coeff_h, coeff_l, rgb, size);
        
        unsigned char* h_ptr   = &((*coeff_h)[0]);
        unsigned char* l_ptr   = &((*coeff_l)[0]);
        unsigned char* rgb_ptr = &((*rgb)[0]);
        
        ptm_load(&ptm, &h_ptr, &l_ptr, &rgb_ptr);
        
        return ptm.header;
    }
}

#ifdef TAF_PTM_IMPLEMENTATION

#include <map>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <stdexcept>
#include <iterator>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace taf
{
    bool is_compressed(const PTMHeader12* ptm)
    {
        return ptm->format == PTM_FORMAT_JPEG_RGB ||
               ptm->format == PTM_FORMAT_JPEG_LRGB ||
               ptm->format == PTM_FORMAT_JPEGLS_RGB ||
               ptm->format == PTM_FORMAT_JPEGLS_LRGB;
    }
    
    bool is_lrgb(const PTMHeader12* ptm)
    {
        return ptm->format == PTM_FORMAT_JPEG_LRGB ||
               ptm->format == PTM_FORMAT_LRGB ||
               ptm->format == PTM_FORMAT_JPEGLS_LRGB;
    }
    
    size_t get_epp(const PTMHeader12* ptm)
    {
        if (is_lrgb(ptm))
            return 9;
        else
            return 18;
    }
    
    namespace detail
    {
        void init_ci(PTMHeader12* ptm)
        {
            size_t epp = get_epp(ptm);
            ptm->ci.transforms.resize(epp);
            ptm->ci.motion_vectors.resize(epp*2);
            ptm->ci.order.resize(epp);
            ptm->ci.reference_planes.resize(epp);
            ptm->ci.compressed_size.resize(epp);
            ptm->ci.side_information.resize(epp);
        }
        
        void ptm_allocate(uchar_vec* coeff_h, uchar_vec* coeff_l, uchar_vec* rgb, size_t size)
        {
            coeff_h->resize(size);
            coeff_l->resize(size);
            rgb->resize(size);
        }
        
        void ptm_allocate(unsigned char** coeff_h, unsigned char** coeff_l, unsigned char** rgb, size_t size)
        {
            (*coeff_h) = new unsigned char[size];
            (*coeff_l) = new unsigned char[size];
            (*rgb)     = new unsigned char[size];
        }
    }
    
    void ptm_cleanup(unsigned char** coeff_h, unsigned char** coeff_l, unsigned char** rgb)
    {
        delete [] *coeff_h;
        delete [] *coeff_l;
        delete [] *rgb;
        
        *coeff_h = nullptr;
        *coeff_l = nullptr;
        *rgb = nullptr;
    }
    
    void ptm_load(const char* file, PTM12* ptm)
    {
        std::ifstream stream(file, std::ios::binary);
        
        TAF_ASSERT(stream.good(), "Can't open file");
        
        std::string version;
        stream >> version;
        
        TAF_ASSERT(version == "PTM_1.2", "Wrong version");
        
        std::string format;
        stream >> format;
        
        TAF_ASSERT(format == "PTM_FORMAT_LRGB" || format == "PTM_FORMAT_JPEG_LRGB", (std::string("Unknown format:") + format).c_str());
        
        if (format == "PTM_FORMAT_LRGB")
            ptm->header.format = PTM_FORMAT_LRGB;
        else if (format == "PTM_FORMAT_JPEG_LRGB")
            ptm->header.format = PTM_FORMAT_JPEG_LRGB;
        
        stream >> ptm->header.width;
        stream >> ptm->header.height;
        
        for (size_t i = 0; i < 6; ++i)
            stream >> ptm->header.scale[i];
        
        for (size_t i = 0; i < 6; ++i)
            stream >> ptm->header.bias[i];
        
        size_t epp = get_epp(&ptm->header);
        
        if (is_compressed(&ptm->header))
        {
            detail::init_ci(&ptm->header);
            
            stream >> ptm->header.ci.compressionParameter;
            
            // TODO: enum
            for (size_t i = 0; i < epp; ++i)
            {
                int v;
                stream >> v;
                switch(v)
                {
                    default:
                    case 0:
                        ptm->header.ci.transforms[i] = NOTHING;
                        break;
                    case 1:
                        ptm->header.ci.transforms[i] = PLANE_INVERSION;
                        break;
                    case 2:
                        ptm->header.ci.transforms[i] = MOTION_COMPENSATION;
                        break;
                }
            }
            
            for (size_t i = 0; i < epp * 2; ++i)
                stream >> ptm->header.ci.motion_vectors[i];
            
            for (size_t i = 0; i < epp; ++i)
                stream >> ptm->header.ci.order[i];
            
            for (size_t i = 0; i < epp; ++i)
                stream >> ptm->header.ci.reference_planes[i];
            
            for (size_t i = 0; i < epp; ++i)
                stream >> ptm->header.ci.compressed_size[i];
            
            for (size_t i = 0; i < epp; ++i)
                stream >> ptm->header.ci.side_information[i];
        }
        
        // search for newline
        char temp;
        do { stream.read(&temp, 1); } while (temp != '\n');
        
        ptm->coefficients.clear();
        
        size_t size = ptm->header.width * ptm->header.height * epp;
        ptm->coefficients.resize(size);
        
        if (ptm->header.format == PTM_FORMAT_LRGB)
        {
            stream.read(reinterpret_cast<char*>(&ptm->coefficients[0]), size);
        }
        else if (ptm->header.format == PTM_FORMAT_JPEG_LRGB)
        {
            std::vector<unsigned char*> planes(epp);
            std::vector<std::vector<unsigned char>> side_info(epp);
            
            std::map<size_t, size_t> order;
            
            // first pass: extract all planes
            for (size_t p = 0; p < epp; ++p)
            {
                int w = ptm->header.width;
                int h = ptm->header.height;
                int comp = 1;
                
                // read jpeg buffer
                size_t bufs = ptm->header.ci.compressed_size[p];
                std::vector<char> jpegbuf(bufs);
                stream.read(&jpegbuf[0], bufs);
                
                size_t sides = ptm->header.ci.side_information[p];
                if (sides > 0)
                {
                    side_info[p].resize(sides);
                    stream.read(reinterpret_cast<char*>(&side_info[p][0]), sides);
                }
                
                // convert to char values
                planes[p] = stbi_load_from_memory(reinterpret_cast<unsigned char*>(&jpegbuf[0]), bufs, &w, &h, &comp, 1);
                
                TAF_ASSERT(!stbi_failure_reason(), stbi_failure_reason());

                TAF_ASSERT(comp == 1, "Too many components in LRGB image");
                
                TAF_ASSERT(w == ptm->header.width && h == ptm->header.height, "Incompatible image size found");
                
                order[ptm->header.ci.order[p]] = p;
            }
            
            // second pass: apply predicition and transformation
            for (size_t n = 0; n < epp; ++n)
            {
                // query actual plane number according to order map
                size_t i = order[n];
                size_t j = ptm->header.ci.reference_planes[i];
                
                unsigned char* i_plane = planes[i];
                
                size_t num_pixels = ptm->header.width * ptm->header.height;
                
                // prediction if plane index j is not -1
                if (j != -1)
                {
                    unsigned char* j_plane = planes[j];
                    
                    for (size_t x = 0; x < num_pixels; ++x)
                    {
                        unsigned char jpx = j_plane[x];
                        
                        // apply transformation
                        // TODO: add motion vector transformation!
                        if (ptm->header.ci.transforms[i] == PLANE_INVERSION)
                            jpx = 255 - jpx;
                        
                        i_plane[x] = (jpx + i_plane[x] - 128)%255;
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
                        
                        int w = ptm->header.width;
                        int h = ptm->header.height;
                        
                        int width = index % w;
                        int height = index / w;
                        int h2 = h - height - 1;
                        int index2 = h2 * w + width;
                        
                        i_plane[index2] = v;
                    }
                }
            }
            
            size_t num_pixels = ptm->header.width * ptm->header.height;
            
            for (size_t y = 0; y < ptm->header.height; ++y)
                for (size_t x = 0; x < ptm->header.width; ++x)
                {
                    size_t index = (x + y*ptm->header.width);
                    size_t invin = num_pixels - index - 1;
                    
                    for (size_t p = 0; p < 6; ++p)
                        ptm->coefficients[index*6 + p] = planes[p][invin];
                    
                    for (size_t p = 0; p < 3; ++p)
                        ptm->coefficients[num_pixels*6 + index*3 + p] = planes[6 + p][invin];
                }
            
            for (auto i : planes)
                stbi_image_free(i);
        }
        
        stream.close();
    }
    
    void ptm_load(const PTM12* ptm, unsigned char** coeff_h, unsigned char** coeff_l, unsigned char** rgb)
    {
        TAF_ASSERT(ptm->header.format == PTM_FORMAT_LRGB || ptm->header.format == PTM_FORMAT_JPEG_LRGB, "Can't read format into RGB buffer");
        
        const size_t num_pixels = ptm->header.width * ptm->header.height;
        
        for (size_t y = 0; y < ptm->header.height; ++y)
            for (size_t x = 0; x < ptm->header.width; ++x)
                for (size_t c = 0; c < 3; ++c)
                {
                    size_t p = ((y * ptm->header.width) + x);
                    size_t index = p*3;
                    
                    // flip image upside down if format LRGB
                    if (ptm->header.format == PTM_FORMAT_LRGB)
                        index = (((ptm->header.height - 1 - y) * ptm->header.width) + x) * 3;
                    
                    // flip image horizontally if format JPEG
                    if (ptm->header.format == PTM_FORMAT_JPEG_LRGB)
                        index = ((y * ptm->header.width) + (ptm->header.width - 1 - x)) * 3;
                    
                    // coefficients: first wxhx6 block
                    (*coeff_h)[index + c] = ptm->coefficients[p*6 + c];
                    (*coeff_l)[index + c] = ptm->coefficients[p*6 + c + 3];
                    
                    // rgb: second wxhx3 block
                    (*rgb)[index + c] = ptm->coefficients[num_pixels*6 + p*3 + c];
                }
    }
    
}
#endif

#endif
