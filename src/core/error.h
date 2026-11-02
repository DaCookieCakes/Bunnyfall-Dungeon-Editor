#pragma once

#include <expected>
#include <string>
#include <utility>

namespace de {

/// An error a caller can show to a person: what failed, in words.
struct Error {
    std::string message;
};

template <typename T>
using Result = std::expected<T, Error>;

inline std::unexpected<Error> fail(std::string message) {
    return std::unexpected<Error>(Error{std::move(message)});
}

} // namespace de
