#pragma once
#include <vector>
#include <algorithm>
#include <cstring>

class CircularBuffer {
public:
    CircularBuffer(size_t capacity = 0) : m_head(0), m_tail(0), m_size(0) {
        if (capacity > 0) {
            m_buffer.resize(capacity);
        }
    }

    void reserve(size_t capacity) {
        if (capacity > m_buffer.size()) {
            std::vector<char> newBuffer(capacity);
            if (m_size > 0) {
                if (m_head < m_tail) {
                    memcpy(newBuffer.data(), &m_buffer[m_head], m_size);
                } else {
                    size_t firstPart = m_buffer.size() - m_head;
                    memcpy(newBuffer.data(), &m_buffer[m_head], firstPart);
                    memcpy(newBuffer.data() + firstPart, &m_buffer[0], m_tail);
                }
            }
            m_buffer = std::move(newBuffer);
            m_head = 0;
            m_tail = m_size;
        }
    }

    void clear() {
        m_head = 0;
        m_tail = 0;
        m_size = 0;
    }

    size_t size() const { return m_size; }
    size_t capacity() const { return m_buffer.size(); }
    bool empty() const { return m_size == 0; }

    void write(const char* data, size_t len) {
        if (len == 0) return;
        
        if (m_size + len > m_buffer.size()) {
            reserve(std::max(m_buffer.size() * 2, m_size + len));
        }

        size_t capacity = m_buffer.size();
        size_t firstPart = std::min(len, capacity - m_tail);
        memcpy(&m_buffer[m_tail], data, firstPart);
        
        if (firstPart < len) {
            memcpy(&m_buffer[0], data + firstPart, len - firstPart);
            m_tail = len - firstPart;
        } else {
            m_tail = (m_tail + len) % capacity;
        }
        m_size += len;
    }

    // Returns pointer and length of the first contiguous chunk of data available to read
    void get_read_chunk(char*& out_ptr, size_t& out_len) {
        if (m_size == 0) {
            out_ptr = nullptr;
            out_len = 0;
            return;
        }

        out_ptr = &m_buffer[m_head];
        if (m_head < m_tail) {
            out_len = m_tail - m_head;
        } else {
            out_len = m_buffer.size() - m_head;
        }
    }

    void consume(size_t len) {
        if (len > m_size) len = m_size;
        m_head = (m_head + len) % m_buffer.size();
        m_size -= len;
        if (m_size == 0) {
            m_head = 0;
            m_tail = 0;
        }
    }

private:
    std::vector<char> m_buffer;
    size_t m_head;
    size_t m_tail;
    size_t m_size;
};