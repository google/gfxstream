/*
* Copyright (C) 2011 The Android Open Source Project
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
#import <QuartzCore/CALayer.h>
#import <QuartzCore/CAMetalLayer.h>


#include "native_sub_window.h"
#include <Cocoa/Cocoa.h>

/*
 * EmuGLView inherit from NSOpenGLView and override the isOpaque
 * method to return YES. That prevents drawing of underlying window/view
 * when the view needs to be redrawn.
 */
@interface EmuGLView : NSOpenGLView {
} @end

@implementation EmuGLView

  - (BOOL)isOpaque {
      return YES;
  }

@end

@interface EmuGLViewWithMetal : NSOpenGLView

- (CAMetalLayer *)getMetalLayer;

@end


@implementation EmuGLViewWithMetal

  - (BOOL)isOpaque {
      return YES;
  }

  - (CALayer *)makeBackingLayer {
    CALayer * layer = [CAMetalLayer layer];
    return layer;
  }

  - (CAMetalLayer *)getMetalLayer {
      // The 'layer' property on NSView returns a CALayer. We can safely cast it
      // to CAMetalLayer because we configured the view to use it.
      return (CAMetalLayer *)self.layer;
  }
@end

EGLNativeWindowType createSubWindow(FBNativeWindowType p_window,
                                    int x,
                                    int y,
                                    int width,
                                    int height,
                                    float dpr,
                                    SubWindowRepaintCallback repaint_callback,
                                    void* repaint_callback_param,
                                    int hideWindow) {
    NSWindow* win = (NSWindow *)p_window;
    if (!win) {
        return NULL;
    }

    /* (x,y) assume an upper-left origin, but Cocoa uses a lower-left origin */
    NSRect content_rect = [win contentRectForFrameRect:[win frame]];
    int cocoa_y = (int)content_rect.size.height - (y + height);
    NSRect contentRect = NSMakeRect(x, cocoa_y, width, height);

    // Enable views with metal backing when hardware acceleration is enabled, as it should provide
    // better performance and can be necessary for vulkan swapchain creation.
    bool useMetalView = true;
    const char* forceMetalView = getenv("ANDROID_EMU_USE_METAL_BACKED_VIEWS");
    if (forceMetalView && (0 == strcmp("0", forceMetalView))) {
        // Enable enabling the option using an environment variable
        useMetalView = false;
    }

    NSView* glView = NULL;
    if (useMetalView) {
        glView = [[EmuGLViewWithMetal alloc] initWithFrame:contentRect];
    } else {
        glView = [[EmuGLView alloc] initWithFrame:contentRect pixelFormat:nil];
    }
    if (!glView) {
        return NULL;
    }
    if (!useMetalView) {
        // This is deprecated since macOS 10.15, but is still required for high-resolution
        // OpenGL surfaces if not using Metal.
        [glView setWantsBestResolutionOpenGLSurface:YES];
    }
    [glView setWantsLayer:YES];
    [[win contentView] addSubview:glView];
    [win makeKeyAndOrderFront:nil];
    if (hideWindow) {
        [glView setHidden:YES];
    }
    // We cannot use the dpr from [NSScreen mainScreen], which usually
    // gives the wrong screen at this point.
    [glView.layer setContentsScale:dpr];
    return (EGLNativeWindowType)(glView);
}

void destroySubWindow(EGLNativeWindowType win) {
    if(win){
        NSView *glView = (NSView *)win;
        [glView removeFromSuperview];
        [glView release];
    }
}

int moveSubWindow(FBNativeWindowType p_parent_window,
                  EGLNativeWindowType p_sub_window,
                  int x,
                  int y,
                  int width,
                  int height,
                  float dpr) {
    NSWindow *win = (NSWindow *)p_parent_window;
    if (!win) {
        return 0;
    }

    NSView *glView = (NSView *)p_sub_window;
    if (!glView) {
        return 0;
    }

    /* The view must be removed from the hierarchy to be properly resized */
    [glView removeFromSuperview];

    /* (x,y) assume an upper-left origin, but Cocoa uses a lower-left origin */
    NSRect content_rect = [win contentRectForFrameRect:[win frame]];
    int cocoa_y = (int)content_rect.size.height - (y + height);
    NSRect newFrame = NSMakeRect(x, cocoa_y, width, height);
    [glView setFrame:newFrame];

    /* Re-add the sub-window to the view hierarchy */
    [[win contentView] addSubview:glView];

    return 1;
}

void* getNativeDisplay() {
    fprintf(stderr, "%s: Unimplemented\n", __func__);
    return nullptr;
}

// Retrieve metal layer from the view, to create a swapchain surface
// To be used with VK_EXT_metal_surface
void* getMetalLayerFromView(void* view) {
    EmuGLViewWithMetal* metalView = (EmuGLViewWithMetal*)view;
    if (!metalView) {
        return nil;
    }
    return [metalView getMetalLayer];
}
