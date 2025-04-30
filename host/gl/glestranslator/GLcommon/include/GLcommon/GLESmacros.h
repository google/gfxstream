#ifndef GLES_MACROS_H
#define GLES_MACROS_H

#if defined(__linux__) || defined(__APPLE__)

#include <mutex>
#include "gfxstream/files/PathUtils.h"
#include "gfxstream/memory/MemoryTracker.h"

#if 0

#define MEM_TRACE_IF(condition, group)                                  \
    if ((condition)) {                                                  \
        static std::once_flag once_flag;                                \
        const std::string func(__FUNCTION__);                           \
        std::call_once(once_flag, [&func]() {                           \
            if (gfxstream::base::MemoryTracker::get()) {                            \
                std::string baseName; \
                gfxstream::base::PathUtils::split(std::string(__FILE__), NULL, &baseName); \
                gfxstream::base::MemoryTracker::get()->addToGroup(                  \
                        group, baseName.r() + ":" + func);            \
            }                                                           \
        });                                                             \
    }
#else

#define MEM_TRACE_IF(condition, group)

#endif

#else
// windows
#define MEM_TRACE_IF(condition, group)
#endif

#define MEM_TRACE(group) MEM_TRACE_IF(true, group)

#define FAIL_IF(condition, description) if((condition)) {                                      \
        fprintf(stderr, "%s:%s:%d error %s\n", __FILE__, __FUNCTION__, __LINE__, description); \
        return;                                                                                \
    }

#define RET_AND_FAIL_IF(condition, description, ret) if((condition)) {                         \
        fprintf(stderr, "%s:%s:%d error %s\n", __FILE__, __FUNCTION__, __LINE__, description); \
        return ret;                                                                            \
    }

#define GET_CTX()                                                             \
    MEM_TRACE_IF(strncmp(__FUNCTION__, "gl", 2) == 0, "EMUGL")                \
    FAIL_IF(!s_eglIface, "null s_eglIface")                                   \
    GLEScontext* ctx = s_eglIface->getGLESContext();                          \
    FAIL_IF(!ctx, "null ctx")

#define GET_CTX_CM()                                                          \
    MEM_TRACE_IF(strncmp(__FUNCTION__, "gl", 2) == 0, "EMUGL")                \
    FAIL_IF(!s_eglIface, "null s_eglIface")                                   \
    GLEScmContext* ctx =                                                      \
            static_cast<GLEScmContext*>(s_eglIface->getGLESContext());        \
    FAIL_IF(!ctx, "null ctx")

#define GET_CTX_V2()                                                          \
    MEM_TRACE_IF(strncmp(__FUNCTION__, "gl", 2) == 0, "EMUGL")                \
    FAIL_IF(!s_eglIface, "null s_eglIface")                                   \
    GLESv2Context* ctx =                                                      \
            static_cast<GLESv2Context*>(s_eglIface->getGLESContext());        \
    FAIL_IF(!ctx, "null ctx")

#define GET_CTX_RET(failure_ret)                                              \
    MEM_TRACE_IF(strncmp(__FUNCTION__, "gl", 2) == 0, "EMUGL")                \
    RET_AND_FAIL_IF(!s_eglIface, "null s_eglIface", failure_ret)              \
    GLEScontext* ctx = s_eglIface->getGLESContext();                          \
    RET_AND_FAIL_IF(!ctx, "null ctx", failure_ret)

#define GET_CTX_CM_RET(failure_ret)                                           \
    MEM_TRACE_IF(strncmp(__FUNCTION__, "gl", 2) == 0, "EMUGL")                \
    RET_AND_FAIL_IF(!s_eglIface, "null s_eglIface", failure_ret)              \
    GLEScmContext* ctx =                                                      \
            static_cast<GLEScmContext*>(s_eglIface->getGLESContext());        \
    RET_AND_FAIL_IF(!ctx, "null ctx", failure_ret)

#define GET_CTX_V2_RET(failure_ret)                                           \
    MEM_TRACE_IF(strncmp(__FUNCTION__, "gl", 2) == 0, "EMUGL")                \
    RET_AND_FAIL_IF(!s_eglIface, "null s_eglIface", failure_ret)              \
    GLESv2Context* ctx =                                                      \
            static_cast<GLESv2Context*>(s_eglIface->getGLESContext());        \
    RET_AND_FAIL_IF(!ctx, "null ctx", failure_ret)

#define SET_ERROR_IF(condition,err) if((condition)) {                            \
                        fprintf(stderr, "%s:%s:%d error 0x%x\n", __FILE__, __FUNCTION__, __LINE__, err); \
                        ctx->setGLerror(err);                                    \
                        return;                                                  \
                    }


#define RET_AND_SET_ERROR_IF(condition,err,ret) if((condition)) {                \
                        fprintf(stderr, "%s:%s:%d error 0x%x\n", __FILE__, __FUNCTION__, __LINE__, err); \
                        ctx->setGLerror(err);                                    \
                        return ret;                                              \
                    }

#define SET_ERROR_IF_DISPATCHER_NOT_SUPPORT(func) \
            SET_ERROR_IF(!ctx->dispatcher().func, GL_INVALID_OPERATION)

#define RET_AND_SET_ERROR_IF_DISPATCHER_NOT_SUPPORT(func, ret) \
            RET_AND_SET_ERROR_IF(!ctx->dispatcher().func, GL_INVALID_OPERATION, ret)

// Define the following flag to work around cocos2d rendering bug
// BUG: 119568237
#define TOLERATE_PROGRAM_LINK_ERROR 1

#endif
