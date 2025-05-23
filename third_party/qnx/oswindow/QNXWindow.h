//
// Copyright (c) 2015 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Copyright (c) 2023 BlackBerry Limited
//

// QNXWindow.h: Definition of the implementation of OSWindow for QNX

#ifndef UTIL_QNX_WINDOW_H
#define UTIL_QNX_WINDOW_H

#include <screen/screen.h>

#include <string>

#include "OSWindow.h"

class QNXWindow : public OSWindow {
   public:
    QNXWindow();
    ~QNXWindow();

    bool initialize(const std::string& name, size_t width, size_t height) override;
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
    void processMouseEvent(const screen_event_t& event);
    void processKeyEvent(const screen_event_t& event);
    void processPropertyChangedEvent(const screen_event_t& event);
    void processInputControlEvent(const screen_event_t& event);
    void processCloseEvent(const screen_event_t& event);

    screen_window_t mWindow;
    bool mVisible;
    pid_t mPid;
};

#endif  // UTIL_QNX_WINDOW_H
