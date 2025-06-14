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

// Generated Code - DO NOT EDIT !!
// generated by './scripts/generate-apigen-sources.sh'

#ifndef GUARD_gles2_decoder_context_t
#define GUARD_gles2_decoder_context_t

#include "gfxstream/host/ChecksumCalculator.h"
#include "gfxstream/host/iostream.h"
#include "gles2_server_context.h"



namespace gfxstream {

struct gles2_decoder_context_t : public gles2_server_context_t {

	size_t decode(void *buf, size_t bufsize, IOStream *stream, ChecksumCalculator* checksumCalc);

};

}  // namespace gfxstream

#endif  // GUARD_gles2_decoder_context_t
