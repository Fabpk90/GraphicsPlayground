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

#pragma once


#include <EASTL/vector.h>
#include <EASTL/array.h>
#include "InputController.hpp"
#include "Common/interface/BasicMath.hpp"
#include "GraphicsTypes.h"

namespace Diligent
{

class FirstPersonCamera
{
public:
    void Update(InputController& Controller, float ElapsedTime);
    void SetRotation(float Yaw, float Pitch);
    void SetLookAt(const float3& LookAt);
    void SetMoveSpeed(float MoveSpeed) { m_fMoveSpeed = MoveSpeed; }
    void SetRotationSpeed(float RotationSpeed) { m_fRotationSpeed = RotationSpeed; }
    void SetPos(const float3& Pos) { m_Pos = Pos; }

    // AspectRatio = width / height accounting for surface pretransform
    // (i.e. logical width / logical height)
    void SetProjAttribs(Float32           NearClipPlane,
                        Float32           FarClipPlane,
                        Float32           AspectRatio,
                        Float32           FOV,
                        SURFACE_TRANSFORM SrfPreTransform,
                        bool              IsGL);
    void SetSpeedUpScales(Float32 SpeedUpScale, Float32 SuperSpeedUpScale);

    void AddSpeedUpScale();
    void DecreaseSpeedUpScale();


    // clang-format off
    const float4x4& GetViewMatrix()  const { return m_ViewMatrix;  }
    const float4x4& GetWorldMatrix() const { return m_WorldMatrix; }
    const float4x4& GetProjMatrix()  const { return m_ProjMatrix;  }

    float3 GetWorldRight() const { return float3(m_ViewMatrix._11, m_ViewMatrix._21, m_ViewMatrix._31); }
    float3 GetWorldUp()    const { return float3(m_ViewMatrix._12, m_ViewMatrix._22, m_ViewMatrix._32); }
    float3 GetWorldAhead() const { return float3(m_ViewMatrix._13, m_ViewMatrix._23, m_ViewMatrix._33); }
    // clang-format on

    float3 GetPos() const { return m_Pos; }
    float  GetCurrentSpeed() const { return m_fCurrentSpeed; }

    struct ProjectionAttribs
    {
        Float32           NearClipPlane = 1.f;
        Float32           FarClipPlane  = 1000.f;
        Float32           AspectRatio   = 1.f;
        Float32           FOV           = PI_F / 4.f;
        SURFACE_TRANSFORM PreTransform  = SURFACE_TRANSFORM_IDENTITY;
        bool              IsGL          = false;
    };
    const ProjectionAttribs& GetProjAttribs() { return m_ProjAttribs; }

    void SetReferenceAxes(const float3& ReferenceRightAxis, const float3& ReferenceUpAxis, bool IsRightHanded = false);

    void SetHandness(bool IsRightHanded)
    {
        m_fHandness = IsRightHanded ? +1.f : -1.f;
    }

    eastl::array<float4x4, 3> getSliceViewProjMatrix(const float3& lightDir);

    static eastl::array<float3, 8> getFrustrumCornersWS(const float4x4& cameraInvProjViewMatrix)
    {
        eastl::array<float3, 8> frustrumCorners;

        for (uint x = 0; x < 2; ++x)
        {
            for (uint y = 0; y < 2; ++y)
            {
                for (uint z = 0; z < 2; ++z)
                {
                    // NDC[-1; 1] to WS
                    // But only Gl has -1 for Z, dx 12 and VK have [0;1]
                    const float4 point =  float4(
                            2.0f * x - 1.0f,
                            2.0f * y - 1.0f,
                            z,
                            1.0f) * cameraInvProjViewMatrix;
                    frustrumCorners[z + y * 2 + x * 2 * 2] = (point / point.w);
                }
            }
        }

        return frustrumCorners;
    }

    [[nodiscard]] static constexpr uint getNbCascade() { return m_slicesFarShadow.size();}
    static eastl::array<float, 3> getCascadeFarPlane()
    {
        eastl::array<float, 3> cascades{};

        for (int i = 0; i < cascades.size(); ++i)
        {
            //todo fsantoro fix this ! (static and hardcoded value)
            cascades[i] = m_slicesFarShadow[i] / 1000.0f;
        }
        return cascades;
    }
    inline static eastl::array<float, 3> m_slicesFarShadow;
protected:

    [[nodiscard]] float4x4 GetReferenceRotation() const;

    ProjectionAttribs m_ProjAttribs;

    MouseState m_LastMouseState;

    float3 m_ReferenceRightAxis = float3{1, 0, 0};
    float3 m_ReferenceUpAxis    = float3{0, 1, 0};
    float3 m_ReferenceAheadAxis = float3{0, 0, 1};

    float3 m_Pos;

    float4x4 m_ViewMatrix;
    float4x4 m_WorldMatrix;
    float4x4 m_ProjMatrix;
    float    m_fRotationSpeed = 0.01f;
    float    m_fMoveSpeed     = 1.f;
    float    m_fCurrentSpeed  = 0.f;

    // This is for debug, 0 means fps, others mean directional camera
    int m_cameraIndex = 0;

    float m_fYawAngle          = 0; // Yaw angle of camera
    float m_fPitchAngle        = 0; // Pitch angle of camera
    float m_fSpeedUpScale      = 1.f;
    float m_fSuperSpeedUpScale = 1.f;
    float m_fHandness          = 1.f; // -1 - left handed
                                      // +1 - right handed
};

} // namespace Diligent
