#include "bitmap_differ.h"
#include "screenshare_protocol.h" // Updated include
#include "utils.h"

BitmapDiffer::BitmapDiffer()
    : m_prevData(NULL), m_dibWidth(0), m_dibHeight(0), m_frameCount(0)
{
}

BitmapDiffer::~BitmapDiffer() {
    if (m_prevData) delete[] m_prevData;
}

void BitmapDiffer::Reset() {
    m_frameCount = 0;
    if (m_prevData) { delete[] m_prevData; m_prevData = NULL; }
}

DiffResult BitmapDiffer::Compare(const ImageUtils::RawImageFrame& newFrame) {
    DiffResult result;
    result.x = result.y = result.width = result.height = 0;
    result.hasChanges = false;
    result.type = Screenshare::FRAME_TYPE_DIFF; // differential by default

    if (!newFrame.data) return result;

    // Allocate buffers if first frame or size changes
    if (m_frameCount == 0 || m_dibWidth != newFrame.width || m_dibHeight != newFrame.height) {
        if (m_prevData) delete[] m_prevData;

        m_dibWidth = newFrame.width;
        m_dibHeight = newFrame.height;
        size_t bufferSize = (size_t)m_dibWidth * m_dibHeight * 4;
        m_prevData = new unsigned char[bufferSize];

        // Since this is the first/new-sized frame, copy the whole thing
        if (newFrame.stride == m_dibWidth * 4) {
            memcpy(m_prevData, newFrame.data, bufferSize);
        } else { // Handle differing strides
            for (int y = 0; y < m_dibHeight; ++y) {
                memcpy(m_prevData + (size_t)y * m_dibWidth * 4, newFrame.data + (size_t)y * newFrame.stride, (size_t)m_dibWidth * 4);
            }
        }

        m_frameCount++;
        result.hasChanges = true;
        result.type = Screenshare::FRAME_TYPE_FULL; // full frame
        result.x = 0;
        result.y = 0;
        result.width = m_dibWidth;
        result.height = m_dibHeight;
        return result;
    }

    int width = m_dibWidth;
    int height = m_dibHeight;
    int left = width;
    int right = 0;
    int top = height;
    int bottom = 0;

    const int* pNew = (const int*)newFrame.data;
    const int* pPrev = (const int*)m_prevData;

    // First Pass - Find the left and top bounds
    for (int y = 0; y < height; ++y) {
        const int* pNewRow = pNew + y * (newFrame.stride / 4);
        const int* pPrevRow = pPrev + y * width;

        // Optimization: Use memcmp to quickly check if the entire row has changed.
        // We only need to check up to the current 'left' boundary.
        if (memcmp_SIMD(pNewRow, pPrevRow, left * 4) == 0) {
            continue; // This row is clean up to the current left boundary, skip it.
        }

        for (int x = 0; x < left; ++x) {
            // Use newFrame.stride for pNew, and width for the tightly packed pPrev
            if (pNewRow[x] != pPrevRow[x]) {
                top = (y < top) ? y : top;
                left = x;
                break; // Found the new leftmost boundary for this row, can stop scanning.
            }
        }
    }

    // If we did not find any changed pixels
    if (left == width) {
        result.hasChanges = false;
        m_frameCount++;
        return result;
    }

    // Initialize bottom and right to the boundaries we've already found.
    // The second pass will expand them outwards.
    bottom = top;
    right = left;

    // Second Pass
    for (int y = height - 1; y >= top; y--) {
        const int* pNewRow = pNew + y * (newFrame.stride / 4);
        const int* pPrevRow = pPrev + y * width;

        // Optimization: Use memcmp to quickly check the right side of the row.
        // We only need to check from the current 'right' boundary to the edge.
        if (memcmp_SIMD(pNewRow + right, pPrevRow + right, (width - right) * 4) == 0) {
            continue; // This part of the row is clean, skip the detailed check.
        }

        for (int x = width - 1; x > right; x--) {
            // Use newFrame.stride for pNew, and width for the tightly packed pPrev
            if (pNewRow[x] != pPrevRow[x]) {
                if (y > bottom) bottom = y; // Find the maximum y
                // We are looking for the rightmost change.
                right = x;
                break; // Found the rightmost change for this row.
            }
        }
    }

    result.hasChanges = true;
    result.type = Screenshare::FRAME_TYPE_DIFF; // differential
    result.x = left;
    result.y = top;
    result.width  = right - left + 1;
    result.height = bottom - top + 1;

    // Copy current frame to previous
    size_t bufferSize = (size_t)m_dibWidth * m_dibHeight * 4;
    if (newFrame.stride == m_dibWidth * 4) {
        memcpy(m_prevData, newFrame.data, bufferSize);
    } else {
        for (int y = 0; y < m_dibHeight; ++y) {
            memcpy(m_prevData + (size_t)y * m_dibWidth * 4, newFrame.data + (size_t)y * newFrame.stride, (size_t)m_dibWidth * 4);
        }
    }

    m_frameCount++;
    return result;
}
