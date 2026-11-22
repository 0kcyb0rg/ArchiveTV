package com.okcyborg.archivetv;

import android.view.KeyEvent;

/** Handheld buttons, the same names as on the Miyoo. */
final class Keys {
    static final int NONE = 0, UP = 1, DOWN = 2, LEFT = 3, RIGHT = 4, A = 5, B = 6, X = 7, Y = 8,
            L1 = 9, R1 = 10, L2 = 11, R2 = 12, START = 13, SELECT = 14, MENU = 15;
    static boolean swapAB = false;

    static int map(int code) {
        int k;
        switch (code) {
            case KeyEvent.KEYCODE_DPAD_UP: return UP;
            case KeyEvent.KEYCODE_DPAD_DOWN: return DOWN;
            case KeyEvent.KEYCODE_DPAD_LEFT: return LEFT;
            case KeyEvent.KEYCODE_DPAD_RIGHT: return RIGHT;
            case KeyEvent.KEYCODE_BUTTON_A: case KeyEvent.KEYCODE_DPAD_CENTER: case KeyEvent.KEYCODE_ENTER: k = A; break;
            case KeyEvent.KEYCODE_BUTTON_B: case KeyEvent.KEYCODE_BACK: case KeyEvent.KEYCODE_ESCAPE: k = B; break;
            case KeyEvent.KEYCODE_BUTTON_X: return X;
            case KeyEvent.KEYCODE_BUTTON_Y: return Y;
            case KeyEvent.KEYCODE_BUTTON_L1: return L1;
            case KeyEvent.KEYCODE_BUTTON_R1: return R1;
            case KeyEvent.KEYCODE_BUTTON_L2: return L2;
            case KeyEvent.KEYCODE_BUTTON_R2: return R2;
            case KeyEvent.KEYCODE_BUTTON_START: return START;
            case KeyEvent.KEYCODE_BUTTON_SELECT: return SELECT;
            case KeyEvent.KEYCODE_BUTTON_MODE: case KeyEvent.KEYCODE_MENU: return MENU;
            default: return NONE;
        }
        if (swapAB && code != KeyEvent.KEYCODE_BACK && code != KeyEvent.KEYCODE_ENTER) k = (k == A) ? B : A;
        return k;
    }
}
