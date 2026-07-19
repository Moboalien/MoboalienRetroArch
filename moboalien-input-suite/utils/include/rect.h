#pragma once

namespace Utils {
    struct Rect {
    int left, top, right, bottom;
    Rect(int left = 0, int top = 0, int right = 0, int bottom = 0)
        : left(left), top(top), right(right), bottom(bottom) {}
    };
}