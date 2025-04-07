// Copyright 2019 The Android Open Source Project
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

#pragma once

#include <type_traits>

// This header defines some utitily methods to manipulate scoped enums as flags
// C++11 scoped enums by default don't support flag operations (e.g. if (a & b))
// We need to define bitwise operations for them to be able to have strongly
// typed flags in our code.
//
// To enable the flag operators for your enum, most probably you just need to
// include this file. The only exception is if your enum is located in some
// namespace other than android, android::base or global. In that case you also
// need to add the following using to bring in the operators:
//
// using namespace ::android::base::EnumFlags;

namespace android {
namespace base {
namespace EnumFlags {

// A predicate which checks if template agument is a scoped enum
template<class E>
using is_scoped_enum = std::integral_constant<
        bool,
        std::is_enum<E>::value && !std::is_convertible<E, int>::value>;

template <class E>
using underlying_enum_type = typename std::underlying_type<E>::type;

template <class E, class Res = E>
using enable_if_scoped_enum =
    typename std::enable_if<is_scoped_enum<E>::value, Res>::type;

template <class E>
enable_if_scoped_enum<E> operator|(E l, E r) {
    return static_cast<E>(static_cast<underlying_enum_type<E>>(l)
                          | static_cast<underlying_enum_type<E>>(r));
}

template <class E>
enable_if_scoped_enum<E> operator&(E l, E r) {
    return static_cast<E>(static_cast<underlying_enum_type<E>>(l)
                          & static_cast<underlying_enum_type<E>>(r));
}

template <class E>
enable_if_scoped_enum<E> operator~(E e) {
    return static_cast<E>(~static_cast<underlying_enum_type<E>>(e));
}

template <class E>
enable_if_scoped_enum<E> operator|=(E& l, E r) {
    return l = (l | r);
}

template <class E>
enable_if_scoped_enum<E> operator&=(E& l, E r) {
    return l = (l & r);
}

template <class E>
enable_if_scoped_enum<E, bool> operator!(E e) {
    return !static_cast<underlying_enum_type<E>>(e);
}

template <class E>
enable_if_scoped_enum<E, bool> operator!=(E e, int val) {
    return static_cast<underlying_enum_type<E>>(e) !=
            static_cast<underlying_enum_type<E>>(val);
}

template <class E>
enable_if_scoped_enum<E, bool> operator!=(int val, E e) {
    return e != val;
}

template <class E>
enable_if_scoped_enum<E, bool> operator==(E e, int val) {
    return static_cast<underlying_enum_type<E>>(e) ==
            static_cast<underlying_enum_type<E>>(val);
}

template <class E>
enable_if_scoped_enum<E, bool> operator==(int val, E e) {
    return e == val;
}

template <class E>
enable_if_scoped_enum<E, bool> nonzero(E e) {
    return static_cast<underlying_enum_type<E>>(e) != 0;
}

}  // namespace EnumFlags

// For the ADL to kick in let's make sure we bring all the operators into our
// main AndroidEmu namespaces...
using namespace ::android::base::EnumFlags;

}  // namespace base

using namespace ::android::base::EnumFlags;

}  // namespace android

// ... and into the global one, where most of the client functions are
using namespace ::android::base::EnumFlags;
