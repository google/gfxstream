#include "GLSnapshotTestDispatch.h"

#include "FrameBuffer.h"
#include "GLSnapshotTesting.h"
#include "GLTestUtils.h"
#include "RenderThreadInfo.h"
#include "apigen-codec-common/glUtils.h"
#include "gfxstream/files/PathUtils.h"
#include "gfxstream/system/System.h"
#include "render-utils/snapshot_operations.h"

namespace gfxstream {
namespace gl {

static SnapshotTestDispatch* sSnapshotTestDispatch() {
    static SnapshotTestDispatch* s = new SnapshotTestDispatch;
    return s;
}

// static
const GLESv2Dispatch* getSnapshotTestDispatch() {
    return sSnapshotTestDispatch();
}

SnapshotTestDispatch::SnapshotTestDispatch() {
    mValid = gles2_dispatch_init(this);
    if (mValid) {
        overrideFunctions();
    } else {
        fprintf(stderr, "SnapshotTestDispatch failed to initialize.\n");
        ADD_FAILURE() << "SnapshotTestDispatch could not initialize.";
    }
}

void SnapshotTestDispatch::overrideFunctions() {
    this->glDrawArrays = (glDrawArrays_t)test_glDrawArrays;
    this->glDrawElements = (glDrawElements_t)test_glDrawElements;
}

void SnapshotTestDispatch::saveSnapshot() {
    FrameBuffer* fb = FrameBuffer::getFB();
    if (!fb) {
        FAIL() << "Could not get FrameBuffer during snapshot test.";
    }


    mStream = std::make_unique<gfxstream::MemStream>();
    mTextureSaverLoader = std::make_shared<InMemoryTextureSaverLoader>();

    fb->onSave(mStream.get(), mTextureSaverLoader);

    // Save thread's context and surface handles so we can restore the bind
    // after load is complete.
    RenderThreadInfo* threadInfo = RenderThreadInfo::get();
    if (threadInfo) {
        threadInfo->onSave(mStream.get());
    }
}

void SnapshotTestDispatch::loadSnapshot() {
    FrameBuffer* fb = FrameBuffer::getFB();
    if (!fb) {
        FAIL() << "Could not get FrameBuffer during snapshot test.";
    }


    // unbind so load will destroy previous objects
    fb->bindContext(0, 0, 0);

    gfxstream::MemStream loadStream(gfxstream::MemStream::Buffer(mStream->buffer()));

    fb->onLoad(&loadStream, mTextureSaverLoader);

    RenderThreadInfo* threadInfo = RenderThreadInfo::get();
    if (threadInfo) {
        threadInfo->onLoad(&loadStream);
        // rebind to context
        fb->bindContext(
                threadInfo->m_glInfo->currContext ? threadInfo->m_glInfo->currContext->getHndl()
                                        : 0,
                threadInfo->m_glInfo->currDrawSurf ? threadInfo->m_glInfo->currDrawSurf->getHndl()
                                         : 0,
                threadInfo->m_glInfo->currReadSurf ? threadInfo->m_glInfo->currReadSurf->getHndl()
                                         : 0);
    }

    mTextureSaverLoader->join();

    mLoadCount++;
}

// static
void SnapshotTestDispatch::testDraw(std::function<void()> doDraw) {
    const GLESv2Dispatch* gl = LazyLoadedGLESv2Dispatch::get();
    ASSERT_NE(nullptr, gl);

    FrameBuffer* fb = FrameBuffer::getFB();
    if (!fb) {
        ADD_FAILURE() << "No framebuffer, test cannot snapshot.";
        doDraw();
        return;
    }

    // save then draw
    ((SnapshotTestDispatch*)getSnapshotTestDispatch())->saveSnapshot();
    // Since current framebuffer contents are not saved, we need to draw
    // onto a clean slate in order to check the result of the draw call
    gl->glClear(GL_COLOR_BUFFER_BIT);
    doDraw();

    // save the framebuffer contents
    GLuint width, height, bytesPerPixel;
    width = fb->getWidth();
    height = fb->getHeight();
    bytesPerPixel = glUtilsPixelBitSize(GL_RGBA, GL_UNSIGNED_BYTE) / 8;
    std::vector<GLubyte> prePixels = {};
    prePixels.resize(width * height * bytesPerPixel);
    gl->glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE,
                     prePixels.data());

    // To verify that the snapshot is restoring our context, we modify the
    // clear color.
    std::vector<GLfloat> oldClear = {};
    oldClear.resize(4);
    gl->glGetFloatv(GL_COLOR_CLEAR_VALUE, oldClear.data());
    EXPECT_TRUE(compareGlobalGlFloatv(gl, GL_COLOR_CLEAR_VALUE, oldClear));
    gl->glClearColor(1, 1, 1, 1);
    gl->glClear(GL_COLOR_BUFFER_BIT);
    EXPECT_TRUE(compareGlobalGlFloatv(gl, GL_COLOR_CLEAR_VALUE, {1, 1, 1, 1}));

    // load and redraw
    ((SnapshotTestDispatch*)getSnapshotTestDispatch())->loadSnapshot();
    gl->glClear(GL_COLOR_BUFFER_BIT);
    doDraw();

    // check that clear is restored
    EXPECT_TRUE(compareGlobalGlFloatv(gl, GL_COLOR_CLEAR_VALUE, oldClear));

    // compare the framebuffer contents
    std::vector<GLubyte> postPixels = {};
    postPixels.resize(width * height * bytesPerPixel);
    gl->glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE,
                     postPixels.data());

    EXPECT_TRUE(ImageMatches(width, height, bytesPerPixel, width,
                             prePixels.data(), postPixels.data()));
}

}  // namespace gl
}  // namespace gfxstream
