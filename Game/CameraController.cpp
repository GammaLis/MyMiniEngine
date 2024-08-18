#include "CameraController.h"
#include "Camera.h"
#include "GameInput.h"
#include "GfxCommon.h"

using namespace MyDirectX;
using namespace Math;

CameraController::CameraController(Camera& camera, Vector3 worldup, GameInput& gameInput)
	: m_TargetCamera(camera), m_Input(gameInput)
{
    m_WorldUp = Normalize(worldup);
    m_WorldNorth = Normalize(Cross(m_WorldUp, Vector3(kXUnitVector)));
    m_WorldEast = Cross(m_WorldNorth, m_WorldUp);

    m_HorizontalLookSensitivity = 2.0f;
    m_VerticalLookSensitivity = 2.0f;
    m_MoveSpeed = 1000.0f;
    m_StrafeSpeed = 1000.0f;
    m_MouseSensitivityX = 1.0f;
    m_MouseSensitivityY = 1.0f;

    m_CurrentPitch = Sin(Dot(camera.GetForwardVec(), m_WorldUp));

    Vector3 forward = Normalize(Cross(m_WorldUp, camera.GetRightVec()));
    m_CurrentHeading = ATan2(-Dot(forward, m_WorldEast), Dot(forward, m_WorldNorth));

    m_FineMovement = false;
    m_FineRotation = false;
    m_Momentum = true;

    m_LastYaw = 0.0f;
    m_LastPitch = 0.0f;
    m_LastForward = 0.0f;
    m_LastStrafe = 0.0f;
    m_LastAscent = 0.0f;
}

void CameraController::Update(float deltaTime)
{
    (deltaTime);

    float timeScale = GfxStates::s_DebugZoom == DebugZoom::Off ? 1.0f : GfxStates::s_DebugZoom == DebugZoom::k2x ? 0.5f : 0.25f;    

    if (m_Input.IsFirstPressed(DigitalInput::kLThumbClick) || m_Input.IsFirstPressed(DigitalInput::kKey_lshift))
        m_FineMovement = !m_FineMovement;

    if (m_Input.IsFirstPressed(DigitalInput::kRThumbClick))
        m_FineRotation = !m_FineRotation;

    float speedScale = (m_FineMovement ? 0.1f : 1.0f) * timeScale;
    float panScale = (m_FineRotation ? 0.5f : 1.0f) * timeScale;

    float yaw = m_Input.GetTimeCorrectedAnalogInput(AnalogInput::kAnalogRightStickX) * m_HorizontalLookSensitivity * panScale;
    float pitch = m_Input.GetTimeCorrectedAnalogInput(AnalogInput::kAnalogRightStickY) * m_VerticalLookSensitivity * panScale;
    float forward = m_MoveSpeed * speedScale * (
        m_Input.GetTimeCorrectedAnalogInput(AnalogInput::kAnalogLeftStickY) +
        (m_Input.IsPressed(DigitalInput::kKey_w) ? deltaTime : 0.0f) +
        (m_Input.IsPressed(DigitalInput::kKey_s) ? -deltaTime : 0.0f)
        );
    float strafe = m_StrafeSpeed * speedScale * (
        m_Input.GetTimeCorrectedAnalogInput(AnalogInput::kAnalogLeftStickX) +
        (m_Input.IsPressed(DigitalInput::kKey_d) ? deltaTime : 0.0f) +
        (m_Input.IsPressed(DigitalInput::kKey_a) ? -deltaTime : 0.0f)
        );
    float ascent = m_StrafeSpeed * speedScale * (
        m_Input.GetTimeCorrectedAnalogInput(AnalogInput::kAnalogRightTrigger) -
        m_Input.GetTimeCorrectedAnalogInput(AnalogInput::kAnalogLeftTrigger) +
        (m_Input.IsPressed(DigitalInput::kKey_e) ? deltaTime : 0.0f) +
        (m_Input.IsPressed(DigitalInput::kKey_q) ? -deltaTime : 0.0f)
        );

    if (m_Momentum)
    {
        ApplyMomentum(m_LastYaw, yaw, deltaTime);
        ApplyMomentum(m_LastPitch, pitch, deltaTime);
        ApplyMomentum(m_LastForward, forward, deltaTime);
        ApplyMomentum(m_LastStrafe, strafe, deltaTime);
        ApplyMomentum(m_LastAscent, ascent, deltaTime);
    }

    // don't apply momentum to mouse inputs
    yaw += m_Input.GetAnalogInput(AnalogInput::kAnalogMouseX) * m_MouseSensitivityX;
    pitch += m_Input.GetAnalogInput(AnalogInput::kAnalogMouseY) * m_MouseSensitivityY;

    m_CurrentPitch += pitch;
    m_CurrentPitch = XMMin(XM_PIDIV2, m_CurrentPitch);
    m_CurrentPitch = XMMax(-XM_PIDIV2, m_CurrentPitch);

    m_CurrentHeading -= yaw;
    if (m_CurrentHeading > XM_PI)
        m_CurrentHeading -= XM_2PI;
    else if (m_CurrentHeading <= -XM_PI)
        m_CurrentHeading += XM_2PI;

    Matrix3 orientation = Matrix3(m_WorldEast, m_WorldUp, -m_WorldNorth) * Matrix3::MakeYRotation(m_CurrentHeading) * Matrix3::MakeXRotation(m_CurrentPitch);
    Vector3 position = orientation * Vector3(strafe, ascent, -forward) + m_TargetCamera.GetPosition();
    m_TargetCamera.SetTransform(AffineTransform(orientation, position));
    m_TargetCamera.Update();
}

void CameraController::ApplyMomentum(float& oldValue, float& newValue, float deltaTime)
{
    float blendedValue;
    if (Abs(newValue) > Abs(oldValue))
        blendedValue = Lerp(newValue, oldValue, Pow(0.6f, deltaTime * 60.0f));
    else
        blendedValue = Lerp(newValue, oldValue, Pow(0.8f, deltaTime * 60.0f));
    oldValue = blendedValue;
    newValue = blendedValue;
}
