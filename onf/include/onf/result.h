#pragma once

// Result<T, E> with an std::expected-like API. Accessing the wrong alternative aborts.

#include <cstdlib>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace onf {

[[noreturn]] inline void result_misuse() noexcept { std::abort(); }

template <typename E>
class Unexpected {
public:
    explicit Unexpected(E error) : error_(std::move(error)) {}

    E& error() & noexcept { return error_; }
    const E& error() const& noexcept { return error_; }
    E&& error() && noexcept { return std::move(error_); }

private:
    E error_;
};

template <typename E>
Unexpected<std::decay_t<E>> unexpected(E&& error) {
    return Unexpected<std::decay_t<E>>(std::forward<E>(error));
}

template <typename T, typename E>
class Result {
public:
    Result(T value) : data_(std::in_place_index<0>, std::move(value)) {}
    Result(Unexpected<E> failure) : data_(std::in_place_index<1>, std::move(failure).error()) {}

    bool has_value() const noexcept { return data_.index() == 0; }
    explicit operator bool() const noexcept { return has_value(); }

    T& value() & {
        if (!has_value()) result_misuse();
        return std::get<0>(data_);
    }
    const T& value() const& {
        if (!has_value()) result_misuse();
        return std::get<0>(data_);
    }
    T&& value() && {
        if (!has_value()) result_misuse();
        return std::get<0>(std::move(data_));
    }

    E& error() & {
        if (has_value()) result_misuse();
        return std::get<1>(data_);
    }
    const E& error() const& {
        if (has_value()) result_misuse();
        return std::get<1>(data_);
    }
    E&& error() && {
        if (has_value()) result_misuse();
        return std::get<1>(std::move(data_));
    }

    T& operator*() & { return value(); }
    const T& operator*() const& { return value(); }
    T* operator->() { return &value(); }
    const T* operator->() const { return &value(); }

private:
    std::variant<T, E> data_;
};

template <typename E>
class Result<void, E> {
public:
    Result() = default;
    Result(Unexpected<E> failure) : error_(std::move(failure).error()) {}

    bool has_value() const noexcept { return !error_.has_value(); }
    explicit operator bool() const noexcept { return has_value(); }

    E& error() & {
        if (has_value()) result_misuse();
        return *error_;
    }
    const E& error() const& {
        if (has_value()) result_misuse();
        return *error_;
    }
    E&& error() && {
        if (has_value()) result_misuse();
        return std::move(*error_);
    }

private:
    std::optional<E> error_;
};

}  // namespace onf
