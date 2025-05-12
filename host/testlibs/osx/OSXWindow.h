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

#ifndef UTIL_OSX_WINDOW_H_
#define UTIL_OSX_WINDOW_H_

#import <Cocoa/Cocoa.h>

#include "OSWindow.h"

class OSXWindow;

@interface WindowDelegate : NSObject
{
    OSXWindow *mWindow;
}
- (id) initWithWindow: (OSXWindow*) window;
@end

@interface ContentView : NSView
{
    OSXWindow *mWindow;
    NSTrackingArea *mTrackingArea;
    int mCurrentModifier;
}
- (id) initWithWindow: (OSXWindow*) window;
@end

class OSXWindow : public OSWindow
{
  public:
    OSXWindow();
    ~OSXWindow();

    bool initialize(const std::string &name, size_t width, size_t height) override;
    void destroy() override;

    EGLNativeWindowType getNativeWindow() const override;
    EGLNativeDisplayType getNativeDisplay() const override;
    void* getFramebufferNativeWindow() const override;
    float getDevicePixelRatio() const override;

    void messageLoop() override;

    void setMousePosition(int x, int y) override;
    bool setPosition(int x, int y) override;
    bool resize(int width, int height) override;
    void setVisible(bool isVisible) override;

    void signalTestEvent() override;

    NSWindow *getNSWindow() const;

  private:
    CALayer *mLayer;
    NSWindow *mWindow;
    WindowDelegate *mDelegate;
    ContentView *mView;
};

#endif // UTIL_OSX_WINDOW_H_
