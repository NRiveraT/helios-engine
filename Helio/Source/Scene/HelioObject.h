/// @file HelioObject.h
/// @brief Base class for things that participate in the World / Actor system.
///
/// `HelioObject` gives you a stable monotonic ID + a virtual destructor.
/// That's it. Not every Helio type needs to be a HelioObject — `Mesh`,
/// `Texture`, `Pipeline` are POD/handle types and stay as-is. Only types
/// that the World holds (Actors, Components, future GameInstance, etc.)
/// derive from this.
#pragma once

#include <cstdint>

namespace helio::scene {

class HelioObject {
public:
    HelioObject();
    virtual ~HelioObject() = default;

    HelioObject(const HelioObject&) = delete;
    HelioObject& operator=(const HelioObject&) = delete;

    [[nodiscard]] uint64_t Id() const noexcept { return m_id; }

private:
    uint64_t m_id;
};

} // namespace helio::scene
