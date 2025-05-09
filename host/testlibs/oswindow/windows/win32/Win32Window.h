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

// Win32Window.h: Definition of the implementation of OSWindow for Win32 (Windows)

#ifndef UTIL_WIN32_WINDOW_H
#define UTIL_WIN32_WINDOW_H

#include <windows.h>
#include <string>

#include "gfxstream/host/testing/OSWindow.h"
#include "gfxstream/host/testing/Timer.h"

class Win32Window : public OSWindow
{
  public:
    Win32Window();
    ~Win32Window() override;

    bool initialize(const std::string &name, size_t width, size_t height) override;
    void destroy() override;

    bool takeScreenshot(uint8_t *pixelData) override;

    EGLNativeWindowType getNativeWindow() const override;
    EGLNativeDisplayType getNativeDisplay() const override;
    void* getFramebufferNativeWindow() const override;

    void messageLoop() override;

    void pushEvent(Event event) override;

    void setMousePosition(int x, int y) override;
    bool setPosition(int x, int y) override;
    bool resize(int width, int height) override;
    void setVisible(bool isVisible) override;

    void signalTestEvent() override;

  private:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    std::string mParentClassName;
    std::string mChildClassName;

    bool mIsVisible;
    Timer *mSetVisibleTimer;

    bool mIsMouseInWindow;

    EGLNativeWindowType mNativeWindow;
    EGLNativeWindowType mParentWindow;
    EGLNativeDisplayType mNativeDisplay;
};

#endif  // UTIL_WIN32_WINDOW_H
