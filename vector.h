#pragma once

#include <cassert>
#include <cstdlib>
#include <new>
#include <memory>
#include <utility>
#include <type_traits>
#include <algorithm>
#include <cstddef>

template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity))
        , capacity_(capacity) {
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }

    T* operator+(size_t offset) noexcept {
        assert(offset <= capacity_);
        return offset == 0 ? buffer_ : buffer_ + offset;
    }

    const T* operator+(size_t offset) const noexcept {
        return const_cast<RawMemory&>(*this) + offset;
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<RawMemory&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    RawMemory(const RawMemory&) = delete;

    RawMemory& operator=(const RawMemory& rhs) = delete;

    RawMemory(RawMemory&& other) noexcept 
    : buffer_(other.buffer_)
    , capacity_(other.capacity_) { 
        other.buffer_ = nullptr;
        other.capacity_ = 0;
    }

    RawMemory& operator=(RawMemory&& rhs) noexcept { 
        if (this != &rhs) {
            Deallocate(buffer_);

            buffer_ = rhs.buffer_;
            capacity_ = rhs.capacity_;

            rhs.buffer_ = nullptr;
            rhs.capacity_ = 0;
        }
        return *this;
    }

    void Swap(RawMemory& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T* GetAddress() const noexcept {
        return buffer_;
    }

    T* GetAddress() noexcept {
        return buffer_;
    }

    size_t Capacity() const {
        return capacity_;
    }

private:
    static T* Allocate(size_t n) {
        return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
    }

    static void Deallocate(T* buf) noexcept {
        operator delete(buf);
    }

    T* buffer_ = nullptr;
    size_t capacity_ = 0;
};

template <typename T>
class Vector {
public:
    using iterator = T*;
    using const_iterator = const T*;

    iterator begin() noexcept {
        return data_.GetAddress();
    }
    iterator end() noexcept {
        return size_ == 0 ? data_.GetAddress() : data_.GetAddress() + size_;
    }
    const_iterator begin() const noexcept {
        return data_.GetAddress();
    }
    const_iterator end() const noexcept {
        return size_ == 0 ? data_.GetAddress() : data_.GetAddress() + size_;
    }
    const_iterator cbegin() const noexcept {
        return begin();
    }
    const_iterator cend() const noexcept {
        return end();
    }

    Vector() noexcept = default;

    explicit Vector(size_t size) 
    : data_(size) {
        std::uninitialized_value_construct_n(data_.GetAddress(), size);
        size_ = size;
    }

    Vector(const Vector& other) 
    : data_(other.size_) {
        std::uninitialized_copy_n(other.data_.GetAddress(), other.Size(), data_.GetAddress());
        size_ = other.Size();
    }

    Vector(Vector&& other) noexcept {
        Swap(other);
    }

    ~Vector() {
        std::destroy_n(data_.GetAddress(), size_);
    }

    
    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args) {
        assert(pos >= cbegin() && pos <= cend());
        size_t index = pos - cbegin();

        if (size_ < Capacity()) {
            T new_value(std::forward<Args>(args)...);

            if (index < size_) {
                new (end()) T(std::move(*(end() - 1)));

                try {
                    std::move_backward(begin() + index, end() - 1, end());
                    *(begin() + index) = std::move(new_value);
                } catch (...) {
                    std::destroy_at(end());
                    throw;
                }
            } else {
                new (end()) T(std::move(new_value));
            }

            ++size_;
        } else {
            size_t new_capacity = Capacity() == 0 ? 1 : Capacity() * 2;
            RawMemory<T> new_data(new_capacity);

            new (new_data.GetAddress() + index) T(std::forward<Args>(args)...);
        
            try {
                UninitializedMoveIfNoexcept(data_.GetAddress(), index, new_data.GetAddress());
                try {
                    UninitializedMoveIfNoexcept(data_.GetAddress() + index, size_ - index, new_data.GetAddress() + index + 1);
                } catch (...) {
                    std::destroy_n(new_data.GetAddress(), index);
                    throw;
                }
            } catch (...) {
                std::destroy_at(new_data.GetAddress() + index);
                throw;
            }

            std::destroy_n(data_.GetAddress(), size_);
            data_.Swap(new_data);
            ++size_;
        }

        return begin() + index;
    }


    iterator Erase(const_iterator pos) noexcept(std::is_nothrow_move_assignable_v<T>) {
        size_t index = pos - cbegin();

        std::move(begin() + index + 1, end(), begin() + index);

        std::destroy_at(data_.GetAddress() + size_ - 1);

        --size_;

        return begin() + index;
    }

    iterator Insert(const_iterator pos, const T& value) {
        return Emplace(pos, value);
    }

    iterator Insert(const_iterator pos, T&& value) {
        return Emplace(pos, std::move(value));
    }

    void Resize(size_t new_size) {
        if (size_ == new_size) return;

        if (size_ > new_size) {
            std::destroy_n(data_.GetAddress() + new_size, size_ - new_size);
            size_ = new_size;
            return;
        }

        if (new_size > Capacity()) {
            RawMemory<T> new_data(new_size);

            UninitializedMoveIfNoexcept(data_.GetAddress(), size_, new_data.GetAddress());

            std::uninitialized_value_construct_n(new_data.GetAddress() + size_, new_size - size_);

            std::destroy_n(data_.GetAddress(), size_);
            data_.Swap(new_data);
            size_ = new_size;
        } else {
            std::uninitialized_value_construct_n(data_.GetAddress() + size_, new_size - size_);
            size_ = new_size;
        }
    }

    void PushBack(const T& value) {
        EmplaceBack(value);
    }

    void PushBack(T&& value) {
        EmplaceBack(std::move(value));
    }

    void PopBack() noexcept {
        assert(size_ > 0);

        std::destroy_at(data_.GetAddress() + size_ - 1);
        --size_;
    }

    template <typename... Args>
    T& EmplaceBack(Args&&... args) {
        return *Emplace(cend(), std::forward<Args>(args)...);
    }

    void Reserve(size_t new_capacity) {
        if (new_capacity <= Capacity()) {
            return;
        }

        RawMemory<T> new_data(new_capacity);
        
        UninitializedMoveIfNoexcept(data_.GetAddress(), size_, new_data.GetAddress());

        std::destroy_n(data_.GetAddress(), size_);

        data_.Swap(new_data);
    }

    void Swap(Vector& other) noexcept {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }

    size_t Size() const noexcept {
        return size_;
    }

    size_t Capacity() const noexcept {
        return data_.Capacity();
    }


    Vector& operator=(const Vector& other) {
    if (this == &other) {
        return *this;
    }

    if (other.size_ > Capacity()) {
        Vector temp(other);
        Swap(temp);
    } else {
        size_t common_size = size_ < other.size_
            ? size_
            : other.size_;

        for (size_t i = 0; i < common_size; ++i) {
            data_[i] = other.data_[i];
        }

        if (other.size_ > size_) {
            std::uninitialized_copy_n(
                other.data_.GetAddress() + size_,
                other.size_ - size_,
                data_.GetAddress() + size_
            );
        } else {
            std::destroy_n(
                data_.GetAddress() + other.size_,
                size_ - other.size_
            );
        }

        size_ = other.size_;
    }

    return *this;
}


    Vector& operator=(Vector&& other) noexcept {
        if (this != &other) {
            Swap(other);
        }
        return *this;
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

private:
    RawMemory<T> data_;
    size_t size_ = 0;

    void UninitializedMoveIfNoexcept(T* first, size_t count, T* destination) {
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
            std::uninitialized_move_n(first, count, destination);
        } else {
            std::uninitialized_copy_n(first, count, destination);
        }
    }
};