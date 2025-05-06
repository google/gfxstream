#ifndef GLES_MACROS_H
#define GLES_MACROS_H

#define FAIL_IF(condition, description) if((condition)) {                                      \
        fprintf(stderr, "%s:%s:%d error %s\n", __FILE__, __FUNCTION__, __LINE__, description); \
        return;                                                                                \
    }

#define RET_AND_FAIL_IF(condition, description, ret) if((condition)) {                         \
        fprintf(stderr, "%s:%s:%d error %s\n", __FILE__, __FUNCTION__, __LINE__, description); \
        return ret;                                                                            \
    }

#define GET_CTX()                                                             \
    FAIL_IF(!s_eglIface, "null s_eglIface")                                   \
    GLEScontext* ctx = s_eglIface->getGLESContext();                          \
    FAIL_IF(!ctx, "null ctx")

#define GET_CTX_CM()                                                          \
    FAIL_IF(!s_eglIface, "null s_eglIface")                                   \
    GLEScmContext* ctx =                                                      \
            static_cast<GLEScmContext*>(s_eglIface->getGLESContext());        \
    FAIL_IF(!ctx, "null ctx")

#define GET_CTX_V2()                                                          \
    FAIL_IF(!s_eglIface, "null s_eglIface")                                   \
    GLESv2Context* ctx =                                                      \
            static_cast<GLESv2Context*>(s_eglIface->getGLESContext());        \
    FAIL_IF(!ctx, "null ctx")

#define GET_CTX_RET(failure_ret)                                              \
    RET_AND_FAIL_IF(!s_eglIface, "null s_eglIface", failure_ret)              \
    GLEScontext* ctx = s_eglIface->getGLESContext();                          \
    RET_AND_FAIL_IF(!ctx, "null ctx", failure_ret)

#define GET_CTX_CM_RET(failure_ret)                                           \
    RET_AND_FAIL_IF(!s_eglIface, "null s_eglIface", failure_ret)              \
    GLEScmContext* ctx =                                                      \
            static_cast<GLEScmContext*>(s_eglIface->getGLESContext());        \
    RET_AND_FAIL_IF(!ctx, "null ctx", failure_ret)

#define GET_CTX_V2_RET(failure_ret)                                           \
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
