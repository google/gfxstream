// Copyright 2015 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Some free functions for manipulating strings as URIs. Wherever possible,
// these functions take const references to std::string_view to avoid
// unnecessary copies.

#include "gfxstream/StringFormat.h"

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace gfxstream {
namespace base {

class Uri {
public:
    // |Encode| is aggressive -- it will always encode a reserved character,
    // disregarding a possibly included URL scheme.
    static std::string Encode(std::string_view uri);

    // |Decode| is aggressive. It will decode every occurance of %XX in a single
    // pass -- even for unreserved characters.
    // Returns empty string on error.
    static std::string Decode(std::string_view uri);

    // Set of functions for arguments encoding
    struct FormatHelper {
        // Anything which can potentially have encodable character goes here and
        // is encoded into a const char*
        static std::string encodeArg(std::string_view str);

        // Forward the rest as-is (non-std::string_view types)
        template <class T>
        static T&& encodeArg(
                T&& t,
                typename std::enable_if<
                        !std::is_convertible<typename std::decay<T>::type,
                                             std::string_view>::value>::type* =
                        nullptr) {
            // Don't allow single char parameters as they have a '%c' format
            // specifier but potentially may encode into a whole string. This
            // is very confusing for the user (which format to use - %c or %s?)
            // and is too error-prone (how to make sure no one later
            // 'fixes' 'wrong' format?)
            static_assert(!std::is_same<typename std::decay<T>::type, char>::value,
                "Uri::FormatEncodeArguments() does not support arguments of type 'char'");

            return std::forward<T>(t);
        }
    };

    // A small convenience method to encode all arguments when formatting the
    // string, but don't touch the |format| string itself
    template <class... Args>
    static std::string FormatEncodeArguments(const char* format,
                                             Args&&... args) {
        return gfxstream::base::StringFormat(
                    format,
                    FormatHelper::encodeArg(std::forward<Args>(args))...);
    }
};

}  // namespace base
}  // namespace android
