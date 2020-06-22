#pragma once

#include <cstddef>

#if defined(UBIP_USE_ETL)
#include <etl/atomic.h>
#include <etl/optional.h>
#include <etl/span.h>

namespace ubip
{
namespace stl = ::etl;
}
#else
#include <atomic>
#include <optional>
#include <span>

namespace ubip
{
namespace stl = ::std;
}
#endif

namespace ubip
{
template <class T, std::size_t N>
class buffer;

namespace detail
{
inline constexpr std::size_t max(std::size_t a, std::size_t b) {
    return a > b ? a : b;
}

inline constexpr std::size_t min(std::size_t a, std::size_t b) {
    return a < b ? a : b;
}

template <class T>
struct control_block
{
private:
    const std::size_t capacity_;
    T *buffer_;
    stl::atomic<std::size_t> read_{0};
    stl::atomic<std::size_t> write_{0};
    stl::atomic<std::size_t> watermark_{0};
    control_block(std::size_t sz, T *buf) : capacity_(sz), buffer_(buf) {
    }

    template <class, std::size_t>
    friend class ::ubip::buffer;

public:
    std::size_t get_read() const {
        return read_.load(stl::memory_order::seq_cst);
    }

    void set_read(std::size_t new_value) {
        read_.store(new_value, stl::memory_order::seq_cst);
    }

    std::size_t get_write() const {
        return write_.load(stl::memory_order::seq_cst);
    }

    void set_write(std::size_t w) {
        write_.store(w, stl::memory_order::seq_cst);
    }

    std::size_t get_watermark() const {
        return watermark_.load(stl::memory_order::seq_cst);
    }

    void set_watermark(std::size_t w) {
        watermark_.store(w, stl::memory_order::seq_cst);
    }

    std::size_t get_capacity() const {
        return capacity_;
    }

    stl::span<const T> const_span(std::size_t start_offset, std::size_t size) const {
        return {buffer_ + start_offset, size};
    }

    stl::span<T> mut_span(std::size_t start_offset, std::size_t size) {
        return {buffer_ + start_offset, size};
    }
};
} // namespace detail
template <class T>
class buffer_reader
{
    detail::control_block<T> *control_block_;

public:
    buffer_reader(detail::control_block<T> &cb) : control_block_(&cb) {
    }

    stl::span<const T> values() const {
        auto read = control_block_->get_read();
        auto write = control_block_->get_write();

        if (write > read) {
            const auto num_values = write - read;
            return control_block_->const_span(read, num_values);
        }
        else if (write < read) {
            // Write is less than read so has wrapped around,
            // so either we have read up to the watermark and should
            // read [0, write), or we have the area [read, watermark) to read
            // from and consume before we wrap.
            const auto watermark = control_block_->get_watermark();
            if (read == watermark) {
                return control_block_->const_span(0, write);
            }
            else {
                auto num_values = watermark - read;
                return control_block_->const_span(read, num_values);
            }
        }

        return {};
    }

    void consume(std::size_t amount) {
        auto read = control_block_->get_read();
        auto write = control_block_->get_write();

        if (read == write) {
            return;
        }

        if (write > read) {
            const auto new_read = detail::min(read + amount, write);
            control_block_->set_read(new_read);
        }
        else {
            const auto watermark = control_block_->get_watermark();
            const auto available_for_consumption = watermark - read + write;
            amount = detail::min(available_for_consumption, amount);
            const auto new_read = (read + amount) % watermark;
            control_block_->set_read(new_read);
        }
    }
};

template <class T>
class buffer_writer
{
    detail::control_block<T> *control_block_;
    std::size_t prepared_ = 0;

public:
    buffer_writer(detail::control_block<T> &cb) : control_block_(&cb) {
    }

    stl::span<T> prepare(std::size_t amount) {
        if (prepared_) {
            return {};
        }

        auto read = control_block_->get_read();
        auto write = control_block_->get_write();

        if (write >= read) {
            // Write leads read, either amount can fit inside [write, capacity), or
            // inside [0, read-1)
            const auto capacity = control_block_->get_capacity();
            const auto left_before_capacity = capacity - write;
            if (left_before_capacity >= amount) {
                prepared_ = amount;
                control_block_->set_watermark(write + amount);
                return control_block_->mut_span(write, amount);
            }
            else if (read > amount) {
                // Must not change order, write must be written after watermark is set
                // in case the reader fetches values between watermark is set and write
                // is written
                control_block_->set_watermark(write);
                control_block_->set_write(0);
                prepared_ = amount;
                return control_block_->mut_span(0, amount);
            }
        }
        else if (write + amount < read) {
            prepared_ = amount;
            return control_block_->mut_span(write, amount);
        }

        return {};
    }

    void commit(std::size_t amount) {
        if (prepared_ == 0 || amount == 0) {
            return;
        }

        amount = detail::min(amount, prepared_);

        prepared_ -= amount;
        auto write = control_block_->get_write();
        control_block_->set_write(write + amount);
    }
};

template <class T>
struct reader_writer_pair
{
    buffer_reader<T> reader;
    buffer_writer<T> writer;

    reader_writer_pair(detail::control_block<T> &cb) : reader(cb), writer(cb) {
    }
};

template <class T, std::size_t N>
class buffer
{
    T buffer_[N];
    detail::control_block<T> control_block_;

    bool rw_taken_ = false;

public:
    using pair_type = reader_writer_pair<T>;
    buffer() : control_block_(N, buffer_) {
    }

    stl::optional<pair_type> take_reader_writer() {
        if (rw_taken_) {
            return {};
        }

        rw_taken_ = true;
        return pair_type(control_block_);
    }
};
} // namespace ubip