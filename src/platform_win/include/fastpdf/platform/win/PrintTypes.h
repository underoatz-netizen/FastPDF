#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace fastpdf::platform::win {

// Scoped RAII wrapper for printer Device Context (created via CreateDCW)
class UniqueHDC {
public:
    UniqueHDC() noexcept = default;
    explicit UniqueHDC(HDC hdc) noexcept : hdc_(hdc) {}

    ~UniqueHDC() noexcept {
        reset();
    }

    UniqueHDC(const UniqueHDC&) = delete;
    UniqueHDC& operator=(const UniqueHDC&) = delete;

    UniqueHDC(UniqueHDC&& other) noexcept : hdc_(other.hdc_) {
        other.hdc_ = nullptr;
    }

    UniqueHDC& operator=(UniqueHDC&& other) noexcept {
        if (this != &other) {
            reset();
            hdc_ = other.hdc_;
            other.hdc_ = nullptr;
        }
        return *this;
    }

    HDC get() const noexcept { return hdc_; }
    explicit operator bool() const noexcept { return hdc_ != nullptr; }

    HDC release() noexcept {
        HDC h = hdc_;
        hdc_ = nullptr;
        return h;
    }

    void reset(HDC hdc = nullptr) noexcept {
        if (hdc_ != nullptr) {
            DeleteDC(hdc_);
        }
        hdc_ = hdc;
    }

private:
    HDC hdc_ = nullptr;
};

// Scoped RAII wrapper for HGLOBAL (used with DEVMODE / DEVNAMES in Windows printing)
class UniqueHGLOBAL {
public:
    UniqueHGLOBAL() noexcept = default;
    explicit UniqueHGLOBAL(HGLOBAL h) noexcept : handle_(h) {}

    ~UniqueHGLOBAL() noexcept {
        reset();
    }

    UniqueHGLOBAL(const UniqueHGLOBAL&) = delete;
    UniqueHGLOBAL& operator=(const UniqueHGLOBAL&) = delete;

    UniqueHGLOBAL(UniqueHGLOBAL&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }

    UniqueHGLOBAL& operator=(UniqueHGLOBAL&& other) noexcept {
        if (this != &other) {
            reset();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    HGLOBAL get() const noexcept { return handle_; }
    explicit operator bool() const noexcept { return handle_ != nullptr; }

    HGLOBAL release() noexcept {
        HGLOBAL h = handle_;
        handle_ = nullptr;
        return h;
    }

    void reset(HGLOBAL h = nullptr) noexcept {
        if (handle_ != nullptr) {
            GlobalFree(handle_);
        }
        handle_ = h;
    }

private:
    HGLOBAL handle_ = nullptr;
};

// Scoped GlobalLock RAII helper
template <typename T>
class ScopedGlobalLock {
public:
    explicit ScopedGlobalLock(HGLOBAL h) noexcept : handle_(h) {
        if (handle_ != nullptr) {
            ptr_ = reinterpret_cast<T*>(GlobalLock(handle_));
        }
    }

    ~ScopedGlobalLock() noexcept {
        if (handle_ != nullptr && ptr_ != nullptr) {
            GlobalUnlock(handle_);
        }
    }

    ScopedGlobalLock(const ScopedGlobalLock&) = delete;
    ScopedGlobalLock& operator=(const ScopedGlobalLock&) = delete;

    T* get() const noexcept { return ptr_; }
    T* operator->() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

private:
    HGLOBAL handle_ = nullptr;
    T* ptr_ = nullptr;
};

} // namespace fastpdf::platform::win
