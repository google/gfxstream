/*
* Copyright (C) 2016 The Android Open Source Project
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

// This is almost literally
// external/angle/src/common/mathutil.h: IndexRange +
// external/angle/src/libANGLE/IndexRangeCache.h: IndexRangeCache,
// with adaptations to work with goldfish opengl driver.
// Currently, primitive restart is not supported, so there
// is a very minimal incorporation of that.

#ifndef _GL_INDEX_RANGE_CACHE_H_
#define _GL_INDEX_RANGE_CACHE_H_

#include <GLES/gl.h>
#include <GLES/glext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include "glUtils.h"

#include <map>

struct IndexRange {
    // Inclusive range of indices that are not primitive restart
    int start;
    int end;

    // Number of non-primitive restart indices
    size_t vertexIndexCount; // TODO; not being accounted yet (GLES3 feature)
};

class IndexRangeCache {
public:
    void addRange(GLenum type,
                  size_t offset,
                  size_t count,
                  bool primitiveRestartEnabled,
                  int start,
                  int end);
    bool findRange(GLenum type,
                   size_t offset,
                   size_t count,
                   bool primitiveRestartEnabled,
                   int* start_out,
                   int* end_out) const;
    void invalidateRange(size_t offset, size_t size);
    void clear();
private:
    struct IndexRangeKey {
        IndexRangeKey() :
            type(GL_NONE),
            offset(0),
            count(0),
            primitiveRestartEnabled(false) { }
        IndexRangeKey(GLenum _type,
                      size_t _offset,
                      size_t _count,
                      bool _primitiveRestart) :
            type(_type),
            offset(_offset),
            count(_count),
            primitiveRestartEnabled(_primitiveRestart) { }

        bool operator<(const IndexRangeKey& rhs) const {
            size_t end = offset + count * glSizeof(type);
            size_t end_other = rhs.offset + rhs.count * glSizeof(rhs.type);

            if (offset != rhs.offset) return offset < rhs.offset;
            if (end != end_other) return end < end_other;
            if (type != rhs.type) return type < rhs.type;
            if (primitiveRestartEnabled != rhs.primitiveRestartEnabled)
                return primitiveRestartEnabled;
            return false;
        }

        GLenum type;
        size_t offset;
        size_t count;
        bool primitiveRestartEnabled;
    };

    typedef std::map<IndexRangeKey, IndexRange> IndexRangeMap;
    IndexRangeMap mIndexRangeCache;
};

#endif
