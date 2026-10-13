/*
* Copyright (C) 2011 The Android Open Source Project
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/
#include "common/palette_texture.h"
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>



struct Color
{
  Color(unsigned char r, unsigned char g,unsigned char b, unsigned char a):red(r),green(g),blue(b),alpha(a){};
  unsigned char red;
  unsigned char green;
  unsigned char blue;
  unsigned char alpha;
};

static void getPaletteInfo(GLenum internalFormat,unsigned int& indexSizeBits,unsigned int& colorSizeBytes,GLenum& colorFrmt) {

        colorFrmt = GL_RGB;
        switch(internalFormat)
    {
    case GL_PALETTE4_RGB8_OES:
        indexSizeBits = 4;
        colorSizeBytes = 3;
        break;

    case GL_PALETTE4_RGBA8_OES:
        indexSizeBits = 4;
        colorSizeBytes = 4;
        colorFrmt = GL_RGBA;
        break;

    case GL_PALETTE4_RGBA4_OES:
    case GL_PALETTE4_RGB5_A1_OES:
        colorFrmt = GL_RGBA;
        [[fallthrough]];
    case GL_PALETTE4_R5_G6_B5_OES:
        indexSizeBits = 4;
        colorSizeBytes = 2;
        break;

    case GL_PALETTE8_RGB8_OES:
        indexSizeBits = 8;
        colorSizeBytes = 3;
        break;

    case GL_PALETTE8_RGBA8_OES:
        indexSizeBits = 8;
        colorSizeBytes = 4;
        colorFrmt = GL_RGBA;
        break;

    case GL_PALETTE8_RGBA4_OES:
    case GL_PALETTE8_RGB5_A1_OES:
        colorFrmt = GL_RGBA;
        [[fallthrough]];
    case GL_PALETTE8_R5_G6_B5_OES:
        indexSizeBits = 8;
        colorSizeBytes = 2;
        break;
    }
}


static Color paletteColor(const unsigned char* pallete,unsigned int index,GLenum format)
{
        uint16_t s = 0;
        switch(format) {
        //RGB
    case GL_PALETTE4_RGB8_OES:
    case GL_PALETTE8_RGB8_OES:
            return Color(pallete[index],pallete[index+1],pallete[index+2],0);
    case GL_PALETTE8_R5_G6_B5_OES:
    case GL_PALETTE4_R5_G6_B5_OES:
            memcpy(&s, pallete + index, sizeof(uint16_t));
            return Color((s >> 11)*255/31,((s >> 5) & 0x3f)*255/63 ,(s & 0x1f)*255/31,0);

        //RGBA
    case GL_PALETTE4_RGBA8_OES:
    case GL_PALETTE8_RGBA8_OES:
            return Color(pallete[index],pallete[index+1],pallete[index+2],pallete[index+3]);
    case GL_PALETTE4_RGBA4_OES:
    case GL_PALETTE8_RGBA4_OES:
            memcpy(&s, pallete + index, sizeof(uint16_t));
            return Color(((s >> 12) & 0xf)*255/15,((s >> 8) & 0xf)*255/15,((s >> 4) & 0xf)*255/15 ,(s & 0xf)*255/15);
    case GL_PALETTE4_RGB5_A1_OES:
    case GL_PALETTE8_RGB5_A1_OES:
            memcpy(&s, pallete + index, sizeof(uint16_t));
            return Color(((s >> 11) & 0x1f)*255/31,((s >> 6) & 0x1f)*255/31,((s >> 1) & 0x1f)*255/31 ,(s & 0x1) * 255);
        default:
            return Color(255,255,255,255);
    }
}

unsigned char* uncompressTexture(GLenum internalformat,GLenum& formatOut,GLsizei width,GLsizei height,GLsizei imageSize, const GLvoid* data,GLint level) {

    unsigned int indexSizeBits = 0;  //the size of the color index in the pallete
    unsigned int colorSizeBytes = 0; //the size of each color cell in the pallete

    getPaletteInfo(internalformat,indexSizeBits,colorSizeBytes,formatOut);
    if (!data || width <= 0 || height <= 0 || level < 0 || imageSize < 0 ||
        indexSizeBits == 0 || colorSizeBytes == 0)
    {
        return NULL;
    }

    const unsigned char* palette = static_cast<const unsigned char *>(data);
    const unsigned char* dataEnd = palette + imageSize;

    //the pallete positioned in the begininng of the data
    // so we jump over it to get to the colos indices in the palette

    int nColors = 2 << (indexSizeBits - 1); //2^indexSizeBits
    int paletteSizeBytes = nColors * colorSizeBytes;
    if (imageSize < paletteSizeBytes) {
        return NULL;
    }
    const unsigned char* imageIndices = palette + paletteSizeBytes;

    //jumping to the the correct mipmap level
    for(int i=0;i<level;i++) {
        uint64_t levelBytes =
            (static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * indexSizeBits + 7) / 8;
        if (levelBytes > static_cast<uint64_t>(dataEnd - imageIndices)) {
            return NULL;
        }
        imageIndices += levelBytes;
        width  = (width >> 1) ? (width >> 1) : 1;
        height = (height >> 1) ? (height >> 1) : 1;
    }

    int colorSizeOut = (formatOut == GL_RGB? 3:4);
    uint64_t nPixels64 = static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
    uint64_t outBytes64 = nPixels64 * static_cast<uint64_t>(colorSizeOut);
    if (outBytes64 == 0 || outBytes64 > static_cast<uint64_t>(INT32_MAX)) {
        return NULL;
    }
    int nPixels = static_cast<int>(nPixels64);
    unsigned char* pixelsOut = new unsigned char[static_cast<size_t>(outBytes64)];
    if(!pixelsOut) return NULL;
    memset(pixelsOut, 0, static_cast<size_t>(outBytes64));

    ptrdiff_t leftBytes = dataEnd - imageIndices;
    if (leftBytes < 0) {
        leftBytes = 0;
    }
    uint64_t leftPixels64 = (static_cast<uint64_t>(leftBytes) * 8) / indexSizeBits;

    int maxIndices = (leftPixels64 < nPixels64) ? static_cast<int>(leftPixels64) : nPixels;

    //filling the pixels array
    for(int i =0 ; i < maxIndices ; i++) {
        int paletteIndex = 0;
        int indexOut = i*colorSizeOut;
        if(indexSizeBits == 4) {
            paletteIndex = (i%2) == 0 ?
                           imageIndices[i/2] >> 4:  //upper bits
                           imageIndices[i/2] & 0xf; //lower bits
        } else {
            paletteIndex = imageIndices[i];
        }

        paletteIndex*=colorSizeBytes;
        Color c = paletteColor(palette,paletteIndex,internalformat);

        pixelsOut[indexOut] = c.red;
        pixelsOut[indexOut+1] = c.green;
        pixelsOut[indexOut+2] = c.blue;
        if(formatOut == GL_RGBA) {
            pixelsOut[indexOut+3] = c.alpha;
        }
    }
    return pixelsOut;
}

