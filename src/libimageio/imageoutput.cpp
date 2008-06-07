/////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2008 Larry Gritz
// 
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
// 
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
// 
// (this is the MIT license)
/////////////////////////////////////////////////////////////////////////////


#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstdarg>
#include <iostream>
#include <vector>

#include "dassert.h"
#include "paramtype.h"
#include "filesystem.h"
#include "plugin.h"
#include "thread.h"
#include "strutil.h"

#define DLL_EXPORT_PUBLIC /* Because we are implementing ImageIO */
#include "imageio.h"
#include "imageio_pvt.h"
#undef DLL_EXPORT_PUBLIC

using namespace OpenImageIO;
using namespace OpenImageIO::pvt;



int
ImageOutput::send_to_output (const char *format, ...)
{
    // FIXME -- I can't remember how this is supposed to work
    return 0;
}



int
ImageOutput::send_to_client (const char *format, ...)
{
    // FIXME -- I can't remember how this is supposed to work
    return 0;
}



void
ImageOutput::error (const char *message, ...)
{
    va_list ap;
    va_start (ap, message);
    m_errmessage = Strutil::vformat (message, ap);
    va_end (ap);
}



const void *
ImageOutput::to_native_scanline (ParamBaseType format,
                                 const void *data, stride_t xstride,
                                 std::vector<unsigned char> &scratch)
{
    return to_native_rectangle (0, spec.width-1, 0, 0, 0, 0, format, data,
                                xstride, 0, 0, scratch);
}



const void *
ImageOutput::to_native_tile (ParamBaseType format, const void *data,
                             stride_t xstride, stride_t ystride, stride_t zstride,
                             std::vector<unsigned char> &scratch)
{
    return to_native_rectangle (0, spec.tile_width-1, 0, spec.tile_height-1,
                                0, std::max(0,spec.tile_depth-1), format, data,
                                xstride, ystride, zstride, scratch);
}



const void *
ImageOutput::to_native_rectangle (int xmin, int xmax, int ymin, int ymax,
                                  int zmin, int zmax, 
                                  ParamBaseType format, const void *data,
                                  stride_t xstride, stride_t ystride, stride_t zstride,
                                  std::vector<unsigned char> &scratch)
{
    spec.auto_stride (xstride, ystride, zstride);

    // Compute width and height from the rectangle extents
    int width = xmax - xmin + 1;
    int height = ymax - ymin + 1;
    int depth = zmax - zmin + 1;

    // Do the strides indicate that the data are already contiguous?
    bool contiguous = (xstride == spec.nchannels*ParamBaseTypeSize(format) &&
                       (ystride == xstride*width || height == 1) &&
                       (zstride == ystride*height || depth == 1));
    // Is the only conversion we are doing that of data format?
    bool data_conversion_only =  (contiguous && spec.gamma == 1.0f);

    if (format == spec.format && data_conversion_only) {
        // Data are already in the native format, contiguous, and need
        // no gamma correction -- just return a ptr to the original data.
        return data;
    }

    int rectangle_pixels = width * height * depth;
    int rectangle_values = rectangle_pixels * spec.nchannels;
    int contiguoussize = contiguous ? 0 
                             : rectangle_values * ParamBaseTypeSize(format);
    contiguoussize = (contiguoussize+3) & (~3); // Round up to 4-byte boundary
    DASSERT ((contiguoussize & 3) == 0);
    int rectangle_bytes = rectangle_pixels * spec.pixel_bytes();
    int floatsize = rectangle_values * sizeof(float);
    scratch.resize (contiguoussize + floatsize + rectangle_bytes);

    // Force contiguity if not already present
    if (! contiguous) {
        data = contiguize (data, spec.nchannels, xstride, ystride, zstride,
                           (void *)&scratch[0], width, height, depth, format);
    }

    // Rather than implement the entire cross-product of possible
    // conversions, use float as an intermediate format, which generally
    // will always preserve enough precision.
    const float *buf;
    if (format == PT_FLOAT && spec.gamma == 1.0f) {
        // Already in float format and no gamma correction is needed --
        // leave it as-is.
        buf = (float *)data;
    } else {
        // Convert to from 'format' to float.
        buf = convert_to_float (data, (float *)&scratch[contiguoussize],
                                rectangle_values, format);
        // Now buf points to float
        if (spec.gamma != 1) {
            float invgamma = 1.0 / spec.gamma;
            float *f = (float *)buf;
            for (int p = 0;  p < rectangle_pixels;  ++p)
                for (int c = 0;  c < spec.nchannels;  ++c, ++f)
                    if (c != spec.alpha_channel)
                        *f = powf (*f, invgamma);
            // FIXME: we should really move the gamma correction to
            // happen immediately after contiguization.  That way,
            // byte->byte with gamma can use a table shortcut instead
            // of having to go through float just for gamma.
        }
        // Now buf points to gamma-corrected float
    }
    // Convert from float to native format.
    return convert_from_float (buf, &scratch[contiguoussize+floatsize], 
                       rectangle_values, spec.quant_black, spec.quant_white,
                       spec.quant_min, spec.quant_max, spec.quant_dither,
                       spec.format);
}



bool
ImageOutput::write_image (ParamBaseType format, const void *data,
                          stride_t xstride, stride_t ystride, stride_t zstride,
                          OpenImageIO::ProgressCallback progress_callback,
                          void *progress_callback_data)
{
    spec.auto_stride (xstride, ystride, zstride);
    if (supports ("rectangles")) {
        // Use a rectangle if we can
        return write_rectangle (0, spec.width-1, 0, spec.height-1, 0, spec.depth-1,
                                format, data, xstride, ystride, zstride);
    }

    bool ok = true;
    if (progress_callback)
        if (progress_callback (progress_callback_data, 0.0f))
            return ok;
    if (spec.tile_width && supports ("tiles")) {
        // Tiled image

        // FIXME: what happens if the image dimensions are smaller than
        // the tile dimensions?  Or if one of the tiles runs past the
        // right or bottom edge?  Do we need to allocate a full tile and
        // copy into it before calling write_tile?  That's probably the
        // safe thing to do.  Or should that handling be pushed all the
        // way into write_tile itself?
        bool ok = true;
        for (int z = 0;  z < spec.depth;  z += spec.tile_depth)
            for (int y = 0;  y < spec.height;  y += spec.tile_height) {
                for (int x = 0;  x < spec.width && ok;  y += spec.tile_width)
                    ok &= write_tile (x, y, z, format,
                                      (const char *)data + z*zstride + y*ystride + x*xstride,
                                      xstride, ystride, zstride);
                if (progress_callback)
                    if (progress_callback (progress_callback_data, (float)y/spec.height))
                        return ok;
            }
    } else {
        // Scanline image
        bool ok = true;
        for (int z = 0;  z < spec.depth;  ++z)
            for (int y = 0;  y < spec.height && ok;  ++y) {
                ok &= write_scanline (y, z, format,
                                      (const char *)data + z*zstride + y*ystride,
                                      xstride);
                if (progress_callback && !(y & 0x0f))
                    if (progress_callback (progress_callback_data, (float)y/spec.height))
                        return ok;
            }
    }
    if (progress_callback)
        progress_callback (progress_callback_data, 1.0f);

    return ok;
}
