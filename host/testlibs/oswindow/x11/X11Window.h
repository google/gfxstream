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

// X11Window.h: Definition of the implementation of OSWindow for X11

#ifndef UTIL_X11_WINDOW_H
#define UTIL_X11_WINDOW_H

#include <string>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xresource.h>

#include "gfxstream/host/testing/OSWindow.h"

class X11Window : public OSWindow
{
  public:
    X11Window();
    X11Window(int visualId);
    ~X11Window();

    bool initialize(const std::string &name, size_t width, size_t height) override;
    void destroy() override;

    EGLNativeWindowType getNativeWindow() const override;
    EGLNativeDisplayType getNativeDisplay() const override;
    void* getFramebufferNativeWindow() const override;

    void messageLoop() override;

    void setMousePosition(int x, int y) override;
    bool setPosition(int x, int y) override;
    bool resize(int width, int height) override;
    void setVisible(bool isVisible) override;

    void signalTestEvent() override;

  private:
    void processEvent(const XEvent &event);

    Atom WM_DELETE_WINDOW;
    Atom WM_PROTOCOLS;
    Atom TEST_EVENT;

    Display *mDisplay;
    Window mWindow;
    int mRequestedVisualId;
    bool mVisible;
};

#endif // UTIL_X11_WINDOW_H
