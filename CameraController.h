#pragma once
#include "pch.h"
#include "VectorMath.h"

namespace Math
{
	class Camera;
}

namespace MyDirectX
{
    class GameInput;

	class CameraController
	{
	public:
		// assumes worldUp is not the X basis vector
		CameraController(Math::Camera& camera, Math::Vector3 worldup, GameInput &gameInput);

		void Update(float dt);

		void SlowMovement(bool enable) { m_FineMovement = enable; }
		void SlowRotation(bool enable) { m_FineRotation = enable; }

		void EnableMomentum(bool enable) { m_Momentum = enable; }

		Math::Vector3 GetWorldEast() { return m_WorldEast; }
		Math::Vector3 GetWorldUp() { return m_WorldUp; }
		Math::Vector3 GetWorldNorth() { return m_WorldNorth; }
		float GetCurrentHeading() { return m_CurrentHeading; }
		float GetCurrentPitch() { return m_CurrentPitch; }

		void SetCurrentHeading(float heading) { m_CurrentHeading = heading; }
		void SetCurrentPitch(float pitch) { m_CurrentPitch = pitch; }
        void SetMoveSpeed(float speed) { m_MoveSpeed = std::max(speed, 100.0f); }
        void SetStrafeSpeed(float speed) { m_StrafeSpeed = std::max(speed, 100.0f); }

	private:
        CameraController& operator=(const CameraController&) { return *this; }

        void ApplyMomentum(float& oldValue, float& newValue, float deltaTime);

        Math::Vector3 m_WorldUp;
        Math::Vector3 m_WorldNorth;
        Math::Vector3 m_WorldEast;
        Math::Camera& m_TargetCamera;
        GameInput& m_Input;

        float m_HorizontalLookSensitivity;
        float m_VerticalLookSensitivity;
        float m_MoveSpeed;
        float m_StrafeSpeed;
        float m_MouseSensitivityX;
        float m_MouseSensitivityY;

        float m_CurrentHeading;
        float m_CurrentPitch;

        bool m_FineMovement;
        bool m_FineRotation;
        bool m_Momentum;

        float m_LastYaw;
        float m_LastPitch;
        float m_LastForward;
        float m_LastStrafe;
        float m_LastAscent;
	};

}