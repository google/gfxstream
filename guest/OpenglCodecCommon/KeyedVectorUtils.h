/*
* Copyright (C) 2018 The Android Open Source Project
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
#pragma once

#include <map>

template <typename T>
void clearObjectMap(std::map<GLuint, T>& v) {
    typename std::map<GLuint, T>::iterator it = v.begin();
    for (; it != v.end(); ++it) {
        delete it->second;
    }
    v.clear();
}

template <typename K, typename V>
V findObjectOrDefault(const std::map<K, V>& m, K key, V defaultValue = 0) {
    typename std::map<K, V>::const_iterator it = m.find(key);

    if (it == m.end()) {
        return defaultValue;
    }

    return it->second;
}
