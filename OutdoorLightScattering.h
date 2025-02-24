/////////////////////////////////////////////////////////////////////////////////////////////
// Copyright 2017 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
/////////////////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <d3dx9math.h>

#include "Structures.fxh"
// Temporary:
#undef float2
#undef float3
#undef float4

#include <stdio.h>

#include "CPUT_DX11.h"
#include "CPUTMaterial.h"

#include <D3D11.h> // for D3D11_BUFFER_DESC
#include <xnamath.h> // for XMFLOAT

#include <time.h> // for srand(time)

#include "EarthHemisphere.h"
#include "ElevationDataSource.h"
#include "LightSctrPostProcess.h"

struct FrameConstantBuffer
{
    XMFLOAT4 Eye;
    XMFLOAT4 LookAt;
    XMFLOAT4 Up;
    XMFLOAT4 LightDirection;  

	XMMATRIX  worldMatrix;
    XMMATRIX  viewMatrix;	
	XMMATRIX  projectionMatrix;
};

static const CPUTControlID ID_MAIN_PANEL = 10;
static const CPUTControlID ID_ADDITIONAL_ATTRIBS_PANEL = 20;
static const CPUTControlID ID_TONE_MAPPING_ATTRIBS_PANEL = 30;
static const CPUTControlID ID_HELP_TEXT_PANEL = 40;
static const CPUTControlID ID_IGNORE_CONTROL_ID = -1;
static const CPUTControlID CONTROL_PANEL_IDS[] = {ID_MAIN_PANEL, ID_ADDITIONAL_ATTRIBS_PANEL, ID_TONE_MAPPING_ATTRIBS_PANEL};

enum CONTROL_IDS
{
    ID_SELECT_PANEL_COMBO = 100,
    ID_FULLSCREEN_BUTTON,
    ID_ENABLE_VSYNC,
    ID_ENABLE_LIGHT_SCATTERING,
    ID_ENABLE_LIGHT_SHAFTS,
    ID_ANIMATE_SUN,
    ID_LIGHT_SCTR_TECHNIQUE,
    ID_NUM_INTEGRATION_STEPS,
    ID_NUM_EPIPOLAR_SLICES,
    ID_NUM_SAMPLES_IN_EPIPOLAR_SLICE,
    ID_INITIAL_SAMPLE_STEP_IN_EPIPOLAR_SLICE,
    ID_EPIPOLE_SAMPLING_DENSITY_FACTOR,
    ID_REFINEMENT_THRESHOLD,
    ID_SHOW_SAMPLING,
    ID_MIN_MAX_SHADOW_MAP_OPTIMIZATION,
    ID_OPTIMIZE_SAMPLE_LOCATIONS,
    ID_CORRECT_SCATTERING_AT_DEPTH_BREAKS,
    ID_SCATTERING_SCALE,
    ID_MIDDLE_GRAY,
    ID_WHITE_POINT,
    ID_LUM_SATURATION,
    ID_AUTO_EXPOSURE,
    ID_TONE_MAPPING_MODE,
    ID_LIGHT_ADAPTATION,
    ID_SHOW_DEPTH_BREAKS,   
    ID_SHOW_LIGHTING_ONLY_CHECK,
    ID_SHADOW_MAP_RESOLUTION,
    ID_USE_CUSTOM_SCTR_COEFFS_CHECK,
    ID_RLGH_COLOR_BTN,
    ID_MIE_COLOR_BTN,
    ID_SINGLE_SCTR_MODE_DROPDOWN,
    ID_MULTIPLE_SCTR_MODE_DROPDOWN,
    ID_NUM_CASCADES_DROPDOWN,
    ID_SHOW_CASCADES_CHECK,
    ID_SMOOTH_SHADOWS_CHECK,
    ID_BEST_CASCADE_SEARCH_CHECK,
    ID_CASCADE_PARTITIONING_SLIDER,
    ID_CASCADE_PROCESSING_MODE_DROPDOWN,
    ID_FIRST_CASCADE_TO_RAY_MARCH_DROPDOWN,
    ID_REFINEMENT_CRITERION_DROPDOWN,
    ID_EXTINCTION_EVAL_MODE_DROPDOWN,
    ID_MIN_MAX_MIP_FORMAT_DROPDOWN,
    ID_AEROSOL_DENSITY_SCALE_SLIDER,
    ID_AEROSOL_ABSORBTION_SCALE_SLIDER,
    ID_TEXTLINES = 1000
};



// DirectX 11 Sample
//-----------------------------------------------------------------------------
class COutdoorLightScatteringSample:public CPUT_DX11
{
public:
    COutdoorLightScatteringSample();
    virtual ~COutdoorLightScatteringSample();

    // Event handling
    virtual CPUTEventHandledCode HandleKeyboardEvent(CPUTKey key);
    virtual CPUTEventHandledCode HandleMouseEvent(int x, int y, int wheel, CPUTMouseState state);
    virtual void                 HandleCallbackEvent( CPUTEventID Event, CPUTControlID ControlID, CPUTControl* pControl );

    // 'callback' handlers for rendering events.  Derived from CPUT_DX11
    virtual void Create();
    virtual void Render(double deltaSeconds);
    virtual void Update(double deltaSeconds);
    virtual void ResizeWindow(UINT width, UINT height);
    
    void InitializeGUI();
    void CreateDefaultRenderStatesBlock();
    void CreateViewCamera(int screenWidth, int screenHeight);
    void CreateLightCamera();
    bool CreateElevationDatSource();
    void CreateEarthHemisphere();
    void CreateLightAttribsConstantBuffer();
    D3DXVECTOR2 GetCascadeZRange(const SShadowMapAttribs &ShadowMapAttribs, int Cascade, const D3DXVECTOR2 &ViewCamNearFarPlane);
    D3DXMATRIX CalculateCascadeProjToLight(const D3DXVECTOR2 &CascadeZNearFar, const D3DXMATRIX &WorldToLightSpace);
    void CalculateCascadeRange(int Cascade, const D3DXMATRIX &CascadeFrustumProjSpaceToLightSpace, D3DXVECTOR3 &MinXYZ, D3DXVECTOR3 &MaxXYZ);
    void FillCascadeAttributes(SCascadeAttribs &CascadeAttribs, int CacadeInd,  const D3DXMATRIX &CascadeProj, const D3DXVECTOR2 &CascadeZNearFar);
    SLightAttribs UpdateLightAttributes(const SShadowMapAttribs &ShadowAttribs);
    void UpdatePostProcessingAttribs();
    void UpdateCameraPos(float deltaSeconds);
    void CorrectCameraNearAndFarPalens();
    SFrameAttribs GetFrameAttributes(float deltaSeconds, SLightAttribs *LightAttribs);

    void Shutdown();

    HRESULT ParseConfigurationFile( LPCWSTR ConfigFilePath );

private:

    HRESULT CreateShadowMap(ID3D11Device* pd3dDevice);
    void ReleaseShadowMap();

    HRESULT CreateTmpBackBuffAndDepthBuff(ID3D11Device* pd3dDevice);
    void ReleaseTmpBackBuffAndDepthBuff();
    
    void RenderShadowMap(SShadowMapAttribs &ShadowMapAttribs);

    void Destroy();

    float GetSceneExtent();

    class CLightSctrPostProcess *m_pLightSctrPP;

    UINT m_uiShadowMapResolution;
    float m_fCascadePartitioningFactor;
    bool m_bEnableLightScattering;
    bool m_bAnimateSun;

    static const int m_iMinEpipolarSlices = 32;
    static const int m_iMaxEpipolarSlices = 2048;
    static const int m_iMinSamplesInEpipolarSlice = 32;
    static const int m_iMaxSamplesInEpipolarSlice = 2048;
    static const int m_iMaxEpipoleSamplingDensityFactor = 32;
    static const int m_iMinInitialSamplesInEpipolarSlice = 8;
    SPostProcessingAttribs m_PPAttribs;
    float m_fScatteringScale;

    std::vector< CComPtr<ID3D11DepthStencilView> > m_pShadowMapDSVs;
    CComPtr<ID3D11ShaderResourceView> m_pShadowMapSRV; 

    CPUTRenderTargetColor*  m_pOffscreenRenderTarget;
    CPUTRenderTargetDepth*  m_pOffscreenDepth;

    CPUTCamera*           m_pDirectionalLightCamera;
    CPUTCamera*           m_pDirLightOrienationCamera;
    CPUTCameraController* mpCameraController;
    CPUTCameraController* m_pLightController;

    CPUTTimerWin          m_Timer;
    float                 m_fElapsedTime;

    D3DXVECTOR4 m_f4LightColor;
    
    int m_iGUIMode;
    
    SRenderingParams m_TerrainRenderParams;
	std::wstring m_strRawDEMDataFile;
	std::wstring m_strMtrlMaskFile;
    std::wstring m_strTileTexPaths[CEarthHemsiphere::NUM_TILE_TEXTURES];
    std::wstring m_strNormalMapTexPaths[CEarthHemsiphere::NUM_TILE_TEXTURES];

	std::auto_ptr<CElevationDataSource> m_pElevDataSource;

    CEarthHemsiphere m_EarthHemisphere;

    D3DXMATRIX  m_CameraViewMatrix;
    D3DXMATRIX  m_CameraProjMatrix;
    D3DXMATRIX  m_CameraViewProjMatrix;
	D3DXVECTOR3 m_CameraPos;

    CComPtr<ID3D11Buffer> m_pcbLightAttribs;
    UINT m_uiBackBufferWidth, m_uiBackBufferHeight;

    CPUTDropdown* m_pSelectPanelDropDowns[3];
    UINT m_uiSelectedPanelInd;

    float m_fMinElevation, m_fMaxElevation;
private:
    COutdoorLightScatteringSample(const COutdoorLightScatteringSample&);
    const COutdoorLightScatteringSample& operator = (const COutdoorLightScatteringSample&);
};
