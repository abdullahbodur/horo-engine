#pragma once

#include "Horo/Foundation/ErrorCode.h"

#include <cassert>
#include <utility>
#include <variant>

namespace Horo
{
    /** @brief Typed result for operations whose expected failure is represented by Error. */
    template <typename T>
    class Result
    {
    public:
        [[nodiscard]] static Result Success(T value) { return Result(std::move(value)); }
        [[nodiscard]] static Result Failure(Error error) { return Result(std::move(error)); }
        [[nodiscard]] bool HasValue() const noexcept { return std::holds_alternative<T>(m_value); }
        [[nodiscard]] bool HasError() const noexcept { return !HasValue(); }
        [[nodiscard]] const T &Value() const & { assert(HasValue()); return std::get<T>(m_value); }
        [[nodiscard]] T &&Value() && { assert(HasValue()); return std::move(std::get<T>(m_value)); }
        [[nodiscard]] const Error &ErrorValue() const { assert(HasError()); return std::get<Error>(m_value); }
    private:
        explicit Result(T value) : m_value(std::move(value)) {}
        explicit Result(Error error) : m_value(std::move(error)) {}
        std::variant<T, Error> m_value;
    };

    /** @brief Result specialization for successful operations without a payload. */
    template <>
    class Result<void>
    {
    public:
        [[nodiscard]] static Result Success() { return Result(true); }
        [[nodiscard]] static Result Failure(Error error) { return Result(std::move(error)); }
        [[nodiscard]] bool HasValue() const noexcept { return m_success; }
        [[nodiscard]] bool HasError() const noexcept { return !m_success; }
        [[nodiscard]] const Error &ErrorValue() const { assert(HasError()); return m_error; }
    private:
        explicit Result(bool success) : m_success(success) {}
        explicit Result(Error error) : m_error(std::move(error)) {}
        bool m_success = false;
        Error m_error{ErrorCode{"foundation.result.no_error"}, ErrorDomainId{"horo.foundation"}, ErrorSeverity::Info, ""};
    };
} // namespace Horo
