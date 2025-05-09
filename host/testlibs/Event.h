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

#ifndef SAMPLE_UTIL_EVENT_H
#define SAMPLE_UTIL_EVENT_H

#include "keyboard.h"
#include "mouse.h"

class Event
{
  public:
    struct MoveEvent
    {
        int x;
        int y;
    };

    struct SizeEvent
    {
        int width;
        int height;
    };

    struct KeyEvent
    {
        Key code;
        bool alt;
        bool control;
        bool shift;
        bool system;
    };

    struct MouseMoveEvent
    {
        int x;
        int y;
    };

    struct MouseButtonEvent
    {
        MouseButton button;
        int x;
        int y;
    };

    struct MouseWheelEvent
    {
        int delta;
    };

    enum EventType
    {
        EVENT_CLOSED,                // The window requested to be closed
        EVENT_MOVED,                 // The window has moved
        EVENT_RESIZED,               // The window was resized
        EVENT_LOST_FOCUS,            // The window lost the focus
        EVENT_GAINED_FOCUS,          // The window gained the focus
        EVENT_TEXT_ENTERED,          // A character was entered
        EVENT_KEY_PRESSED,           // A key was pressed
        EVENT_KEY_RELEASED,          // A key was released
        EVENT_MOUSE_WHEEL_MOVED,     // The mouse wheel was scrolled
        EVENT_MOUSE_BUTTON_PRESSED,  // A mouse button was pressed
        EVENT_MOUSE_BUTTON_RELEASED, // A mouse button was released
        EVENT_MOUSE_MOVED,           // The mouse cursor moved
        EVENT_MOUSE_ENTERED,         // The mouse cursor entered the area of the window
        EVENT_MOUSE_LEFT,            // The mouse cursor left the area of the window
        EVENT_TEST,                  // Event for testing purposes
    };

    EventType type;

    union
    {
        MoveEvent               move;               // Move event parameters
        SizeEvent               size;               // Size event parameters
        KeyEvent                key;                // Key event parameters
        MouseMoveEvent          mouseMove;          // Mouse move event parameters
        MouseButtonEvent        mouseButton;        // Mouse button event parameters
        MouseWheelEvent         mouseWheel;         // Mouse wheel event parameters
    };
};

#endif // SAMPLE_UTIL_EVENT_H
