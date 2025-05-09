// Copyright 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either expresso or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "gfxstream/guest/IOStream.h"

#include "GL2Encoder.h"

#include <GLES3/gl31.h>

#include <vector>

#include <assert.h>

namespace gfxstream {
namespace guest {

void IOStream::readbackPixels(void* context, int width, int height, unsigned int format, unsigned int type, void* pixels) {
    GL2Encoder *ctx = (GL2Encoder *)context;
    assert (ctx->state() != NULL);

    int bpp = 0;
    int startOffset = 0;
    int pixelRowSize = 0;
    int totalRowSize = 0;
    int skipRows = 0;

    ctx->state()->getPackingOffsets2D(width, height, format, type,
                                      &bpp,
                                      &startOffset,
                                      &pixelRowSize,
                                      &totalRowSize,
                                      &skipRows);

    size_t pixelDataSize =
        ctx->state()->pixelDataSize(
            width, height, 1, format, type, 1 /* is pack */);

    if (startOffset == 0 &&
        pixelRowSize == totalRowSize) {
        // fast path
        readback(pixels, pixelDataSize);
    } else if (pixelRowSize == totalRowSize && (pixelRowSize == width * bpp)) {
        // fast path but with skip in the beginning
        std::vector<char> paddingToDiscard(startOffset, 0);
        readback(&paddingToDiscard[0], startOffset);
        readback((char*)pixels + startOffset, pixelDataSize - startOffset);
    } else {

        if (startOffset > 0) {
            std::vector<char> paddingToDiscard(startOffset, 0);
            readback(&paddingToDiscard[0], startOffset);
        }
        // need to read back row by row
        size_t paddingSize = totalRowSize - pixelRowSize;
        std::vector<char> paddingToDiscard(paddingSize, 0);

        char* start = (char*)pixels + startOffset;

        for (int i = 0; i < height; i++) {
            if (pixelRowSize > width * bpp) {
                size_t rowSlack = pixelRowSize - width * bpp;
                std::vector<char> rowSlackToDiscard(rowSlack, 0);
                readback(start, width * bpp);
                readback(&rowSlackToDiscard[0], rowSlack);
                readback(&paddingToDiscard[0], paddingSize);
                start += totalRowSize;
            } else {
                readback(start, pixelRowSize);
                readback(&paddingToDiscard[0], paddingSize);
                start += totalRowSize;
            }
        }
    }
}

void IOStream::uploadPixels(void* context, int width, int height, int depth, unsigned int format, unsigned int type, const void* pixels) {
    GL2Encoder *ctx = (GL2Encoder *)context;
    assert (ctx->state() != NULL);

    if (1 == depth) {
        int bpp = 0;
        int startOffset = 0;
        int pixelRowSize = 0;
        int totalRowSize = 0;
        int skipRows = 0;

        ctx->state()->getUnpackingOffsets2D(width, height, format, type,
                &bpp,
                &startOffset,
                &pixelRowSize,
                &totalRowSize,
                &skipRows);

        size_t pixelDataSize =
            ctx->state()->pixelDataSize(
                    width, height, 1, format, type, 0 /* is unpack */);

        if (startOffset == 0 &&
                pixelRowSize == totalRowSize) {
            // fast path
            writeFully(pixels, pixelDataSize);
        } else if (pixelRowSize == totalRowSize && (pixelRowSize == width * bpp)) {
            // fast path but with skip in the beginning
            std::vector<char> paddingToDiscard(startOffset, 0);
            writeFully(&paddingToDiscard[0], startOffset);
            writeFully((char*)pixels + startOffset, pixelDataSize - startOffset);
        } else {

            if (startOffset > 0) {
                std::vector<char> paddingToDiscard(startOffset, 0);
                writeFully(&paddingToDiscard[0], startOffset);
            }
            // need to upload row by row
            size_t paddingSize = totalRowSize - pixelRowSize;
            std::vector<char> paddingToDiscard(paddingSize, 0);

            char* start = (char*)pixels + startOffset;

            for (int i = 0; i < height; i++) {
                if (pixelRowSize > width * bpp) {
                    size_t rowSlack = pixelRowSize - width * bpp;
                    std::vector<char> rowSlackToDiscard(rowSlack, 0);
                    writeFully(start, width * bpp);
                    writeFully(&rowSlackToDiscard[0], rowSlack);
                    writeFully(&paddingToDiscard[0], paddingSize);
                    start += totalRowSize;
                } else {
                    writeFully(start, pixelRowSize);
                    writeFully(&paddingToDiscard[0], paddingSize);
                    start += totalRowSize;
                }
            }
        }
    } else {
        int bpp = 0;
        int startOffset = 0;
        int pixelRowSize = 0;
        int totalRowSize = 0;
        int pixelImageSize = 0;
        int totalImageSize = 0;
        int skipRows = 0;
        int skipImages = 0;

        ctx->state()->getUnpackingOffsets3D(width, height, depth, format, type,
                &bpp,
                &startOffset,
                &pixelRowSize,
                &totalRowSize,
                &pixelImageSize,
                &totalImageSize,
                &skipRows,
                &skipImages);

        size_t pixelDataSize =
            ctx->state()->pixelDataSize(
                    width, height, depth, format, type, 0 /* is unpack */);


        if (startOffset == 0 &&
            pixelRowSize == totalRowSize &&
            pixelImageSize == totalImageSize) {
            // fast path
            writeFully(pixels, pixelDataSize);
        } else if (pixelRowSize == totalRowSize &&
                   pixelImageSize == totalImageSize &&
                   pixelRowSize == (width * bpp)) {
            // fast path but with skip in the beginning
            std::vector<char> paddingToDiscard(startOffset, 0);
            writeFully(&paddingToDiscard[0], startOffset);
            writeFully((char*)pixels + startOffset, pixelDataSize - startOffset);
        } else {

            if (startOffset > 0) {
                std::vector<char> paddingToDiscard(startOffset, 0);
                writeFully(&paddingToDiscard[0], startOffset);
            }
            // need to upload row by row
            size_t paddingSize = totalRowSize - pixelRowSize;
            std::vector<char> paddingToDiscard(paddingSize, 0);

            char* start = (char*)pixels + startOffset;

            size_t imageSlack = totalImageSize - pixelImageSize;
            std::vector<char> imageSlackToDiscard(imageSlack, 0);

            for (int k = 0; k < depth; ++k) {
                for (int i = 0; i < height; i++) {
                    if (pixelRowSize > width * bpp) {
                        size_t rowSlack = pixelRowSize - width * bpp;
                        std::vector<char> rowSlackToDiscard(rowSlack, 0);
                        writeFully(start, width * bpp);
                        writeFully(&rowSlackToDiscard[0], rowSlack);
                        writeFully(&paddingToDiscard[0], paddingSize);
                        start += totalRowSize;
                    } else {
                        writeFully(start, pixelRowSize);
                        writeFully(&paddingToDiscard[0], paddingSize);
                        start += totalRowSize;
                    }
                }
                if (imageSlack > 0) {
                    writeFully(&imageSlackToDiscard[0], imageSlack);
                    start += imageSlack;
                }
            }
        }
    }
}

}  // namespace guest
}  // namespace gfxstream