/**
 * @file AggregateRoot.hpp
 * @brief Base class for Aggregate Roots in DDD
 */

#pragma once

#include "Entity.hpp"
#include <vector>
#include <memory>
#include <functional>

namespace shared::domain {

// Forward declaration for domain events
class DomainEvent;

/**
 * @brief Base template class for Aggregate Roots
 *
 * Aggregate Roots are the entry point to an aggregate - a cluster of
 * domain objects that can be treated as a single unit.
 * All external access to the aggregate must go through the root.
 *
 * @tparam IdType The type of the aggregate root's identifier
 */
template<typename IdType>
class AggregateRoot : public Entity<IdType> {
private:
    std::vector<std::shared_ptr<DomainEvent>> domainEvents_;
    int version_ = 0;

protected:
    using Entity<IdType>::Entity;

    /**
     * @brief Register a domain event
     * @param event The event to register
     */
    void registerEvent(std::shared_ptr<DomainEvent> event) {
        domainEvents_.push_back(std::move(event));
    }

    /**
     * @brief Increment the version number
     */
    void incrementVersion() {
        ++version_;
        this->touch();
    }

public:
    /**
     * @brief Get all pending domain events
     */
    [[nodiscard]] const std::vector<std::shared_ptr<DomainEvent>>& getDomainEvents() const noexcept {
        return domainEvents_;
    }

    /**
     * @brief Clear all pending domain events
     */
    void clearDomainEvents() {
        domainEvents_.clear();
    }

    /**
     * @brief Get the aggregate version (for optimistic locking)
     */
    [[nodiscard]] int getVersion() const noexcept {
        return version_;
    }

    /**
     * @brief Set the aggregate version (used when loading from persistence)
     */
    void setVersion(int version) {
        version_ = version;
    }
};

/**
 * @brief Base class for Domain Events
 */
class DomainEvent {
private:
    std::string eventType_;
    std::chrono::system_clock::time_point occurredAt_;

protected:
    explicit DomainEvent(std::string eventType)
        : eventType_(std::move(eventType)),
          occurredAt_(std::chrono::system_clock::now()) {}

public:
    virtual ~DomainEvent() = default;

    [[nodiscard]] const std::string& getEventType() const noexcept {
        return eventType_;
    }

    [[nodiscard]] std::chrono::system_clock::time_point getOccurredAt() const noexcept {
        return occurredAt_;
    }
};

} // namespace shared::domain
