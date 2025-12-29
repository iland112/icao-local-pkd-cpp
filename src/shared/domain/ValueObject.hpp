/**
 * @file ValueObject.hpp
 * @brief Base class for Value Objects in DDD
 */

#pragma once

#include <string>
#include <functional>

namespace shared::domain {

/**
 * @brief Base template class for Value Objects
 *
 * Value Objects are immutable objects that are defined by their attributes.
 * Two Value Objects with the same attributes are considered equal.
 *
 * @tparam T The type of the underlying value
 */
template<typename T>
class ValueObject {
protected:
    T value_;

    /**
     * @brief Construct a new Value Object
     * @param value The underlying value
     */
    explicit ValueObject(T value) : value_(std::move(value)) {}

    /**
     * @brief Validate the value
     * Override in derived classes to add validation logic
     */
    virtual void validate() const {}

public:
    virtual ~ValueObject() = default;

    // Delete copy constructor and assignment for immutability
    // But allow move semantics
    ValueObject(const ValueObject&) = default;
    ValueObject& operator=(const ValueObject&) = delete;
    ValueObject(ValueObject&&) noexcept = default;
    ValueObject& operator=(ValueObject&&) noexcept = delete;

    /**
     * @brief Get the underlying value
     */
    [[nodiscard]] const T& getValue() const noexcept {
        return value_;
    }

    /**
     * @brief Equality comparison
     */
    bool operator==(const ValueObject& other) const {
        return value_ == other.value_;
    }

    /**
     * @brief Inequality comparison
     */
    bool operator!=(const ValueObject& other) const {
        return !(*this == other);
    }

    /**
     * @brief Less than comparison (for use in ordered containers)
     */
    bool operator<(const ValueObject& other) const {
        return value_ < other.value_;
    }
};

/**
 * @brief Specialization for string-based Value Objects
 */
class StringValueObject : public ValueObject<std::string> {
protected:
    explicit StringValueObject(std::string value)
        : ValueObject<std::string>(std::move(value)) {}

public:
    /**
     * @brief Check if the value is empty
     */
    [[nodiscard]] bool isEmpty() const noexcept {
        return value_.empty();
    }

    /**
     * @brief Get the string length
     */
    [[nodiscard]] size_t length() const noexcept {
        return value_.length();
    }
};

} // namespace shared::domain

// Hash function support for use in unordered containers
namespace std {
    template<typename T>
    struct hash<shared::domain::ValueObject<T>> {
        size_t operator()(const shared::domain::ValueObject<T>& vo) const {
            return hash<T>{}(vo.getValue());
        }
    };
}
