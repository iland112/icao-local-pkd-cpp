/**
 * @file Entity.hpp
 * @brief Base class for Entities in DDD
 */

#pragma once

#include <string>
#include <chrono>

namespace shared::domain {

/**
 * @brief Base template class for Entities
 *
 * Entities are objects that are defined by their identity (ID),
 * not by their attributes. Two entities with the same ID are the same entity.
 *
 * @tparam IdType The type of the entity's identifier
 */
template<typename IdType>
class Entity {
protected:
    IdType id_;
    std::chrono::system_clock::time_point createdAt_;
    std::chrono::system_clock::time_point updatedAt_;

    /**
     * @brief Construct a new Entity with the given ID
     */
    explicit Entity(IdType id)
        : id_(std::move(id)),
          createdAt_(std::chrono::system_clock::now()),
          updatedAt_(createdAt_) {}

    /**
     * @brief Update the modification timestamp
     */
    void touch() {
        updatedAt_ = std::chrono::system_clock::now();
    }

public:
    virtual ~Entity() = default;

    // Entities should not be copied, only moved
    Entity(const Entity&) = delete;
    Entity& operator=(const Entity&) = delete;
    Entity(Entity&&) noexcept = default;
    Entity& operator=(Entity&&) noexcept = default;

    /**
     * @brief Get the entity's ID
     */
    [[nodiscard]] const IdType& getId() const noexcept {
        return id_;
    }

    /**
     * @brief Get the creation timestamp
     */
    [[nodiscard]] std::chrono::system_clock::time_point getCreatedAt() const noexcept {
        return createdAt_;
    }

    /**
     * @brief Get the last modification timestamp
     */
    [[nodiscard]] std::chrono::system_clock::time_point getUpdatedAt() const noexcept {
        return updatedAt_;
    }

    /**
     * @brief Equality comparison based on ID
     */
    bool operator==(const Entity& other) const {
        return id_ == other.id_;
    }

    /**
     * @brief Inequality comparison
     */
    bool operator!=(const Entity& other) const {
        return !(*this == other);
    }
};

} // namespace shared::domain
