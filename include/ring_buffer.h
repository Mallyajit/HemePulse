#pragma once

#include <stddef.h>

template <typename T, size_t N>
class RingBuffer {
 public:
  RingBuffer() : head_(0), count_(0) {}

  void clear() {
    head_ = 0;
    count_ = 0;
  }

  void push(const T& item) {
    data_[head_] = item;
    head_ = (head_ + 1) % N;
    if (count_ < N) {
      ++count_;
    }
  }

  size_t size() const { return count_; }
  size_t capacity() const { return N; }
  bool empty() const { return count_ == 0; }
  bool full() const { return count_ == N; }

  const T& atOldest(size_t index) const {
    size_t start = (head_ + N - count_) % N;
    size_t pos = (start + index) % N;
    return data_[pos];
  }

  const T& atNewest(size_t indexFromNewest) const {
    size_t newest = (head_ + N - 1) % N;
    size_t pos = (newest + N - (indexFromNewest % N)) % N;
    return data_[pos];
  }

 private:
  T data_[N];
  size_t head_;
  size_t count_;
};
