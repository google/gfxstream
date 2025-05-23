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

#ifndef GLES_TEXTURE_UTILS_H
#define GLES_TEXTURE_UTILS_H

#include <GLES3/gl31.h>

namespace GLESTextureUtils {

// By spec, the buffer is only required to provide just enough data. The
// last row does not have to fill unpackRowLength. But our decoder is
// written to always read full row. So we add "ignoreTrailing" here. When
// ignoreTrailing == 1 we compute the real size as defined by spec. When
// ignoreTrailing == 0 we compute the size used by decoder/encoder.
void computeTextureStartEnd(
        GLsizei width, GLsizei height, GLsizei depth,
        GLenum format, GLenum type,
        int unpackAlignment,
        int unpackRowLength,
        int unpackImageHeight,
        int unpackSkipPixels,
        int unpackSkipRows,
        int unpackSkipImages,
        int* start,
        int* end,
        int ignoreTrailing);

int computeTotalImageSize(
        GLsizei width, GLsizei height, GLsizei depth,
        GLenum format, GLenum type,
        int unpackAlignment,
        int unpackRowLength,
        int unpackImageHeight,
        int unpackSkipPixels,
        int unpackSkipRows,
        int unpackSkipImages);

int computeNeededBufferSize(
        GLsizei width, GLsizei height, GLsizei depth,
        GLenum format, GLenum type,
        int unpackAlignment,
        int unpackRowLength,
        int unpackImageHeight,
        int unpackSkipPixels,
        int unpackSkipRows,
        int unpackSkipImages,
        int ignoreTrailing);

// Writes out |height| offsets for glReadPixels to read back
// data in separate rows of pixels. Returns:
// 1. |startOffset|: offset in bytes to apply at the beginning
// 2. |packingPixelRowSize|: the buffer size in bytes that has the actual pixels per row.
// 2. |packingTotalRowSize|: the length in bytes of each row including the padding from row length.
void computePackingOffsets2D(
        GLsizei width, GLsizei height,
        GLenum format, GLenum type,
        int packAlignment,
        int packRowLength,
        int packSkipPixels,
        int packSkipRows,
        int* bpp,
        int* startOffset,
        int* packingPixelRowSize,
        int* packingTotalRowSize);

// For processing 3D textures exactly to the sizes of client buffers.
void computePackingOffsets3D(
        GLsizei width, GLsizei height, GLsizei depth,
        GLenum format, GLenum type,
        int packAlignment,
        int packRowLength,
        int packImageHeight,
        int packSkipPixels,
        int packSkipRows,
        int packSkipImages,
        int* bpp,
        int* startOffset,
        int* packingPixelRowSize,
        int* packingTotalRowSize,
        int* packingPixelImageSize,
        int* packingTotalImageSize);

// For calculating compressed sizes of ETC/EAC formatted images in the guest.
GLsizei getCompressedImageSize(GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth, bool* error);

// Format queries
bool isEtc2Format(GLenum internalformat);
bool isAstcFormat(GLenum internalformat);
bool isBptcFormat(GLenum internalformat);
bool isS3tcFormat(GLenum internalformat);
bool isRgtcFormat(GLenum internalformat);

} // namespace GLESTextureUtils
#endif
