# Copyright (C) 2025 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

custom_preprocesses = {
"glBindVertexBuffer" : """
    ctx->bindIndexedBuffer(0, bindingindex, buffer, offset, 0, stride);
""",

"glVertexAttribBinding" : """
    ctx->setVertexAttribBindingIndex(attribindex, bindingindex);
""",

"glVertexAttribFormat" : """
    ctx->setVertexAttribFormat(attribindex, size, type, normalized, relativeoffset, false);
""",

"glVertexAttribIFormat" : """
    ctx->setVertexAttribFormat(attribindex, size, type, GL_FALSE, relativeoffset, true);
""",

"glVertexBindingDivisor" : """
    ctx->setVertexAttribDivisor(bindingindex, divisor);
""",

"glTexStorage2DMultisample" : """
    GLint err = GL_NO_ERROR;
    GLenum format, type;
    GLESv2Validate::getCompatibleFormatTypeForInternalFormat(internalformat, &format, &type);
    sPrepareTexImage2D(target, 0, (GLint)internalformat, width, height, 0, format, type, NULL, &type, (GLint*)&internalformat, &err);
    SET_ERROR_IF(err != GL_NO_ERROR, err);
""",
}

custom_postprocesses = {
}

custom_share_processing = {
}

no_passthrough = {
}