/* 
 * ptmconvert by Tobias Alexander Franke (tobias.franke@siggraph.org) 2012 
 * For copyright and license see LICENSE
 */

#include <iostream>

#define TAF_PTM_IMPLEMENTATION
#include "taf_ptm.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

/**
 * Helper function to print bias, scale and size of a PTM.
 */
void ptm_print_info(const taf::PTMHeader12& ptm)
{
    auto&& log = std::clog;
    
    log << "Width: "  << ptm.width << std::endl;
    log << "Height: " << ptm.height << std::endl;
    
    log << "Scale coefficients: ";
    for (size_t i = 0; i < 6; ++i)
        log << ptm.scale[i] << " ";
    
    log << std::endl << "Bias coefficients: ";
    for (size_t i = 0; i < 6; ++i)
        log << ptm.bias[i] << " ";
    
    log << std::endl;
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
void ptm_dump_png(const char* filename)
{
    taf::uchar_vec coeff_h, coeff_l, rgb;
    taf::PTMHeader12 ptmh = taf::ptm_load(filename, &coeff_h, &coeff_l, &rgb);

    auto write_png = [&ptmh](const char* f, unsigned char* data)
    {
        return stbi_write_png(f, ptmh.width, ptmh.height, 3, data, 0);
    };
    
    if (!write_png("coeff_h.png", &coeff_h[0]) ||
        !write_png("coeff_l.png", &coeff_l[0]) ||
        !write_png("rgb.png",     &rgb[0]))
    {
        throw std::runtime_error("Couldn't write PNG files");
    }
    
    ptm_print_info(ptmh);
}

int main(int argc, char** argv)
{
    try
    {
        if (argc < 2)
            throw std::runtime_error("No input file");
        
        ptm_dump_png(argv[1]);
    }
    catch (std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}