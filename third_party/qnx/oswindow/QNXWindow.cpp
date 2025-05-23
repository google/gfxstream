//
// Copyright (c) 2015 The ANGLE Project Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Copyright (c) 2023 BlackBerry Limited
//

// QNXWindow.cpp: Implementation of OSWindow for QNX

#include "qnx/QNXWindow.h"

#include <assert.h>
#include <pthread.h>
#include <sys/keycodes.h>

#include "gfxstream/system/System.h"

namespace {
static pthread_once_t once_control = PTHREAD_ONCE_INIT;
static screen_context_t g_screen_ctx;
static int FAKE_MOUSE_BUTTON_FOR_TRIGGER_TEST_EVENT = 5;

static void screen_init(void) {
    /* initialize the global screen context */
    screen_create_context(&g_screen_ctx, SCREEN_APPLICATION_CONTEXT);
}

static screen_context_t get_screen_context() {
    pthread_once(&once_control, screen_init);
    return g_screen_ctx;
}

Key QNXCodeToKey(unsigned keycode) {
    switch (keycode) {
        case KEYCODE_RETURN:
        case KEYCODE_KP_ENTER:
            return KEY_RETURN;
        case KEYCODE_BACKSPACE:
            return KEY_BACK;
        case KEYCODE_DELETE:
            return KEY_DELETE;
        case KEYCODE_ESCAPE:
            return KEY_ESCAPE;
        case KEYCODE_SPACE:
            return KEY_SPACE;
        case KEYCODE_ZERO:
        case KEYCODE_RIGHT_PAREN:
            return KEY_NUM0;
        case KEYCODE_ONE:
        case KEYCODE_EXCLAM:
            return KEY_NUM1;
        case KEYCODE_TWO:
        case KEYCODE_AT:
            return KEY_NUM2;
        case KEYCODE_THREE:
        case KEYCODE_NUMBER:
            return KEY_NUM3;
        case KEYCODE_FOUR:
        case KEYCODE_DOLLAR:
            return KEY_NUM4;
        case KEYCODE_FIVE:
        case KEYCODE_PERCENT:
            return KEY_NUM5;
        case KEYCODE_SIX:
        case KEYCODE_CIRCUMFLEX:
            return KEY_NUM6;
        case KEYCODE_SEVEN:
        case KEYCODE_AMPERSAND:
            return KEY_NUM7;
        case KEYCODE_EIGHT:
        case KEYCODE_ASTERISK:
            return KEY_NUM8;
        case KEYCODE_NINE:
        case KEYCODE_LEFT_PAREN:
            return KEY_NUM9;
        case KEYCODE_A:
        case KEYCODE_CAPITAL_A:
            return KEY_A;
        case KEYCODE_B:
        case KEYCODE_CAPITAL_B:
            return KEY_B;
        case KEYCODE_C:
        case KEYCODE_CAPITAL_C:
            return KEY_C;
        case KEYCODE_D:
        case KEYCODE_CAPITAL_D:
            return KEY_D;
        case KEYCODE_E:
        case KEYCODE_CAPITAL_E:
            return KEY_E;
        case KEYCODE_F:
        case KEYCODE_CAPITAL_F:
            return KEY_F;
        case KEYCODE_G:
        case KEYCODE_CAPITAL_G:
            return KEY_G;
        case KEYCODE_H:
        case KEYCODE_CAPITAL_H:
            return KEY_H;
        case KEYCODE_I:
        case KEYCODE_CAPITAL_I:
            return KEY_I;
        case KEYCODE_J:
        case KEYCODE_CAPITAL_J:
            return KEY_J;
        case KEYCODE_K:
        case KEYCODE_CAPITAL_K:
            return KEY_K;
        case KEYCODE_L:
        case KEYCODE_CAPITAL_L:
            return KEY_L;
        case KEYCODE_M:
        case KEYCODE_CAPITAL_M:
            return KEY_M;
        case KEYCODE_N:
        case KEYCODE_CAPITAL_N:
            return KEY_N;
        case KEYCODE_O:
        case KEYCODE_CAPITAL_O:
            return KEY_O;
        case KEYCODE_P:
        case KEYCODE_CAPITAL_P:
            return KEY_P;
        case KEYCODE_Q:
        case KEYCODE_CAPITAL_Q:
            return KEY_Q;
        case KEYCODE_R:
        case KEYCODE_CAPITAL_R:
            return KEY_R;
        case KEYCODE_S:
        case KEYCODE_CAPITAL_S:
            return KEY_S;
        case KEYCODE_T:
        case KEYCODE_CAPITAL_T:
            return KEY_T;
        case KEYCODE_U:
        case KEYCODE_CAPITAL_U:
            return KEY_U;
        case KEYCODE_V:
        case KEYCODE_CAPITAL_V:
            return KEY_V;
        case KEYCODE_W:
        case KEYCODE_CAPITAL_W:
            return KEY_W;
        case KEYCODE_X:
        case KEYCODE_CAPITAL_X:
            return KEY_X;
        case KEYCODE_Y:
        case KEYCODE_CAPITAL_Y:
            return KEY_Y;
        case KEYCODE_Z:
        case KEYCODE_CAPITAL_Z:
            return KEY_Z;
        case KEYCODE_PLUS:
        case KEYCODE_EQUAL:
            return KEY_EQUAL;
        case KEYCODE_MINUS:
        case KEYCODE_UNDERSCORE:
            return KEY_SUBTRACT;
        case KEYCODE_LESS_THAN:
        case KEYCODE_COMMA:
            return KEY_COMMA;
        case KEYCODE_GREATER_THAN:
        case KEYCODE_PERIOD:
            return KEY_PERIOD;
        case KEYCODE_COLON:
        case KEYCODE_SEMICOLON:
            return KEY_SEMICOLON;
        case KEYCODE_SLASH:
        case KEYCODE_QUESTION:
            return KEY_SLASH;
        case KEYCODE_TILDE:
        case KEYCODE_GRAVE:
            return KEY_TILDE;
        case KEYCODE_LEFT_BRACE:
        case KEYCODE_LEFT_BRACKET:
            return KEY_LBRACKET;
        case KEYCODE_BAR:
        case KEYCODE_BACK_SLASH:
            return KEY_BACKSLASH;
        case KEYCODE_RIGHT_BRACE:
        case KEYCODE_RIGHT_BRACKET:
            return KEY_RBRACKET;
        case KEYCODE_QUOTE:
        case KEYCODE_APOSTROPHE:
            return KEY_QUOTE;
        case KEYCODE_PAUSE:
            return KEY_PAUSE;
        case KEYCODE_TAB:
        case KEYCODE_BACK_TAB:
            return KEY_TAB;
        case KEYCODE_LEFT:
            return KEY_LEFT;
        case KEYCODE_KP_LEFT:
            return KEY_NUMPAD4;
        case KEYCODE_KP_FIVE:
            return KEY_NUMPAD5;
        case KEYCODE_RIGHT:
            return KEY_RIGHT;
        case KEYCODE_KP_RIGHT:
            return KEY_NUMPAD6;
        case KEYCODE_UP:
            return KEY_UP;
        case KEYCODE_KP_UP:
            return KEY_NUMPAD8;
        case KEYCODE_DOWN:
            return (Key)(KEY_UP + 1);  // avoid name collision
        case KEYCODE_KP_DOWN:
            return KEY_NUMPAD2;
        case KEYCODE_MENU:
        case KEYCODE_LEFT_ALT:
        case KEYCODE_RIGHT_ALT:
            return KEY_MENU;
        case KEYCODE_HOME:
            return KEY_HOME;
        case KEYCODE_END:
            return KEY_END;
        case KEYCODE_LEFT_SHIFT:
            return KEY_LSHIFT;
        case KEYCODE_RIGHT_SHIFT:
            return KEY_RSHIFT;
        case KEYCODE_LEFT_CTRL:
            return KEY_LCONTROL;
        case KEYCODE_RIGHT_CTRL:
            return KEY_RCONTROL;
        case KEYCODE_KP_PLUS:
            return KEY_ADD;
        case KEYCODE_KP_MINUS:
            return KEY_SUBTRACT;
        case KEYCODE_KP_MULTIPLY:
            return KEY_MULTIPLY;
        case KEYCODE_KP_DIVIDE:
            return KEY_DIVIDE;
        case KEYCODE_F1:
            return KEY_F1;
        case KEYCODE_F2:
            return KEY_F2;
        case KEYCODE_F3:
            return KEY_F3;
        case KEYCODE_F4:
            return KEY_F4;
        case KEYCODE_F5:
            return KEY_F5;
        case KEYCODE_F6:
            return KEY_F6;
        case KEYCODE_F7:
            return KEY_F7;
        case KEYCODE_F8:
            return KEY_F8;
        case KEYCODE_F9:
            return KEY_F9;
        case KEYCODE_F10:
            return KEY_F10;
        case KEYCODE_F11:
            return KEY_F11;
        case KEYCODE_F12:
            return KEY_F12;
        default:
            return KEY_UNKNOWN;
    }
    return KEY_UNKNOWN;
}

MouseButton QNXCodeToButton(int button) {
    MouseButton target_button = MOUSEBUTTON_UNKNOWN;
    switch (button) {
        case SCREEN_LEFT_MOUSE_BUTTON:
            target_button = MOUSEBUTTON_LEFT;
            break;
        case SCREEN_MIDDLE_MOUSE_BUTTON:
            target_button = MOUSEBUTTON_MIDDLE;
            break;
        case SCREEN_RIGHT_MOUSE_BUTTON:
            target_button = MOUSEBUTTON_RIGHT;
            break;
        default:
            break;
    }
    return target_button;
}
}  // namespace

QNXWindow::QNXWindow() : mWindow(0), mVisible(false), mPid(getpid()) {}

QNXWindow::~QNXWindow() { destroy(); }

bool QNXWindow::initialize(const std::string& name, size_t width, size_t height) {
    screen_context_t screen_ctx;
    screen_window_t screen_window;
    int rc;

    screen_ctx = get_screen_context();
    if (screen_ctx == nullptr) {
        perror("No screen context");
        return false;
    }

    rc = screen_create_window_type(&screen_window, screen_ctx, SCREEN_APPLICATION_WINDOW);
    if (rc) {
        perror("screen_create_window");
        return false;
    }

    int alpha_mode = SCREEN_PRE_MULTIPLIED_ALPHA;
    screen_set_window_property_iv(screen_window, SCREEN_PROPERTY_ALPHA_MODE, &alpha_mode);

    int usage = SCREEN_USAGE_NATIVE | SCREEN_USAGE_OPENGL_ES2;
    screen_set_window_property_iv(screen_window, SCREEN_PROPERTY_USAGE, &usage);

    int interval = 1;
    screen_set_window_property_iv(screen_window, SCREEN_PROPERTY_SWAP_INTERVAL, &interval);

    int format = SCREEN_FORMAT_RGBA8888;
    screen_set_window_property_iv(screen_window, SCREEN_PROPERTY_FORMAT, &format);

    int transp = SCREEN_TRANSPARENCY_NONE;
    screen_set_window_property_iv(screen_window, SCREEN_PROPERTY_TRANSPARENCY, &transp);

    int pos[2] = {0, 0};
    screen_set_window_property_iv(screen_window, SCREEN_PROPERTY_POSITION, pos);

    int size[2] = {static_cast<int>(width), static_cast<int>(height)};
    screen_set_window_property_iv(screen_window, SCREEN_PROPERTY_SIZE, size);

    size[0] = width;
    size[1] = height;
    screen_set_window_property_iv(screen_window, SCREEN_PROPERTY_BUFFER_SIZE, size);

    rc = screen_create_window_buffers(screen_window, 2);
    if (rc) {
        perror("screen_create_window_buffers");
        screen_destroy_window(screen_window);
        return false;
    }

    char group_name[] = "gfx";
    rc = screen_create_window_group(screen_window, group_name);
    if (rc) {
        perror("screen_create_window_group");
        screen_destroy_window(screen_window);
        return false;
    }

    mWindow = screen_window;

    int sensitivity = SCREEN_SENSITIVITY_TEST;
    screen_set_window_property_iv(screen_window, SCREEN_PROPERTY_SENSITIVITY, &sensitivity);

    screen_set_window_property_cv(screen_window, SCREEN_PROPERTY_ID_STRING, name.size(),
                                  name.c_str());

    int visible = mVisible ? 1 : 0;
    screen_set_window_property_iv(screen_window, SCREEN_PROPERTY_VISIBLE, &visible);

    rc = screen_flush_context(screen_ctx, SCREEN_WAIT_IDLE);
    if (rc) {
        perror("flush");
    }

    return true;
}

void QNXWindow::destroy() {
    if (mWindow) screen_destroy_window(mWindow);
}

EGLNativeWindowType QNXWindow::getNativeWindow() const { return mWindow; }

EGLNativeDisplayType QNXWindow::getNativeDisplay() const {
    // TODO: yodai
    return 0;
}

void* QNXWindow::getFramebufferNativeWindow() const { return mWindow; }

void QNXWindow::messageLoop() {
    screen_event_t screen_event;
    int rc = screen_create_event(&screen_event);
    if (rc) {
        perror("screen_create_event");
        return;
    }

    while (!screen_get_event(get_screen_context(), screen_event, 0)) {
        int event_type;
        rc = screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_TYPE, &event_type);
        if (rc || event_type == SCREEN_EVENT_NONE) {
            break;
        }

        switch (event_type) {
            case SCREEN_EVENT_KEYBOARD:
                processKeyEvent(screen_event);
                break;
            case SCREEN_EVENT_POINTER:
                processMouseEvent(screen_event);
                break;
            case SCREEN_EVENT_PROPERTY:
                processPropertyChangedEvent(screen_event);
                break;
            case SCREEN_EVENT_INPUT_CONTROL:
                processInputControlEvent(screen_event);
                break;
            case SCREEN_EVENT_CLOSE:
                processCloseEvent(screen_event);
                break;
            default:
                break;
        }
    }
    screen_destroy_event(screen_event);
}

void QNXWindow::setMousePosition(int x, int y) {
    screen_event_t screen_event;
    if (screen_create_event(&screen_event)) return;

    int param = SCREEN_EVENT_POINTER;
    if (screen_set_event_property_iv(screen_event, SCREEN_PROPERTY_TYPE, &param)) return;

    if (screen_set_event_property_pv(screen_event, SCREEN_PROPERTY_WINDOW, (void**)&mWindow))
        return;
    int coords[] = {x, y};
    screen_set_event_property_iv(screen_event, SCREEN_PROPERTY_SOURCE_POSITION, coords);
    screen_send_event(get_screen_context(), screen_event, mPid);
}

OSWindow* CreateOSWindow() { return new QNXWindow(); }

bool QNXWindow::setPosition(int x, int y) {
    int pos[2] = {x, y};
    screen_set_window_property_iv(mWindow, SCREEN_PROPERTY_POSITION, pos);
    screen_context_t screen_ctx = get_screen_context();
    screen_flush_context(screen_ctx, 0);
    return true;
}

bool QNXWindow::resize(int width, int height) {
    int size[2] = {width, height};
    screen_set_window_property_iv(mWindow, SCREEN_PROPERTY_SIZE, size);
    size[0] = width;
    size[1] = height;
    screen_set_window_property_iv(mWindow, SCREEN_PROPERTY_BUFFER_SIZE, size);
    return true;
}

void QNXWindow::setVisible(bool isVisible) {
    if (mVisible == isVisible) {
        return;
    }
    int visible = isVisible ? 1 : 0;
    if (!screen_set_window_property_iv(mWindow, SCREEN_PROPERTY_VISIBLE, &visible))
        mVisible = isVisible;
}

void QNXWindow::signalTestEvent() {
    screen_event_t screen_event;
    if (screen_create_event(&screen_event)) return;

    int param = SCREEN_EVENT_POINTER;
    if (screen_set_event_property_iv(screen_event, SCREEN_PROPERTY_TYPE, &param)) return;

    if (screen_set_event_property_pv(screen_event, SCREEN_PROPERTY_WINDOW, (void**)&mWindow))
        return;
    int button = FAKE_MOUSE_BUTTON_FOR_TRIGGER_TEST_EVENT;
    screen_set_event_property_iv(screen_event, SCREEN_PROPERTY_BUTTONS, &button);
    screen_send_event(get_screen_context(), screen_event, mPid);
}

void QNXWindow::processMouseEvent(const screen_event_t& screen_event) {
    static int s_lastButtonState = 0;
    int buttonState = 0;
    screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_BUTTONS, &buttonState);
    Event event;
    if (buttonState == FAKE_MOUSE_BUTTON_FOR_TRIGGER_TEST_EVENT) {
        event.Type = Event::EVENT_TEST;
        pushEvent(event);
        return;
    }

    int lastButtonState = s_lastButtonState;

    s_lastButtonState = buttonState;
    int wheel_ticks = 0;
    screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_MOUSE_WHEEL, &wheel_ticks);
    if (wheel_ticks)
        event.Type = Event::EVENT_MOUSE_WHEEL_MOVED;
    else if (buttonState == lastButtonState)
        event.Type = Event::EVENT_MOUSE_MOVED;
    else if (buttonState > lastButtonState)
        event.Type = Event::EVENT_MOUSE_BUTTON_PRESSED;
    else
        event.Type = Event::EVENT_MOUSE_BUTTON_RELEASED;

    switch (event.Type) {
        case Event::EVENT_MOUSE_WHEEL_MOVED:
            event.MouseWheel.Delta = wheel_ticks;
            pushEvent(event);
            break;
        case Event::EVENT_MOUSE_MOVED:
            int position[2];
            screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_SOURCE_POSITION, position);
            event.MouseMove.X = position[0];
            event.MouseMove.Y = position[1];
            pushEvent(event);
            break;
        case Event::EVENT_MOUSE_BUTTON_RELEASED:
        case Event::EVENT_MOUSE_BUTTON_PRESSED:
            event.MouseButton.Button = event.Type == Event::EVENT_MOUSE_BUTTON_RELEASED
                                           ? QNXCodeToButton(lastButtonState)
                                           : QNXCodeToButton(buttonState);
            if (event.MouseButton.Button != MOUSEBUTTON_UNKNOWN) {
                // int position[2];
                screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_SOURCE_POSITION,
                                             position);
                event.MouseButton.X = position[0];
                event.MouseButton.Y = position[1];
                pushEvent(event);
            }
            break;
    }
}

void QNXWindow::processKeyEvent(const screen_event_t& screen_event) {
    int modifiers = 0;
    int flags = 0;
    int cap = 0;

    screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_MODIFIERS, &modifiers);
    screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_FLAGS, &flags);
    screen_get_event_property_iv(screen_event, SCREEN_PROPERTY_KEY_CAP, &cap);

    Event event;
    event.Key.Code = QNXCodeToKey(cap);
    event.Type = flags & KEY_DOWN || flags & KEY_REPEAT ? Event::EVENT_KEY_PRESSED
                                                        : Event::EVENT_KEY_RELEASED;
    event.Key.Shift = modifiers & KEYMOD_SHIFT;
    event.Key.Control = modifiers & KEYMOD_CTRL;
    event.Key.Alt = modifiers & KEYMOD_ALT;
    event.Key.System = 0;
    pushEvent(event);
}

void QNXWindow::processPropertyChangedEvent(const screen_event_t& event) {
    int property = 0;
    screen_get_event_property_iv(event, SCREEN_PROPERTY_NAME, &property);
    int type = 0;
    screen_get_event_property_iv(event, SCREEN_PROPERTY_OBJECT_TYPE, &type);
    if (type != SCREEN_OBJECT_TYPE_WINDOW) return;

    if (property == SCREEN_PROPERTY_SIZE) {
        screen_window_t screenWindow = 0;
        if (screen_get_event_property_pv(event, SCREEN_PROPERTY_WINDOW, (void**)&screenWindow))
            return;
        if (screenWindow != mWindow) return;

        int size[2] = {0, 0};
        if (screen_get_window_property_iv(screenWindow, SCREEN_PROPERTY_SIZE, size)) return;
        Event event;
        event.Type = Event::EVENT_RESIZED;
        event.Size.Width = size[0];
        event.Size.Height = size[1];
        pushEvent(event);
    } else if (property == SCREEN_PROPERTY_FOCUS) {
        screen_window_t screenWindow = 0;
        if (screen_get_event_property_pv(event, SCREEN_PROPERTY_WINDOW, (void**)&screenWindow))
            return;
        if (screenWindow != mWindow) return;
        int value;
        if (screen_get_window_property_iv(screenWindow, property, &value)) return;
        Event event;
        event.Type = value ? Event::EVENT_GAINED_FOCUS : Event::EVENT_LOST_FOCUS;
    }
}

void QNXWindow::processInputControlEvent(const screen_event_t& screen_event) {
    int val;
    if (screen_get_event_property_iv(screen_event, SCREEN_INPUT_CONTROL_POINTER_STOP, &val)) return;
    if (!val) return;
    screen_window_t screenWindow = 0;
    if (screen_get_event_property_pv(screen_event, SCREEN_PROPERTY_WINDOW, (void**)&screenWindow))
        return;
    if (screenWindow != mWindow) return;
    Event event;
    event.Type = Event::EVENT_MOUSE_LEFT;
    pushEvent(event);
}

void QNXWindow::processCloseEvent(const screen_event_t& screen_event) {
    screen_window_t screenWindow = 0;
    if (screen_get_event_property_pv(screen_event, SCREEN_PROPERTY_WINDOW, (void**)&screenWindow))
        return;
    if (screenWindow != mWindow) return;
    Event event;
    event.Type = Event::EVENT_CLOSED;
    pushEvent(event);
}
