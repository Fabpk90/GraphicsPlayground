/*
 *  Copyright 2019-2021 Diligent Graphics LLC
 *  Copyright 2015-2019 Egor Yusov
 *  
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *  
 *      http://www.apache.org/licenses/LICENSE-2.0
 *  
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence), 
 *  contract, or otherwise, unless required by applicable law (such as deliberate 
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental, 
 *  or consequential damages of any character arising as a result of this License or 
 *  out of the use or inability to use the software (including but not limited to damages 
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and 
 *  all other commercial damages or losses), even if such Contributor has been advised 
 *  of the possibility of such damages.
 */
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_LEFT_HANDED
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec3.hpp>

#include "FirstPersonCamera.hpp"


namespace Diligent
{

    void FirstPersonCamera::Update(InputController &Controller, float ElapsedTime)
    {
        float3 MoveDirection = float3(0, 0, 0);
        // Update acceleration vector based on keyboard state
        if (Controller.IsKeyDown(InputKeys::MoveForward))
            MoveDirection.z += 1.0f;
        if (Controller.IsKeyDown(InputKeys::MoveBackward))
            MoveDirection.z -= 1.0f;

        if (Controller.IsKeyDown(InputKeys::MoveRight))
            MoveDirection.x += 1.0f;
        if (Controller.IsKeyDown(InputKeys::MoveLeft))
            MoveDirection.x -= 1.0f;

        if (Controller.IsKeyDown(InputKeys::MoveUp))
            MoveDirection.y += 1.0f;
        if (Controller.IsKeyDown(InputKeys::MoveDown))
            MoveDirection.y -= 1.0f;

        if (Controller.IsKeyDown(InputKeys::F1))
        {
            m_cameraIndex = 0;
        }
        else if (Controller.IsKeyDown(InputKeys::F2))
        {
            m_cameraIndex++;
        }
        else if (Controller.IsKeyDown(InputKeys::F3))
        {
            m_cameraIndex--;
        }

        // Normalize vector so if moving in 2 dirs (left & forward),
        // the camera doesn't move faster than if moving in 1 dir
        auto len = length(MoveDirection);
        if (len != 0.0)
            MoveDirection /= len;

        bool IsSpeedUpScale = Controller.IsKeyDown(InputKeys::ShiftDown);
        bool IsSuperSpeedUpScale = Controller.IsKeyDown(InputKeys::ControlDown);

        MoveDirection *= m_fMoveSpeed;
        if (IsSpeedUpScale) MoveDirection *= m_fSpeedUpScale;
        if (IsSuperSpeedUpScale) MoveDirection *= m_fSuperSpeedUpScale;

        m_fCurrentSpeed = length(MoveDirection);

        float3 PosDelta = MoveDirection * ElapsedTime;

        {
            const auto &mouseState = Controller.GetMouseState();

            float MouseDeltaX = 0;
            float MouseDeltaY = 0;
            if (m_LastMouseState.PosX >= 0 && m_LastMouseState.PosY >= 0 &&
                m_LastMouseState.ButtonFlags != MouseState::BUTTON_FLAG_NONE)
            {
                MouseDeltaX = mouseState.PosX - m_LastMouseState.PosX;
                MouseDeltaY = mouseState.PosY - m_LastMouseState.PosY;
            }
            m_LastMouseState = mouseState;

            float fYawDelta = MouseDeltaX * m_fRotationSpeed;
            float fPitchDelta = MouseDeltaY * m_fRotationSpeed;
            if (mouseState.ButtonFlags & MouseState::BUTTON_FLAG_RIGHT)
            {
                m_fYawAngle += fYawDelta * -m_fHandness;
                m_fPitchAngle += fPitchDelta * -m_fHandness;
                m_fPitchAngle = std::max(m_fPitchAngle, -PI_F / 2.f);
                m_fPitchAngle = std::min(m_fPitchAngle, +PI_F / 2.f);
            }
        }

        float4x4 ReferenceRotation = GetReferenceRotation();

        float4x4 CameraRotation = float4x4::RotationArbitrary(m_ReferenceUpAxis, m_fYawAngle) *
                                  float4x4::RotationArbitrary(m_ReferenceRightAxis, m_fPitchAngle) *
                                  ReferenceRotation;
        float4x4 WorldRotation = CameraRotation.Transpose();

        float3 PosDeltaWorld = PosDelta * WorldRotation;
        m_Pos += PosDeltaWorld;

        m_ViewMatrix = float4x4::Translation(-m_Pos) * CameraRotation;
        m_WorldMatrix = WorldRotation * float4x4::Translation(m_Pos);
    }

    float4x4 FirstPersonCamera::GetReferenceRotation() const
    {
        // clang-format off
        return float4x4
                {
                        m_ReferenceRightAxis.x, m_ReferenceUpAxis.x, m_ReferenceAheadAxis.x, 0,
                        m_ReferenceRightAxis.y, m_ReferenceUpAxis.y, m_ReferenceAheadAxis.y, 0,
                        m_ReferenceRightAxis.z, m_ReferenceUpAxis.z, m_ReferenceAheadAxis.z, 0,
                        0, 0, 0, 1
                };
        // clang-format on
    }

    void FirstPersonCamera::SetReferenceAxes(const float3 &ReferenceRightAxis, const float3 &ReferenceUpAxis,
                                             bool IsRightHanded)
    {
        m_ReferenceRightAxis = normalize(ReferenceRightAxis);
        m_ReferenceUpAxis = ReferenceUpAxis - dot(ReferenceUpAxis, m_ReferenceRightAxis) * m_ReferenceRightAxis;
        auto UpLen = length(m_ReferenceUpAxis);
        constexpr float Epsilon = 1e-5f;
        if (UpLen < Epsilon)
        {
            UpLen = Epsilon;
            LOG_WARNING_MESSAGE("Right and Up axes are collinear");
        }
        m_ReferenceUpAxis /= UpLen;

        m_fHandness = IsRightHanded ? +1.f : -1.f;
        m_ReferenceAheadAxis = m_fHandness * cross(m_ReferenceRightAxis, m_ReferenceUpAxis);
        auto AheadLen = length(m_ReferenceAheadAxis);
        if (AheadLen < Epsilon)
        {
            AheadLen = Epsilon;
            LOG_WARNING_MESSAGE("Ahead axis is not well defined");
        }
        m_ReferenceAheadAxis /= AheadLen;
    }

    void FirstPersonCamera::SetLookAt(const float3 &LookAt)
    {
        float3 ViewDir = LookAt - m_Pos;

        ViewDir = ViewDir * GetReferenceRotation();

        m_fYawAngle = atan2f(ViewDir.x, ViewDir.z);

        float fXZLen = sqrtf(ViewDir.z * ViewDir.z + ViewDir.x * ViewDir.x);
        m_fPitchAngle = -atan2f(ViewDir.y, fXZLen);
    }

    void FirstPersonCamera::SetRotation(float Yaw, float Pitch)
    {
        m_fYawAngle = Yaw;
        m_fPitchAngle = Pitch;
    }

    void FirstPersonCamera::SetProjAttribs(Float32 NearClipPlane,
                                           Float32 FarClipPlane,
                                           Float32 AspectRatio,
                                           Float32 FOV,
                                           SURFACE_TRANSFORM SrfPreTransform,
                                           bool IsGL)
    {
        m_ProjAttribs.NearClipPlane = NearClipPlane;
        m_ProjAttribs.FarClipPlane = FarClipPlane;
        m_ProjAttribs.AspectRatio = AspectRatio;
        m_ProjAttribs.FOV = FOV;
        m_ProjAttribs.PreTransform = SrfPreTransform;
        m_ProjAttribs.IsGL = IsGL;

        m_slicesFarShadow = {FarClipPlane / 50.0f, FarClipPlane / 25.0f, FarClipPlane / 10.0f};

        float XScale, YScale;
        if (SrfPreTransform == SURFACE_TRANSFORM_ROTATE_90 ||
            SrfPreTransform == SURFACE_TRANSFORM_ROTATE_270 ||
            SrfPreTransform == SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_90 ||
            SrfPreTransform == SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_270)
        {
            // When the screen is rotated, vertical FOV becomes horizontal FOV
            XScale = 1.f / std::tan(FOV / 2.f);
            // Aspect ratio is width/height accounting for pretransform
            YScale = XScale / AspectRatio;
        }
        else
        {
            YScale = 1.f / std::tan(FOV / 2.f);
            XScale = YScale / AspectRatio;
        }

        float4x4 Proj;
        Proj._11 = XScale;
        Proj._22 = YScale;
        Proj.SetNearFarClipPlanes(NearClipPlane, FarClipPlane, IsGL);

        m_ProjMatrix = float4x4::Projection(m_ProjAttribs.FOV, m_ProjAttribs.AspectRatio, m_ProjAttribs.NearClipPlane,
                                            m_ProjAttribs.FarClipPlane, IsGL);
    }

    void FirstPersonCamera::SetSpeedUpScales(Float32 SpeedUpScale, Float32 SuperSpeedUpScale)
    {
        m_fSpeedUpScale = SpeedUpScale;
        m_fSuperSpeedUpScale = SuperSpeedUpScale;
    }

    void FirstPersonCamera::AddSpeedUpScale()
    {
        m_fSpeedUpScale += 2.0f;
        m_fSuperSpeedUpScale += 2.0f;
    }

    void FirstPersonCamera::DecreaseSpeedUpScale()
    {
        m_fSpeedUpScale -= 2.0f;
        m_fSuperSpeedUpScale -= 2.0f;
    }

    eastl::array<float4x4, 3> FirstPersonCamera::getSliceViewProjMatrix(const float3& lightDir)
    {
        eastl::array<float4x4, 3> slicesVP;
        float near = m_ProjAttribs.NearClipPlane;

        for (int i = 0; i < m_slicesFarShadow.size(); ++i)
        {
            auto center = float3(0);
            float far = m_slicesFarShadow[i];
            //We transform the corner of the frustrum from world space to light space, for each cascade
            const auto projMatrix = float4x4::Projection(m_ProjAttribs.FOV,m_ProjAttribs.AspectRatio, near, far, false);
            const auto frustrumCornersWS = getFrustrumCornersWS((m_ViewMatrix * projMatrix).Inverse());

            for (const float3& corner: frustrumCornersWS)
            {
                center += corner;
            }

            center /= frustrumCornersWS.size();

            //Look at
            //TODO fsantoro: add this as an helper
            const auto up = float3(0, 1, 0);
            const auto lightSpaceZ = (lightDir); // todo fsantoro: simplify this

            float3 lightSpaceX = (cross(up, lightSpaceZ)); // right
            float3 lightSpaceY = (cross(lightSpaceZ, lightSpaceX)); // up

            // we are looking down at the center of the cascade

            const auto eye = center + lightDir;
            auto lightView = float4x4 {
                    lightSpaceX.x, lightSpaceY.x, lightSpaceZ.x, 0,
                    lightSpaceX.y, lightSpaceY.y, lightSpaceZ.y, 0,
                    lightSpaceX.z, lightSpaceY.z, lightSpaceZ.z, 0,
                    -dot(lightSpaceX, eye), -dot(lightSpaceY, eye), -dot(lightSpaceZ, eye), 1
            };

           /* const auto t = center + (lightDir);
            glm::vec3 lookGLM = glm::vec3(t.x, t.y, t.z);
            glm::vec3 centerGLM = glm::vec3(center.x, center.y, center.z);
            auto testMat = glm::lookAt( lookGLM, centerGLM, glm::vec3(0, 1, 0));
           // testMat = glm::transpose(testMat);

            memcpy_s(lightView.Data(), sizeof(float) * 16, &testMat, sizeof(float) * 16);*/
            //transform from world space to light space to find how big the camera frustrum should be
            auto min = float3(eastl::numeric_limits<float>::max());
            auto max = float3(eastl::numeric_limits<float>::lowest());

            for (const auto &corner: frustrumCornersWS)
            {
                //todo fsantoro add the eastl version of this
                const float3 cornerLightSpace =  float4(corner, 1) * lightView;
                min = std::min(min, cornerLightSpace);
                max = std::max(max, cornerLightSpace);
            }

            //this scales the ortho view to include stuff behind the camera too
           /* constexpr auto zMultiplier = 10.0f;
            constexpr float invZMultiplier = 1 / zMultiplier;

            min.z *= min.z < 0 ? zMultiplier : invZMultiplier;
            max.z *= max.z < 0 ? zMultiplier : invZMultiplier;*/

            // Tune this parameter according to the scene
            constexpr float zMult = 10.0f;
            if (min.z < 0)
            {
                min.z *= zMult;
            }
            else
            {
                min.z /= zMult;
            }
            if (min.z < 0)
            {
                min.z /= zMult;
            }
            else
            {
                min.z *= zMult;
            }

            const auto lightProj = float4x4::OrthoOffCenter(min.x, max.x, min.y, max.y, min.z, max.z, GetProjAttribs().IsGL);
           // float4x4 lightProjDili{1};

            //memcpy_s(lightProjDili.Data(), sizeof(float) * 16, &lightProj, sizeof(float) * 16);

            //const auto ortho = glm::ortho(min.x, max.x, min.y, max.y, 0.1f, max.z - min.z);

            slicesVP[i] = (lightView * lightProj);

            near += far;
        }

        return slicesVP;
    }

} // namespace Diligent
