/// @file FlyCameraController.h
/// @brief FPS-style fly controls for a scene::Camera.
///
/// Lives in the gameplay layer so the Scene module stays input-free: the
/// camera actor is pure data (transform + projection), this controller reads
/// the input dispatcher every frame and drives it through the normal Actor
/// transform API. Yaw/pitch state lives HERE (the one place that thinks in
/// Euler angles); the camera's quaternion is always derived from it via
/// `QuatFromEuler` — no dual source of truth, no LookAt fighting the
/// accumulated rotation.
///
/// Controls: WASD planar, E/Q up/down, hold right mouse to look.
#pragma once

#include <cstdint>

namespace helio::input { class ActionMap; }
namespace helio::platform::windows { class Window; }
namespace helio::scene { class Camera; }

namespace helio::gameplay
{
    class FlyCameraController
    {
    public:
        /// Point the controller at `Cam` for this frame. Pass the freshly
        /// resolved camera (or nullptr if it was destroyed) EVERY frame — the
        /// controller never caches a camera pointer across frames, so an editor
        /// deletion can't dangle it. Yaw/pitch re-sync only when the camera
        /// identity actually changes, so re-pointing at the same camera each
        /// frame is free and does not fight the accumulated look angles.
        void RetargetCamera(scene::Camera* Cam);

        /// Register the WASD/EQ action bindings on the engine's action map.
        void BindInput(input::ActionMap& Map) const;

        /// Poll input and move/rotate the camera. Call once per frame after
        /// the window pumped events.
        void Update(platform::windows::Window& Win, float DeltaTime);

        /// Gate for the editor: when disabled the controller neither moves
        /// the camera nor captures the mouse.
        void SetEnabled(bool Enabled) noexcept { m_Enabled = Enabled; }
        [[nodiscard]] bool IsEnabled() const noexcept { return m_Enabled; }

        void SetMoveSpeed(float UnitsPerSecond) noexcept { m_MoveSpeed = UnitsPerSecond; }
        [[nodiscard]] float GetMoveSpeed() const noexcept { return m_MoveSpeed; }

        void SetLookSensitivity(float RadiansPerCount) noexcept { m_LookSensitivity = RadiansPerCount; }
        [[nodiscard]] float GetLookSensitivity() const noexcept { return m_LookSensitivity; }

    private:
        scene::Camera* m_Camera = nullptr;
        uint64_t m_CameraId = 0; // identity of m_Camera, to detect retargeting
        float m_Yaw = 0.0f;
        float m_Pitch = 0.0f;
        float m_MoveSpeed = 3.0f;
        float m_LookSensitivity = 0.002f;
        bool m_Enabled = true;
    };
} // namespace helio::gameplay
