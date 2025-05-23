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

#include "IndexRangeCache.h"

// This is almost literally
// external/angle/src/libANGLE/IndexRangeCache.cpp

void IndexRangeCache::addRange(GLenum type,
                               size_t offset,
                               size_t count,
                               bool primitiveRestartEnabled,
                               int start,
                               int end) {
    IndexRange r;
    r.start = start;
    r.end = end;
    mIndexRangeCache[IndexRangeKey(type, offset, count, primitiveRestartEnabled)] = r;
}

bool IndexRangeCache::findRange(GLenum type,
                                size_t offset,
                                size_t count,
                                bool primitiveRestartEnabled,
                                int* start_out,
                                int* end_out) const {
    IndexRangeMap::const_iterator it =
        mIndexRangeCache.find(
                IndexRangeKey(type, offset, count, primitiveRestartEnabled));

    if (it != mIndexRangeCache.end()) {
        if (start_out) *start_out = it->second.start;
        if (end_out) *end_out = it->second.end;
        return true;
    } else {
        if (start_out) *start_out = 0;
        if (end_out) *end_out = 0;
        return false;
    }
}


void IndexRangeCache::invalidateRange(size_t offset, size_t size) {
    size_t invalidateStart = offset;
    size_t invalidateEnd = offset + size;

    IndexRangeMap::iterator it = mIndexRangeCache.begin();

    while (it != mIndexRangeCache.end()) {
        size_t rangeStart = it->first.offset;
        size_t rangeEnd =
            it->first.offset +
            it->first.count * glSizeof(it->first.type);

        if (invalidateEnd < rangeStart ||
            invalidateStart > rangeEnd) {
            ++it;
        } else {
            mIndexRangeCache.erase(it++);
        }
    }
}

void IndexRangeCache::clear() {
    mIndexRangeCache.clear();
}
