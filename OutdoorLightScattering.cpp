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
#include "stdafx.h"
#include "OutdoorLightScattering.h"
#include <iomanip>
#include "CPUTBufferDX11.h"
#include <Commdlg.h>

// important to use the right CPUT namespace

extern float3 gLightDir;

void UpdateConstantBuffer(ID3D11DeviceContext *pDeviceCtx, ID3D11Buffer *pCB, const void *pData, size_t DataSize);

class CParallelLightCamera : public CPUTCamera
{
public:
    virtual void Update( float deltaSeconds=0.0f ) {
        mView = inverse(*GetWorldMatrix());
    };
};

// Constructor
//-----------------------------------------------------------------------------
COutdoorLightScatteringSample::COutdoorLightScatteringSample() : 
    mpCameraController(NULL),
    m_pLightController(NULL),
    m_uiShadowMapResolution( 1024 ),
    m_fCascadePartitioningFactor(0.95f),
    m_bEnableLightScattering(true),
    m_bAnimateSun(false),
    m_pOffscreenRenderTarget(NULL),
    m_pOffscreenDepth(NULL),
    m_fScatteringScale(0.5f),
    m_f4LightColor(1.f, 1.f, 1.f, 1.f),
    m_iGUIMode(1),
    m_uiBackBufferWidth(0), 
    m_uiBackBufferHeight(0),
    m_uiSelectedPanelInd(0),
    m_CameraPos(0,0,0)
{
    m_pLightSctrPP = new CLightSctrPostProcess;
}

// Destructor
//-----------------------------------------------------------------------------
COutdoorLightScatteringSample::~COutdoorLightScatteringSample()
{
    Destroy();

    SAFE_DELETE( m_pLightSctrPP );
}

void COutdoorLightScatteringSample::Destroy()
{
    m_EarthHemisphere.OnD3D11DestroyDevice();

    ReleaseShadowMap();
    ReleaseTmpBackBuffAndDepthBuff();

    m_pLightSctrPP->OnDestroyDevice();

    SAFE_RELEASE(m_pDirectionalLightCamera);
    SAFE_RELEASE(m_pDirLightOrienationCamera);
    
    SAFE_RELEASE(mpCamera);
    SAFE_DELETE( mpCameraController);

    SAFE_DELETE( m_pLightController);
    m_pcbLightAttribs.Release();
}

// Handle keyboard events
//-----------------------------------------------------------------------------
CPUTEventHandledCode COutdoorLightScatteringSample::HandleKeyboardEvent(CPUTKey key)
{
    CPUTEventHandledCode    handled = CPUT_EVENT_UNHANDLED;
    CPUTGuiControllerDX11*      pGUI = CPUTGetGuiController(); 
    cString filename;

    switch(key)
    {
    case KEY_F1:
        ++m_iGUIMode;
        if( m_iGUIMode>2 )
            m_iGUIMode = 0;
        if(m_iGUIMode==1)
            pGUI->SetActivePanel(CONTROL_PANEL_IDS[m_uiSelectedPanelInd]);
        else if(m_iGUIMode==2)
            pGUI->SetActivePanel(ID_HELP_TEXT_PANEL);
        handled = CPUT_EVENT_HANDLED;
        break;
    
    case KEY_ESCAPE:
        handled = CPUT_EVENT_HANDLED;
        Shutdown();
        break;
    }

    if((handled == CPUT_EVENT_UNHANDLED) && mpCameraController)
    {
        handled = mpCameraController->HandleKeyboardEvent(key);
    }

    if((handled == CPUT_EVENT_UNHANDLED) && m_pLightController)
    {
        handled = m_pLightController->HandleKeyboardEvent(key);
    }

    return handled;
}

// Handle mouse events
//-----------------------------------------------------------------------------
CPUTEventHandledCode COutdoorLightScatteringSample::HandleMouseEvent(int x, int y, int wheel, CPUTMouseState state)
{
    CPUTEventHandledCode handled = CPUT_EVENT_UNHANDLED;
    if( mpCameraController )
    {
        handled = mpCameraController->HandleMouseEvent(x,y,wheel, state);
    }
    if( (handled == CPUT_EVENT_UNHANDLED) && m_pLightController )
    {
        handled = m_pLightController->HandleMouseEvent(x,y,wheel, state);
    }
    return handled;
}

bool SelectColor(D3DXVECTOR4 &f4Color)
{
    // Create an initial color from the current value
    COLORREF InitColor = RGB( f4Color.x * 255.f, f4Color.y * 255.f, f4Color.z * 255.f );
    COLORREF CustomColors[16];
    // Now create a choose color structure with the original color as the default
    CHOOSECOLOR ChooseColorInitStruct;
    ZeroMemory( &ChooseColorInitStruct, sizeof(ChooseColorInitStruct));
    CPUTOSServices::GetOSServices()->GetWindowHandle( &ChooseColorInitStruct.hwndOwner );
    ChooseColorInitStruct.lStructSize = sizeof(ChooseColorInitStruct);
    ChooseColorInitStruct.rgbResult = InitColor;
    ChooseColorInitStruct.Flags = CC_ANYCOLOR | CC_FULLOPEN | CC_RGBINIT;
    ChooseColorInitStruct.lpCustColors = CustomColors;
    // Display a color selection dialog box and if the user does not cancel, select a color
    if( ChooseColor( &ChooseColorInitStruct ) && ChooseColorInitStruct.rgbResult != 0)
    {
        // Get the new color
        COLORREF NewColor = ChooseColorInitStruct.rgbResult;
        f4Color.x = (float)(NewColor&0x0FF) / 255.f;
        f4Color.y = (float)((NewColor>>8)&0x0FF) / 255.f;
        f4Color.z = (float)((NewColor>>16)&0x0FF) / 255.f;
        f4Color.w = 0;
        return true;
    }

    return false;
}

// Handle any control callback events
//-----------------------------------------------------------------------------
void COutdoorLightScatteringSample::HandleCallbackEvent( CPUTEventID Event, CPUTControlID ControlID, CPUTControl* pControl )
{
    cString SelectedItem;
    CPUTGuiControllerDX11*  pGUI            = CPUTGetGuiController();   
    switch(ControlID)
    {
    case ID_SELECT_PANEL_COMBO:
        {
            CPUTDropdown* pDropDown = static_cast<CPUTDropdown*>(pControl);
            pDropDown->GetSelectedItem(m_uiSelectedPanelInd);
            for(int i = 0; i < _countof(m_pSelectPanelDropDowns); ++i)
                m_pSelectPanelDropDowns[i]->SetSelectedItem(m_uiSelectedPanelInd+1);
            pGUI->SetActivePanel(CONTROL_PANEL_IDS[m_uiSelectedPanelInd]);
            break;
        }

    case ID_FULLSCREEN_BUTTON:
        CPUTToggleFullScreenMode();
        pGUI->GetControl(ID_RLGH_COLOR_BTN)->SetEnable(!CPUTGetFullscreenState() && m_PPAttribs.m_bUseCustomSctrCoeffs);
        pGUI->GetControl(ID_MIE_COLOR_BTN)->SetEnable(!CPUTGetFullscreenState() && m_PPAttribs.m_bUseCustomSctrCoeffs);
        break;
    
    case ID_ENABLE_VSYNC:
        {
            CPUTCheckbox *pCheckBox = static_cast<CPUTCheckbox*>(pControl);
            mSyncInterval = pCheckBox->GetCheckboxState() == CPUT_CHECKBOX_CHECKED ? 1 : 0;
            break;
        }
    
    case ID_ENABLE_LIGHT_SCATTERING:
        {
            CPUTCheckbox *pCheckBox = static_cast<CPUTCheckbox*>(pControl);
            m_bEnableLightScattering = pCheckBox->GetCheckboxState() == CPUT_CHECKBOX_CHECKED;
            break;
        }

    case ID_ANIMATE_SUN:
        {
            CPUTCheckbox *pCheckBox = static_cast<CPUTCheckbox*>(pControl);
            m_bAnimateSun = pCheckBox->GetCheckboxState() == CPUT_CHECKBOX_CHECKED;
            break;
        }

    case ID_ENABLE_LIGHT_SHAFTS:
        {
            CPUTCheckbox *pCheckBox = static_cast<CPUTCheckbox*>(pControl);
            m_PPAttribs.m_bEnableLightShafts = pCheckBox->GetCheckboxState() == CPUT_CHECKBOX_CHECKED;
            pGUI->GetControl(ID_NUM_INTEGRATION_STEPS)->SetEnable(!m_PPAttribs.m_bEnableLightShafts && m_PPAttribs.m_uiSingleScatteringMode == SINGLE_SCTR_MODE_INTEGRATION);
            break;
        }

    case ID_LIGHT_SCTR_TECHNIQUE:
        {
            CPUTDropdown* pDropDown = static_cast<CPUTDropdown*>(pControl);
            UINT uiSelectedItem;
            pDropDown->GetSelectedItem(uiSelectedItem);
            m_PPAttribs.m_uiLightSctrTechnique = uiSelectedItem;
            bool bIsEpipolarSampling = m_PPAttribs.m_uiLightSctrTechnique == LIGHT_SCTR_TECHNIQUE_EPIPOLAR_SAMPLING;
            pGUI->GetControl(ID_NUM_EPIPOLAR_SLICES)->SetEnable(bIsEpipolarSampling);
            pGUI->GetControl(ID_NUM_SAMPLES_IN_EPIPOLAR_SLICE)->SetEnable(bIsEpipolarSampling);
            pGUI->GetControl(ID_INITIAL_SAMPLE_STEP_IN_EPIPOLAR_SLICE)->SetEnable(bIsEpipolarSampling);
            pGUI->GetControl(ID_EPIPOLE_SAMPLING_DENSITY_FACTOR)->SetEnable(bIsEpipolarSampling);
            pGUI->GetControl(ID_REFINEMENT_THRESHOLD)->SetEnable(bIsEpipolarSampling);
            pGUI->GetControl(ID_MIN_MAX_SHADOW_MAP_OPTIMIZATION)->SetEnable(bIsEpipolarSampling);
            pGUI->GetControl(ID_OPTIMIZE_SAMPLE_LOCATIONS)->SetEnable(bIsEpipolarSampling);
            pGUI->GetControl(ID_SHOW_SAMPLING)->SetEnable(bIsEpipolarSampling);
            pGUI->GetControl(ID_CORRECT_SCATTERING_AT_DEPTH_BREAKS)->SetEnable(bIsEpipolarSampling);
            pGUI->GetControl(ID_SHOW_DEPTH_BREAKS)->SetEnable(bIsEpipolarSampling);
            break;
        }

    case ID_SHADOW_MAP_RESOLUTION:
        {
            CPUTDropdown* pDropDown = static_cast<CPUTDropdown*>(pControl);
            UINT uiSelectedItem;
            pDropDown->GetSelectedItem(uiSelectedItem);
            // uiSelectedItem is 1-based
            m_uiShadowMapResolution = 512 << uiSelectedItem;
            CreateShadowMap(mpD3dDevice);
            break;
        }

    case ID_NUM_EPIPOLAR_SLICES:
        {
            CPUTSlider* pSlider = static_cast<CPUTSlider*>(pControl);
            float fSliderVal;
            pSlider->GetValue(fSliderVal);
            m_PPAttribs.m_uiNumEpipolarSlices = 1 << (int)fSliderVal;
            std::wstringstream NumSlicesSS;
            NumSlicesSS << "Epipolar slices: " << m_PPAttribs.m_uiNumEpipolarSlices;
            pSlider->SetText( NumSlicesSS.str().c_str() );
            break;
        }
    
    case ID_NUM_INTEGRATION_STEPS:
        {
            CPUTSlider* pSlider = static_cast<CPUTSlider*>(pControl);
            float fSliderVal;
            pSlider->GetValue(fSliderVal);
            m_PPAttribs.m_uiInstrIntegralSteps = (int)fSliderVal;
            std::wstringstream NumStepsSS;
            NumStepsSS << "Num integration steps: " << m_PPAttribs.m_uiInstrIntegralSteps;
            pSlider->SetText( NumStepsSS.str().c_str() );
            break;
        }

    case ID_NUM_SAMPLES_IN_EPIPOLAR_SLICE:
        {
            CPUTSlider* pNumSamplesSlider = static_cast<CPUTSlider*>(pControl);
            float fSliderVal;
            pNumSamplesSlider->GetValue(fSliderVal);
            m_PPAttribs.m_uiMaxSamplesInSlice = 1 << (int)fSliderVal;
            std::wstringstream NumSamplesSS;
            NumSamplesSS << "Total samples in slice: " << m_PPAttribs.m_uiMaxSamplesInSlice;
            pNumSamplesSlider->SetText( NumSamplesSS.str().c_str() );

            {
                CPUTSlider* pInitialSamplesSlider = static_cast<CPUTSlider*>(pGUI->GetControl(ID_INITIAL_SAMPLE_STEP_IN_EPIPOLAR_SLICE));
                unsigned long ulStartVal=0, ulEndVal, ulCurrVal;
                BitScanForward(&ulEndVal, m_PPAttribs.m_uiMaxSamplesInSlice / m_iMinInitialSamplesInEpipolarSlice);
                pInitialSamplesSlider->SetScale( (float)ulStartVal, (float)ulEndVal, ulEndVal-ulStartVal+1 );
                if( m_PPAttribs.m_uiInitialSampleStepInSlice > (1u<<ulEndVal) )
                {
                    m_PPAttribs.m_uiInitialSampleStepInSlice = 1 << ulEndVal;
                    std::wstringstream InitialSamplesStepSS;
                    InitialSamplesStepSS << "Initial sample step: " << m_PPAttribs.m_uiInitialSampleStepInSlice;
                    pInitialSamplesSlider->SetText( InitialSamplesStepSS.str().c_str() );
                }
                BitScanForward(&ulCurrVal, m_PPAttribs.m_uiInitialSampleStepInSlice);
                pInitialSamplesSlider->SetValue( (float)ulCurrVal );
            }
            break;
        }

    case ID_INITIAL_SAMPLE_STEP_IN_EPIPOLAR_SLICE:
        {
            CPUTSlider* pSlider = static_cast<CPUTSlider*>(pControl);
            float fSliderVal;
            pSlider->GetValue(fSliderVal);
            m_PPAttribs.m_uiInitialSampleStepInSlice = 1 << (int)fSliderVal;
            std::wstringstream InitialSamplesStepSS;
            InitialSamplesStepSS << "Initial sample step: " << m_PPAttribs.m_uiInitialSampleStepInSlice;
            pSlider->SetText( InitialSamplesStepSS.str().c_str() );

            break;
        }

    case ID_EPIPOLE_SAMPLING_DENSITY_FACTOR:
        {
            CPUTSlider* pSlider = static_cast<CPUTSlider*>(pControl);
            float fSliderVal;
            pSlider->GetValue(fSliderVal);
            m_PPAttribs.m_uiEpipoleSamplingDensityFactor = 1 << (int)fSliderVal;
            std::wstringstream EpipoleSamplingDensitySS;
            EpipoleSamplingDensitySS << "Epipole sampling density: " << m_PPAttribs.m_uiEpipoleSamplingDensityFactor;
            pSlider->SetText( EpipoleSamplingDensitySS.str().c_str() );

            break;
        }

    case ID_REFINEMENT_THRESHOLD:
        {
            CPUTSlider* pSlider = static_cast<CPUTSlider*>(pControl);
            pSlider->GetValue(m_PPAttribs.m_fRefinementThreshold);
            break;
        }

    case ID_SHOW_SAMPLING:
        {
            CPUTCheckbox *pCheckBox = static_cast<CPUTCheckbox*>(pControl);
            m_PPAttribs.m_bShowSampling = pCheckBox->GetCheckboxState() == CPUT_CHECKBOX_CHECKED;
            break;
        }

    case ID_MIN_MAX_SHADOW_MAP_OPTIMIZATION:
        {
            CPUTCheckbox *pCheckBox = static_cast<CPUTCheckbox*>(pControl);
            m_PPAttribs.m_bUse1DMinMaxTree = pCheckBox->GetCheckboxState()== CPUT_CHECKBOX_CHECKED;
            break;
        }

    case ID_CORRECT_SCATTERING_AT_DEPTH_BREAKS:
        {
            CPUTCheckbox *pCheckBox = static_cast<CPUTCheckbox*>(pControl);
            m_PPAttribs.m_bCorrectScatteringAtDepthBreaks = pCheckBox->GetCheckboxState() == CPUT_CHECKBOX_CHECKED;
            break;
        }

    case ID_SCATTERING_SCALE:
        {
            CPUTSlider* pSlider = static_cast<CPUTSlider*>(pControl);
            pSlider->GetValue(m_fScatteringScale);
            break;
        }

    case ID_MIDDLE_GRAY:
        {
            CPUTSlider* pSlider = static_cast<CPUTSlider*>(pControl);
            pSlider->GetValue(m_PPAttribs.m_fMiddleGray);
            break;
        }

    case ID_WHITE_POINT:
        {
            CPUTSlider* pSlider = static_cast<CPUTSlider*>(pControl);
            pSlider->GetValue(m_PPAttribs.m_fWhitePoint);
            break;
        }

    case ID_LUM_SATURATION:
        {
            CPUTSlider* pSlider = static_cast<CPUTSlider*>(pControl);
            pSlider->GetValue(m_PPAttribs.m_fLuminanceSaturation);
            break;
        }

    case ID_AUTO_EXPOSURE:
        {
            CPUTCheckbox *pCheckBox = static_cast<CPUTCheckbox*>(pControl);
            m_PPAttribs.m_bAutoExposure = pCheckBox->GetCheckboxState() == CPUT_CHECKBOX_CHECKED;
            pGUI->GetControl(ID_LIGHT_ADAPTATION)->SetEnable(m_PPAttribs.m_bAutoExposure ? true : false);
            break;
        }
    
    case ID_TONE_MAPPING_MODE:
        {
            CPUTDropdown *pDropDown = static_cast<CPUTDropdown*>(pControl);
            pDropDown->GetSelectedItem(m_PPAttribs.m_uiToneMappingMode);
            pGUI->GetControl(ID_LUM_SATURATION)->SetEnable( 
                                m_PPAttribs.m_uiToneMappingMode == TONE_MAPPING_MODE_EXP ||
                                m_PPAttribs.m_uiToneMappingMode == TONE_MAPPING_MODE_REINHARD ||
                                m_PPAttribs.m_uiToneMappingMode == TONE_MAPPING_MODE_REINHARD_MOD ||
                                m_PPAttribs.m_uiToneMappingMode == TONE_MAPPING_LOGARITHMIC ||
                                m_PPAttribs.m_uiToneMappingMode == TONE_MAPPING_ADAPTIVE_LOG );
            pGUI->GetControl(ID_WHITE_POINT)->SetEnable(  
                                m_PPAttribs.m_uiToneMappingMode == TONE_MAPPING_MODE_REINHARD_MOD ||
                                m_PPAttribs.m_uiToneMappingMode == TONE_MAPPING_MODE_UNCHARTED2 ||
                                m_PPAttribs.m_uiToneMappingMode == TONE_MAPPING_LOGARITHMIC ||
                                m_PPAttribs.m_uiToneMappingMode == TONE_MAPPING_ADAPTIVE_LOG );
            break;
        }

    case ID_LIGHT_ADAPTATION:
        {
            CPUTCheckbox *pCheckBox = static_cast<CPUTCheckbox*>(pControl);
            m_PPAttribs.m_bLightAdaptation = pCheckBox->GetCheckboxState() == CPUT_CHECKBOX_CHECKED;
            break;
        }

    case ID_OPTIMIZE_SAMPLE_LOCATIONS:
        {
            CPUTCheckbox *pCheckBox = static_cast<CPUTCheckbox*>(pControl);
            m_PPAttribs.m_bOptimizeSampleLocations = pCheckBox->GetCheckboxState() == CPUT_CHECKBOX_CHECKED;
            break;
        }

    case ID_SHOW_DEPTH_BREAKS:
        {
            CPUTCheckbox *pCheckBox = static_cast<CPUTCheckbox*>(pControl);
            m_PPAttribs.m_bShowDepthBreaks = pCheckBox->GetCheckboxState() == CPUT_CHECKBOX_CHECKED;
            break;
        }

    case ID_SHOW_LIGHTING_ONLY_CHECK:
        {
            CPUTCheckbox *pCheckBox = static_cast<CPUTCheckbox*>(pControl);
            m_PPAttribs.m_bShowLightingOnly = pCheckBox->GetCheckboxState() == CPUT_CHECKBOX_CHECKED;
            break;
        }

    case ID_USE_CUSTOM_SCTR_COEFFS_CHECK:
        {
            CPUTCheckbox *pCheckBox = static_cast<CPUTCheckbox*>(pControl);
            m_PPAttribs.m_bUseCustomSctrCoeffs = pCheckBox->GetCheckboxState() == CPUT_CHECKBOX_CHECKED;
            pGUI->GetControl(ID_RLGH_COLOR_BTN)->SetEnable(!CPUTGetFullscreenState() && m_PPAttribs.m_bUseCustomSctrCoeffs);
            pGUI->GetControl(ID_MIE_COLOR_BTN)->SetEnable(!CPUTGetFullscreenState() && m_PPAttribs.m_bUseCustomSctrCoeffs);
            break;
        }

    case ID_RLGH_COLOR_BTN:
        {
            const float fRlghColorScale = 5e-5f;
            D3DXVECTOR4 f4RlghColor = m_PPAttribs.m_f4CustomRlghBeta / fRlghColorScale;
            if( SelectColor(f4RlghColor ) )
            {
                m_PPAttribs.m_f4CustomRlghBeta = f4RlghColor * fRlghColorScale;
            }
            break;
        }

    case ID_MIE_COLOR_BTN:
        {
            const float fMieColorScale = 5e-5f;
            D3DXVECTOR4 f4MieColor = m_PPAttribs.m_f4CustomMieBeta / fMieColorScale;
            if( SelectColor(f4MieColor ) )
            {
                m_PPAttribs.m_f4CustomMieBeta = f4MieColor * fMieColorScale;
            }
            break;
        }

    case ID_AEROSOL_DENSITY_SCALE_SLIDER:
        {
            CPUTSlider* pSlider = static_cast<CPUTSlider*>(pControl);
            pSlider->GetValue(m_PPAttribs.m_fAerosolDensityScale);
            break;
        }

    case ID_AEROSOL_ABSORBTION_SCALE_SLIDER:
        {
            CPUTSlider* pSlider = static_cast<CPUTSlider*>(pControl);
            pSlider->GetValue(m_PPAttribs.m_fAerosolAbsorbtionScale);
            break;
        }

    case ID_SINGLE_SCTR_MODE_DROPDOWN:
        {
            CPUTDropdown *pDropDown = static_cast<CPUTDropdown*>(pControl);
            pDropDown->GetSelectedItem(m_PPAttribs.m_uiSingleScatteringMode);
            pGUI->GetControl(ID_NUM_INTEGRATION_STEPS)->SetEnable(!m_PPAttribs.m_bEnableLightShafts && m_PPAttribs.m_uiSingleScatteringMode == SINGLE_SCTR_MODE_INTEGRATION);
            break;
        }

    case ID_MULTIPLE_SCTR_MODE_DROPDOWN:
        {
            CPUTDropdown *pDropDown = static_cast<CPUTDropdown*>(pControl);
            pDropDown->GetSelectedItem(m_PPAttribs.m_uiMultipleScatteringMode);
            break;
        }

    case ID_NUM_CASCADES_DROPDOWN:
        {
            CPUTDropdown *pDropDown = static_cast<CPUTDropdown*>(pControl);
            UINT uiSelectedItem;
            pDropDown->GetSelectedItem(uiSelectedItem);
            m_TerrainRenderParams.m_iNumShadowCascades = uiSelectedItem+1;
            CreateShadowMap(mpD3dDevice);
            m_EarthHemisphere.UpdateParams(m_TerrainRenderParams);
            break;
        }

    case ID_SMOOTH_SHADOWS_CHECK:
        {
            CPUTCheckbox *pCheckBox = static_cast<CPUTCheckbox*>(pControl);
            m_TerrainRenderParams.m_bSmoothShadows = pCheckBox->GetCheckboxState() == CPUT_CHECKBOX_CHECKED ? TRUE : FALSE;
            m_EarthHemisphere.UpdateParams(m_TerrainRenderParams);
            break;
        }

    case ID_BEST_CASCADE_SEARCH_CHECK:
        {
            CPUTCheckbox *pCheckBox = static_cast<CPUTCheckbox*>(pControl);
            m_TerrainRenderParams.m_bBestCascadeSearch = pCheckBox->GetCheckboxState() == CPUT_CHECKBOX_CHECKED ? TRUE : FALSE;
            m_EarthHemisphere.UpdateParams(m_TerrainRenderParams);
            break;
        }

    case ID_CASCADE_PARTITIONING_SLIDER:
        {
            CPUTSlider* pSlider = static_cast<CPUTSlider*>(pControl);
            pSlider->GetValue(m_fCascadePartitioningFactor);
            std::wstringstream SS;
            SS << "Partitioning Factor: " << m_fCascadePartitioningFactor;
            pSlider->SetText( SS.str().c_str() );
            break;
        }

    case ID_CASCADE_PROCESSING_MODE_DROPDOWN:
        {
            CPUTDropdown *pDropDown = static_cast<CPUTDropdown*>(pControl);
            pDropDown->GetSelectedItem(m_PPAttribs.m_uiCascadeProcessingMode);
            break;
        }

    case ID_EXTINCTION_EVAL_MODE_DROPDOWN:
        {
            CPUTDropdown *pDropDown = static_cast<CPUTDropdown*>(pControl);
            pDropDown->GetSelectedItem(m_PPAttribs.m_uiExtinctionEvalMode);
            break;
        }

    case ID_MIN_MAX_MIP_FORMAT_DROPDOWN:
        {
            CPUTDropdown *pDropDown = static_cast<CPUTDropdown*>(pControl);
            UINT uiSelectedItem;
            pDropDown->GetSelectedItem(uiSelectedItem);
            m_PPAttribs.m_bIs32BitMinMaxMipMap = (uiSelectedItem == 1);
            break;
        }


    case ID_REFINEMENT_CRITERION_DROPDOWN:
        {
            CPUTDropdown *pDropDown = static_cast<CPUTDropdown*>(pControl);
            pDropDown->GetSelectedItem(m_PPAttribs.m_uiRefinementCriterion);
            break;
        }

    default:
        break;
    }
}

// Handle resize events
//-----------------------------------------------------------------------------
void COutdoorLightScatteringSample::ResizeWindow(UINT width, UINT height)
{
    if( width == 0 || height == 0 )
        return;
    m_uiBackBufferWidth = width;
    m_uiBackBufferHeight = height;
    // Before we can resize the swap chain, we must release any references to it.
    // We could have a "AssetLibrary::ReleaseSwapChainResources(), or similar.  But,
    // Generic "release all" works, is simpler to implement/maintain, and is not performance critical.
    //pAssetLibrary->ReleaseTexturesAndBuffers();

    CPUT_DX11::ResizeWindow( width, height );

    // Resize any application-specific render targets here
    if( mpCamera ) 
    {
        mpCamera->SetAspectRatio(((float)width)/((float)height));
        mpCamera->Update();
    }

    m_pLightSctrPP->OnResizedSwapChain(mpD3dDevice, width, height );
    m_pOffscreenRenderTarget->RecreateRenderTarget(width, height);
    m_pOffscreenDepth->RecreateRenderTarget(width, height);
}


void COutdoorLightScatteringSample::ReleaseTmpBackBuffAndDepthBuff()
{
    SAFE_DELETE(m_pOffscreenRenderTarget);
    SAFE_DELETE(m_pOffscreenDepth);
}

HRESULT COutdoorLightScatteringSample::CreateTmpBackBuffAndDepthBuff(ID3D11Device* pd3dDevice)
{
    ReleaseTmpBackBuffAndDepthBuff();

    DXGI_SWAP_CHAIN_DESC SwapChainDesc;
    mpSwapChain->GetDesc(&SwapChainDesc);
        
    SAFE_DELETE(m_pOffscreenRenderTarget);
    m_pOffscreenRenderTarget = new CPUTRenderTargetColor();
    m_pOffscreenRenderTarget->CreateRenderTarget( 
        cString( _L("OffscreenRenderTarget") ),
        SwapChainDesc.BufferDesc.Width,                                 //UINT Width
        SwapChainDesc.BufferDesc.Height,                                //UINT Height
        // It is essential to use floating point format for back buffer
        // to avoid banding artifacts at low light conditions
        DXGI_FORMAT_R11G11B10_FLOAT);

    SAFE_DELETE(m_pOffscreenDepth);
    m_pOffscreenDepth = new CPUTRenderTargetDepth();
    m_pOffscreenDepth->CreateRenderTarget( 
        cString( _L("OffscreenDepthBuffer") ),
        SwapChainDesc.BufferDesc.Width,                                 //UINT Width
        SwapChainDesc.BufferDesc.Height,                                //UINT Height
        DXGI_FORMAT_D32_FLOAT ); 


    return D3D_OK;
}

void COutdoorLightScatteringSample::ReleaseShadowMap()
{
    // Check for the existance of a shadow map buffer and if it exists, destroy it and set its pointer to NULL

    // Release the light space depth shader resource and depth stencil views
    m_pShadowMapDSVs.clear();
    m_pShadowMapSRV.Release();
}


HRESULT COutdoorLightScatteringSample::CreateShadowMap(ID3D11Device* pd3dDevice)
{
    HRESULT hr;

    ReleaseShadowMap();

    static const bool bIs32BitShadowMap = true;
	//ShadowMap
	D3D11_TEXTURE2D_DESC ShadowMapDesc =
	{
        m_uiShadowMapResolution,
        m_uiShadowMapResolution,
		1,
		m_TerrainRenderParams.m_iNumShadowCascades,
		bIs32BitShadowMap ? DXGI_FORMAT_R32_TYPELESS : DXGI_FORMAT_R16_TYPELESS,
        {1,0},
		D3D11_USAGE_DEFAULT,
		D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_DEPTH_STENCIL,
		0,
		0
	};

	CComPtr<ID3D11Texture2D> ptex2DShadowMap;
	V_RETURN(pd3dDevice->CreateTexture2D(&ShadowMapDesc, NULL, &ptex2DShadowMap));

	D3D11_SHADER_RESOURCE_VIEW_DESC ShadowMapSRVDesc;
    ZeroMemory( &ShadowMapSRVDesc, sizeof(ShadowMapSRVDesc) );
    ShadowMapSRVDesc.Format = bIs32BitShadowMap ? DXGI_FORMAT_R32_FLOAT : DXGI_FORMAT_R16_UNORM;
    ShadowMapSRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
	ShadowMapSRVDesc.Texture2DArray.MostDetailedMip = 0;
	ShadowMapSRVDesc.Texture2DArray.MipLevels = 1;
    ShadowMapSRVDesc.Texture2DArray.FirstArraySlice = 0;
    ShadowMapSRVDesc.Texture2DArray.ArraySize = ShadowMapDesc.ArraySize;

    V_RETURN(pd3dDevice->CreateShaderResourceView(ptex2DShadowMap, &ShadowMapSRVDesc, &m_pShadowMapSRV));

    D3D11_DEPTH_STENCIL_VIEW_DESC ShadowMapDSVDesc;
    ZeroMemory( &ShadowMapDSVDesc, sizeof(ShadowMapDSVDesc) );
    ShadowMapDSVDesc.Format = bIs32BitShadowMap ? DXGI_FORMAT_D32_FLOAT : DXGI_FORMAT_D16_UNORM;
    ShadowMapDSVDesc.Flags = 0;
    ShadowMapDSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
    ShadowMapDSVDesc.Texture2DArray.MipSlice = 0;
    ShadowMapDSVDesc.Texture2DArray.ArraySize = 1;
    m_pShadowMapDSVs.resize(ShadowMapDesc.ArraySize);
    for(UINT iArrSlice=0; iArrSlice < ShadowMapDesc.ArraySize; iArrSlice++)
    {
        ShadowMapDSVDesc.Texture2DArray.FirstArraySlice = iArrSlice;
        V_RETURN(pd3dDevice->CreateDepthStencilView(ptex2DShadowMap, &ShadowMapDSVDesc, &m_pShadowMapDSVs[iArrSlice]));
    }

    return D3D_OK;
}

float COutdoorLightScatteringSample::GetSceneExtent()
{
    return 10000;
}

void COutdoorLightScatteringSample::InitializeGUI()
{
    CPUTGuiControllerDX11*  pGUI            = CPUTGetGuiController();

    pGUI->DrawFPS(true);

    static_assert( _countof(m_pSelectPanelDropDowns) == _countof(CONTROL_PANEL_IDS), "Incorrect size of m_pSelectPanelDropDowns" );
    for(int iPanel=0; iPanel < _countof(CONTROL_PANEL_IDS); ++iPanel)
    {
        pGUI->CreateDropdown( L"Controls: basic", ID_SELECT_PANEL_COMBO, CONTROL_PANEL_IDS[iPanel], &m_pSelectPanelDropDowns[iPanel]);
        m_pSelectPanelDropDowns[iPanel]->AddSelectionItem( L"Controls: additional" );
        m_pSelectPanelDropDowns[iPanel]->AddSelectionItem( L"Controls: tone mapping" );
        // SelectedItem is 1-based
        m_pSelectPanelDropDowns[iPanel]->SetSelectedItem(m_uiSelectedPanelInd+1);
    }


    CPUTButton* pButton = NULL;
    pGUI->CreateButton(_L("Fullscreen"), ID_FULLSCREEN_BUTTON, ID_MAIN_PANEL, &pButton);
    
    {
        CPUTCheckbox *pCheckBox = NULL;
        pGUI->CreateCheckbox( L"VSync", ID_ENABLE_VSYNC, ID_MAIN_PANEL, &pCheckBox);
        pCheckBox->SetCheckboxState( mSyncInterval ? CPUT_CHECKBOX_CHECKED : CPUT_CHECKBOX_UNCHECKED );
    }

    {
        CPUTCheckbox *pCheckBox = NULL;
        pGUI->CreateCheckbox( L"Enable light scattering", ID_ENABLE_LIGHT_SCATTERING, ID_MAIN_PANEL, &pCheckBox);
        pCheckBox->SetCheckboxState( m_bEnableLightScattering ? CPUT_CHECKBOX_CHECKED : CPUT_CHECKBOX_UNCHECKED );
    }

    {
        CPUTCheckbox *pCheckBox = NULL;
        pGUI->CreateCheckbox( L"Enable light shafts", ID_ENABLE_LIGHT_SHAFTS, ID_MAIN_PANEL, &pCheckBox);
        pCheckBox->SetCheckboxState( m_PPAttribs.m_bEnableLightShafts ? CPUT_CHECKBOX_CHECKED : CPUT_CHECKBOX_UNCHECKED );
    }

    {
        CPUTDropdown* pDropDown = NULL;
        pGUI->CreateDropdown( L"Epipolar sampling", ID_LIGHT_SCTR_TECHNIQUE, ID_MAIN_PANEL, &pDropDown);
        pDropDown->AddSelectionItem( L"Brute force ray marching" );
        // SelectedItem is 1-based
        pDropDown->SetSelectedItem(m_PPAttribs.m_uiLightSctrTechnique+1);
    }

    {
        CPUTDropdown* pDropDown = NULL;
        pGUI->CreateDropdown( L"Shadow Map res: 512x512", ID_SHADOW_MAP_RESOLUTION, ID_MAIN_PANEL, &pDropDown);
        pDropDown->AddSelectionItem( L"Shadow Map res: 1024x1024" );
        pDropDown->AddSelectionItem( L"Shadow Map res: 2048x2048" );
        pDropDown->AddSelectionItem( L"Shadow Map res: 4096x4096" );
        unsigned long ulCurrVal;
        BitScanForward(&ulCurrVal, m_uiShadowMapResolution);
        // SelectedItem is 1-based
        pDropDown->SetSelectedItem(ulCurrVal-9 + 1);
    }

    {
        CPUTSlider* pSlider = NULL;
        std::wstringstream NumStepsSS;
        NumStepsSS << "Num integration steps: " << m_PPAttribs.m_uiInstrIntegralSteps;
        pGUI->CreateSlider( NumStepsSS.str().c_str(), ID_NUM_INTEGRATION_STEPS, ID_MAIN_PANEL, &pSlider);
        pSlider->SetEnable(!m_PPAttribs.m_bEnableLightShafts && m_PPAttribs.m_uiSingleScatteringMode == SINGLE_SCTR_MODE_INTEGRATION);

        unsigned long ulStartVal, ulEndVal;
        BitScanForward(&ulStartVal, m_iMinEpipolarSlices);
        BitScanForward(&ulEndVal, m_iMaxEpipolarSlices);
        pSlider->SetScale( 5, 100, 20 );
        pSlider->SetValue( (float)m_PPAttribs.m_uiInstrIntegralSteps );
    }

    {
        CPUTSlider* pSlider = NULL;
        std::wstringstream NumSlicesSS;
        NumSlicesSS << "Epipolar slices: " << m_PPAttribs.m_uiNumEpipolarSlices;
        pGUI->CreateSlider( NumSlicesSS.str().c_str(), ID_NUM_EPIPOLAR_SLICES, ID_MAIN_PANEL, &pSlider);

        unsigned long ulStartVal, ulEndVal, ulCurrVal;
        BitScanForward(&ulStartVal, m_iMinEpipolarSlices);
        BitScanForward(&ulEndVal, m_iMaxEpipolarSlices);
        pSlider->SetScale( (float)ulStartVal, (float)ulEndVal, ulEndVal-ulStartVal+1 );

        BitScanForward(&ulCurrVal, m_PPAttribs.m_uiNumEpipolarSlices);
        pSlider->SetValue( (float)ulCurrVal );
    }

    {
        CPUTSlider* pSlider = NULL;
        std::wstringstream NumSamplesSS;
        NumSamplesSS << "Total samples in slice: " << m_PPAttribs.m_uiMaxSamplesInSlice;
        pGUI->CreateSlider( NumSamplesSS.str().c_str(), ID_NUM_SAMPLES_IN_EPIPOLAR_SLICE, ID_MAIN_PANEL, &pSlider);

        unsigned long ulStartVal, ulEndVal, ulCurrVal;
        BitScanForward(&ulStartVal, m_iMinSamplesInEpipolarSlice);
        BitScanForward(&ulEndVal, m_iMaxSamplesInEpipolarSlice);
        pSlider->SetScale( (float)ulStartVal, (float)ulEndVal, ulEndVal-ulStartVal+1 );

        BitScanForward(&ulCurrVal, m_PPAttribs.m_uiMaxSamplesInSlice);
        pSlider->SetValue( (float)ulCurrVal );
    }

    {
        CPUTSlider* pSlider = NULL;
        std::wstringstream InitialSamplesStepSS;
        InitialSamplesStepSS << "Initial sample step: " << m_PPAttribs.m_uiInitialSampleStepInSlice;
        pGUI->CreateSlider( InitialSamplesStepSS.str().c_str(), ID_INITIAL_SAMPLE_STEP_IN_EPIPOLAR_SLICE, ID_MAIN_PANEL, &pSlider);

        unsigned long ulStartVal=0, ulEndVal, ulCurrVal;
        BitScanForward(&ulEndVal, m_PPAttribs.m_uiMaxSamplesInSlice / m_iMinInitialSamplesInEpipolarSlice);
        pSlider->SetScale( (float)ulStartVal, (float)ulEndVal, ulEndVal-ulStartVal+1 );

        BitScanForward(&ulCurrVal, m_PPAttribs.m_uiInitialSampleStepInSlice);
        pSlider->SetValue( (float)ulCurrVal );
    }

    {
        CPUTSlider* pSlider = NULL;
        std::wstringstream EpipoleSamplingDensitySS;
        EpipoleSamplingDensitySS << "Epipole sampling density: " << m_PPAttribs.m_uiEpipoleSamplingDensityFactor;
        pGUI->CreateSlider( EpipoleSamplingDensitySS.str().c_str(), ID_EPIPOLE_SAMPLING_DENSITY_FACTOR, ID_MAIN_PANEL, &pSlider);

        unsigned long ulStartVal=0, ulEndVal, ulCurrVal;
        BitScanForward(&ulEndVal, m_iMaxEpipoleSamplingDensityFactor);
        pSlider->SetScale( (float)ulStartVal, (float)ulEndVal, ulEndVal-ulStartVal+1 );

        BitScanForward(&ulCurrVal, m_PPAttribs.m_uiEpipoleSamplingDensityFactor);
        pSlider->SetValue( (float)ulCurrVal );
    }

    {
        CPUTSlider* pSlider = NULL;
        pGUI->CreateSlider( L"Refinement threshold", ID_REFINEMENT_THRESHOLD, ID_MAIN_PANEL, &pSlider);
        pSlider->SetScale( 0.001f, 0.5f, 100 );
        pSlider->SetValue( m_PPAttribs.m_fRefinementThreshold );
    }

    {
        CPUTSlider* pSlider = NULL;
        pGUI->CreateSlider( L"Scattering Scale", ID_SCATTERING_SCALE, ID_MAIN_PANEL, &pSlider);
        pSlider->SetScale( 0.1f, 2.f, 50 );
        pSlider->SetValue( m_fScatteringScale );
    }

    {
        CPUTCheckbox *pCheckBox = NULL;
        pGUI->CreateCheckbox( L"Show sampling", ID_SHOW_SAMPLING, ID_MAIN_PANEL, &pCheckBox);
        pCheckBox->SetCheckboxState( m_PPAttribs.m_bShowSampling ? CPUT_CHECKBOX_CHECKED : CPUT_CHECKBOX_UNCHECKED );
    }

    {
        CPUTCheckbox *pCheckBox = NULL;
        pGUI->CreateCheckbox( L"1D Min/Max optimization", ID_MIN_MAX_SHADOW_MAP_OPTIMIZATION, ID_MAIN_PANEL, &pCheckBox);
        pCheckBox->SetCheckboxState( m_PPAttribs.m_bUse1DMinMaxTree ? CPUT_CHECKBOX_CHECKED : CPUT_CHECKBOX_UNCHECKED );
    }

    {
        CPUTCheckbox *pCheckBox = NULL;
        pGUI->CreateCheckbox( L"Optimize sample locations", ID_OPTIMIZE_SAMPLE_LOCATIONS, ID_MAIN_PANEL, &pCheckBox);
        pCheckBox->SetCheckboxState( m_PPAttribs.m_bOptimizeSampleLocations ? CPUT_CHECKBOX_CHECKED : CPUT_CHECKBOX_UNCHECKED );
    }

    {
        CPUTCheckbox *pCheckBox = NULL;
        pGUI->CreateCheckbox( L"Correction at depth breaks", ID_CORRECT_SCATTERING_AT_DEPTH_BREAKS, ID_MAIN_PANEL, &pCheckBox);
        pCheckBox->SetCheckboxState( m_PPAttribs.m_bCorrectScatteringAtDepthBreaks ? CPUT_CHECKBOX_CHECKED : CPUT_CHECKBOX_UNCHECKED );
    }

    {
        CPUTCheckbox *pCheckBox = NULL;
        pGUI->CreateCheckbox( L"Show depth breaks", ID_SHOW_DEPTH_BREAKS, ID_MAIN_PANEL, &pCheckBox);
        pCheckBox->SetCheckboxState( m_PPAttribs.m_bShowDepthBreaks ? CPUT_CHECKBOX_CHECKED : CPUT_CHECKBOX_UNCHECKED );
    }

    {
        CPUTCheckbox *pCheckBox = NULL;
        pGUI->CreateCheckbox( L"Lighting only", ID_SHOW_LIGHTING_ONLY_CHECK, ID_MAIN_PANEL, &pCheckBox);
        pCheckBox->SetCheckboxState( m_PPAttribs.m_bShowLightingOnly ? CPUT_CHECKBOX_CHECKED : CPUT_CHECKBOX_UNCHECKED );
    }

    {
        CPUTDropdown *pDropDown = NULL;
        pGUI->CreateDropdown( L"Sngl sctr: none", ID_SINGLE_SCTR_MODE_DROPDOWN, ID_ADDITIONAL_ATTRIBS_PANEL, &pDropDown);
        pDropDown->AddSelectionItem( L"Sngl sctr: integration" );
        pDropDown->AddSelectionItem( L"Sngl sctr: LUT" );
        pDropDown->SetSelectedItem( m_PPAttribs.m_uiSingleScatteringMode+1 );
    }
    
    {
        CPUTDropdown *pDropDown = NULL;
        pGUI->CreateDropdown( L"Mult sctr: none", ID_MULTIPLE_SCTR_MODE_DROPDOWN, ID_ADDITIONAL_ATTRIBS_PANEL, &pDropDown);
        pDropDown->AddSelectionItem( L"Mult sctr: unoccluded" );
        pDropDown->AddSelectionItem( L"Mult sctr: occluded" );
        pDropDown->SetSelectedItem( m_PPAttribs.m_uiMultipleScatteringMode+1 );
    }
    
    {
        CPUTDropdown *pDropDown = NULL;
        pGUI->CreateDropdown( L"Num Cascades: 1", ID_NUM_CASCADES_DROPDOWN, ID_ADDITIONAL_ATTRIBS_PANEL, &pDropDown);
        for(int i=2; i <= MAX_CASCADES; ++i)
        {
            WCHAR Text[32];
            _stprintf_s(Text, _countof(Text), L"Num Cascades: %d", i);
            pDropDown->AddSelectionItem( Text );
        }
        pDropDown->SetSelectedItem( m_TerrainRenderParams.m_iNumShadowCascades );
    }

    {
        CPUTCheckbox *pCheckBox = NULL;
        pGUI->CreateCheckbox( L"Show cascades", ID_SHOW_CASCADES_CHECK, ID_ADDITIONAL_ATTRIBS_PANEL, &pCheckBox);
        pCheckBox->SetCheckboxState( CPUT_CHECKBOX_UNCHECKED );
    }

    {
        CPUTCheckbox *pCheckBox = NULL;
        pGUI->CreateCheckbox( L"Smooth shadows", ID_SMOOTH_SHADOWS_CHECK, ID_ADDITIONAL_ATTRIBS_PANEL, &pCheckBox);
        pCheckBox->SetCheckboxState( m_TerrainRenderParams.m_bSmoothShadows ? CPUT_CHECKBOX_CHECKED : CPUT_CHECKBOX_UNCHECKED );
    }

    {
        CPUTCheckbox *pCheckBox = NULL;
        pGUI->CreateCheckbox( L"Best cascade search", ID_BEST_CASCADE_SEARCH_CHECK, ID_ADDITIONAL_ATTRIBS_PANEL, &pCheckBox);
        pCheckBox->SetCheckboxState( m_TerrainRenderParams.m_bBestCascadeSearch ? CPUT_CHECKBOX_CHECKED : CPUT_CHECKBOX_UNCHECKED );
    }

    {
        CPUTSlider *pSlider = NULL;
        std::wstringstream SS;
        SS << "Partitioning Factor: " << m_fCascadePartitioningFactor;
        pGUI->CreateSlider( SS.str().c_str(), ID_CASCADE_PARTITIONING_SLIDER, ID_ADDITIONAL_ATTRIBS_PANEL, &pSlider);
        pSlider->SetScale( 0.0f, 1.f, 101 );
        pSlider->SetValue( m_fCascadePartitioningFactor );
    }

    {
        CPUTDropdown *pDropDown = NULL;
        pGUI->CreateDropdown( L"Cascades processing: single pass", ID_CASCADE_PROCESSING_MODE_DROPDOWN, ID_ADDITIONAL_ATTRIBS_PANEL, &pDropDown);
        pDropDown->AddSelectionItem( L"Cascades processing: multi pass" );
        pDropDown->AddSelectionItem( L"Cascades processing: multi pass inst" );
        pDropDown->SetSelectedItem( m_PPAttribs.m_uiCascadeProcessingMode+1 );
    }

    {
        CPUTDropdown *pDropDown = NULL;
        pGUI->CreateDropdown( L"First cascade to ray march: 0", ID_FIRST_CASCADE_TO_RAY_MARCH_DROPDOWN, ID_ADDITIONAL_ATTRIBS_PANEL, &pDropDown);
        for(int i=1; i<MAX_CASCADES; ++i)
        {
            WCHAR Text[32];
            _stprintf_s(Text, _countof(Text), L"First cascade to ray march: %d", i);
            pDropDown->AddSelectionItem( Text );
        }
        pDropDown->SetSelectedItem( m_PPAttribs.m_iFirstCascade+1 );
    }

    {
        CPUTDropdown *pDropDown = NULL;
        pGUI->CreateDropdown( L"Extinction eval mode: per pixel", ID_EXTINCTION_EVAL_MODE_DROPDOWN, ID_ADDITIONAL_ATTRIBS_PANEL, &pDropDown);
        pDropDown->AddSelectionItem( L"Extinction eval mode: Epipolar" );
        pDropDown->SetSelectedItem( m_PPAttribs.m_uiExtinctionEvalMode+1 );
    }

    {
        CPUTDropdown *pDropDown = NULL;
        pGUI->CreateDropdown( L"Refinement criterion: depth", ID_REFINEMENT_CRITERION_DROPDOWN, ID_ADDITIONAL_ATTRIBS_PANEL, &pDropDown);
        pDropDown->AddSelectionItem( L"Refinement criterion: inscattering" );
        pDropDown->SetSelectedItem( m_PPAttribs.m_uiRefinementCriterion+1 );
    }

    {
        CPUTDropdown *pDropDown = NULL;
        pGUI->CreateDropdown( L"Min/max format: 16u", ID_MIN_MAX_MIP_FORMAT_DROPDOWN, ID_ADDITIONAL_ATTRIBS_PANEL, &pDropDown);
        pDropDown->AddSelectionItem( L"Min/max format: 32f" );
        pDropDown->SetSelectedItem( m_PPAttribs.m_bIs32BitMinMaxMipMap ? 2 : 1 );
    }
    
    {
        CPUTCheckbox *pCheckBox = NULL;
        pGUI->CreateCheckbox( L"Use custom sctr coeffs", ID_USE_CUSTOM_SCTR_COEFFS_CHECK, ID_ADDITIONAL_ATTRIBS_PANEL, &pCheckBox);
        pCheckBox->SetCheckboxState( m_PPAttribs.m_bUseCustomSctrCoeffs ? CPUT_CHECKBOX_CHECKED : CPUT_CHECKBOX_UNCHECKED );
    }

    {
        CPUTButton *pBtn = NULL;
        pGUI->CreateButton( L"Set Rayleigh color", ID_RLGH_COLOR_BTN, ID_ADDITIONAL_ATTRIBS_PANEL, &pBtn);
        pBtn->SetEnable(!CPUTGetFullscreenState() && m_PPAttribs.m_bUseCustomSctrCoeffs);
    }

    {
        CPUTButton *pBtn = NULL;
        pGUI->CreateButton( L"Set Mie color", ID_MIE_COLOR_BTN, ID_ADDITIONAL_ATTRIBS_PANEL, &pBtn);
        pBtn->SetEnable(!CPUTGetFullscreenState() && m_PPAttribs.m_bUseCustomSctrCoeffs);
    }

    {
        CPUTSlider* pSlider = NULL;
        pGUI->CreateSlider( L"Aerosol density", ID_AEROSOL_DENSITY_SCALE_SLIDER, ID_ADDITIONAL_ATTRIBS_PANEL, &pSlider);
        pSlider->SetScale( 0.1f, 5.f, 50 );
        pSlider->SetValue( m_PPAttribs.m_fAerosolDensityScale );

    }

    {
        CPUTSlider* pSlider = NULL;
        pGUI->CreateSlider( L"Aerosol absorbtion", ID_AEROSOL_ABSORBTION_SCALE_SLIDER, ID_ADDITIONAL_ATTRIBS_PANEL, &pSlider);
        pSlider->SetScale( 0.0f, 5.f, 50 );
        pSlider->SetValue( m_PPAttribs.m_fAerosolAbsorbtionScale );
    }

    {
        CPUTCheckbox *pCheckBox = NULL;
        pGUI->CreateCheckbox( L"Animate sun", ID_ANIMATE_SUN, ID_ADDITIONAL_ATTRIBS_PANEL, &pCheckBox);
        pCheckBox->SetCheckboxState( m_bAnimateSun ? CPUT_CHECKBOX_CHECKED : CPUT_CHECKBOX_UNCHECKED );
    }

    {
        CPUTSlider* pSlider = NULL;
        pGUI->CreateSlider( L"Middle gray (Key)", ID_MIDDLE_GRAY, ID_TONE_MAPPING_ATTRIBS_PANEL, &pSlider);
        pSlider->SetScale( 0.01f, 1.f, 50 );
        pSlider->SetValue( m_PPAttribs.m_fMiddleGray );
    }

    {
        CPUTSlider* pSlider = NULL;
        pGUI->CreateSlider( L"White point", ID_WHITE_POINT, ID_TONE_MAPPING_ATTRIBS_PANEL, &pSlider);
        pSlider->SetScale( 0.01f, 10.f, 50 );
        pSlider->SetValue( m_PPAttribs.m_fWhitePoint );
        pSlider->SetEnable( m_PPAttribs.m_uiToneMappingMode == TONE_MAPPING_MODE_REINHARD_MOD ||
                            m_PPAttribs.m_uiToneMappingMode == TONE_MAPPING_MODE_UNCHARTED2 ||
                            m_PPAttribs.m_uiToneMappingMode == TONE_MAPPING_LOGARITHMIC ||
                            m_PPAttribs.m_uiToneMappingMode == TONE_MAPPING_ADAPTIVE_LOG );
    }

    {
        CPUTSlider* pSlider = NULL;
        pGUI->CreateSlider( L"Luminance saturation", ID_LUM_SATURATION, ID_TONE_MAPPING_ATTRIBS_PANEL, &pSlider);
        pSlider->SetScale( 0.01f, 2.f, 50 );
        pSlider->SetValue( m_PPAttribs.m_fLuminanceSaturation );
        pSlider->SetEnable( m_PPAttribs.m_uiToneMappingMode == TONE_MAPPING_MODE_EXP ||
                            m_PPAttribs.m_uiToneMappingMode == TONE_MAPPING_MODE_REINHARD ||
                            m_PPAttribs.m_uiToneMappingMode == TONE_MAPPING_MODE_REINHARD_MOD ||
                            m_PPAttribs.m_uiToneMappingMode == TONE_MAPPING_LOGARITHMIC ||
                            m_PPAttribs.m_uiToneMappingMode == TONE_MAPPING_ADAPTIVE_LOG );
    }

    {
        CPUTCheckbox *pCheckBox = NULL;
        pGUI->CreateCheckbox( L"Auto exposure", ID_AUTO_EXPOSURE, ID_TONE_MAPPING_ATTRIBS_PANEL, &pCheckBox);
        pCheckBox->SetCheckboxState( m_PPAttribs.m_bAutoExposure ? CPUT_CHECKBOX_CHECKED : CPUT_CHECKBOX_UNCHECKED );
    }

    {
        CPUTDropdown *pDropDown = NULL;
        pGUI->CreateDropdown( L"Tone mapping: exp", ID_TONE_MAPPING_MODE, ID_TONE_MAPPING_ATTRIBS_PANEL, &pDropDown);
        pDropDown->AddSelectionItem( L"Tone mapping: Reinhard" );
        pDropDown->AddSelectionItem( L"Tone mapping: Reinhard Mod" );
        pDropDown->AddSelectionItem( L"Tone mapping: Uncharted 2" );
        pDropDown->AddSelectionItem( L"Tone mapping: Filmic ALU" );
        pDropDown->AddSelectionItem( L"Tone mapping: Logarithmic" );
        pDropDown->AddSelectionItem( L"Tone mapping: Adaptive log" );
        pDropDown->SetSelectedItem( m_PPAttribs.m_uiToneMappingMode + 1 );
    }

    {
        CPUTCheckbox *pCheckBox = NULL;
        pGUI->CreateCheckbox( L"Light adaptation", ID_LIGHT_ADAPTATION, ID_TONE_MAPPING_ATTRIBS_PANEL, &pCheckBox);
        pCheckBox->SetCheckboxState( m_PPAttribs.m_bLightAdaptation ? CPUT_CHECKBOX_CHECKED : CPUT_CHECKBOX_UNCHECKED );
        pCheckBox->SetEnable(m_PPAttribs.m_bAutoExposure ? true : false );
    }

    pGUI->CreateText( _L("F1 for Help"), ID_IGNORE_CONTROL_ID, ID_HELP_TEXT_PANEL);
    pGUI->CreateText( _L("[Escape] to quit application"), ID_IGNORE_CONTROL_ID, ID_HELP_TEXT_PANEL);
    pGUI->CreateText( _L("A,S,D,F - move camera position"), ID_IGNORE_CONTROL_ID, ID_HELP_TEXT_PANEL);
    pGUI->CreateText( _L("Q - camera position down"), ID_IGNORE_CONTROL_ID, ID_HELP_TEXT_PANEL);
    pGUI->CreateText( _L("E - camera position up"), ID_IGNORE_CONTROL_ID, ID_HELP_TEXT_PANEL);
    pGUI->CreateText( _L("[Shift] - accelerate camera movement"), ID_IGNORE_CONTROL_ID, ID_HELP_TEXT_PANEL);
    pGUI->CreateText( _L("mouse + left click - camera look rotation"), ID_IGNORE_CONTROL_ID, ID_HELP_TEXT_PANEL);
    pGUI->CreateText( _L("mouse + right click - light rotation"), ID_IGNORE_CONTROL_ID, ID_HELP_TEXT_PANEL);

    //
    // Make the main panel active
    //
    pGUI->SetActivePanel(CONTROL_PANEL_IDS[m_uiSelectedPanelInd]);
}

void COutdoorLightScatteringSample::CreateDefaultRenderStatesBlock()
{
    CPUTRenderStateBlockDX11 *pBlock = new CPUTRenderStateBlockDX11();
    CPUTRenderStateDX11 *pStates = pBlock->GetState();

    // Override default sampler desc for our default shadowing sampler
    pStates->SamplerDesc[1].Filter         = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    pStates->SamplerDesc[1].AddressU       = D3D11_TEXTURE_ADDRESS_BORDER;
    pStates->SamplerDesc[1].AddressV       = D3D11_TEXTURE_ADDRESS_BORDER;
    pStates->SamplerDesc[1].ComparisonFunc = D3D11_COMPARISON_GREATER;
    pBlock->CreateNativeResources();
    CPUTAssetLibrary::GetAssetLibrary()->AddRenderStateBlock( _L("$DefaultRenderStates"), pBlock );
    
    pBlock->Release(); // We're done with it.  The library owns it now.
}

void COutdoorLightScatteringSample::CreateViewCamera(int screenWidth, int screenHeight)
{
    float4x4 InitialWorldMatrix
    (
		 -0.88250709f,  0.00000000f,  0.47029909f, 0.0000000f,
		 -0.10293237f,  0.97575504f, -0.19315059f, 0.0000000f,
		 -0.45889670f, -0.21886577f, -0.86111075f, 0.0000000f, 
		          0.f,   8023.6152f,          0.f, 1.0000000f 
    );

    mpCamera = new CPUTCamera();
    // Set the projection matrix for all of the cameras to match our window.
    mpCamera->SetAspectRatio(((float)screenWidth)/((float)screenHeight)); 
    mpCamera->SetFov( XMConvertToRadians(45.0f) );
    mpCamera->SetFarPlaneDistance(1e+7);
    mpCamera->SetNearPlaneDistance(50.0f);
    mpCamera->SetParentMatrix(InitialWorldMatrix);
    mpCamera->Update();

    CPUTAssetLibraryDX11::GetAssetLibrary()->AddCamera( _L("Outdoor light scattering sample camera"), mpCamera );

    mpCameraController = new CPUTCameraControllerFPS();
    mpCameraController->SetCamera(mpCamera);
    mpCameraController->SetLookSpeed(0.004f);
    mpCameraController->SetMoveSpeed(200.0f);
}

void COutdoorLightScatteringSample::CreateLightCamera()
{
    m_pDirectionalLightCamera = new CParallelLightCamera();
    
    m_pDirLightOrienationCamera = new CParallelLightCamera();
    float4x4 LightOrientationWorld
    (
        -0.92137718f, -0.36748588f,  -0.12656364f, 0.0000000f,
		-0.37707147f,  0.92411846f,  0.061823435f, 0.0000000f,
		0.094240554f,  0.10468624f,  -0.99003011f, 0.0000000f,
		         0.f,          0.f,           0.f, 1.0000000f
    );

    m_pDirLightOrienationCamera->SetParentMatrix( LightOrientationWorld );
    m_pDirLightOrienationCamera->Update();
    
    m_pLightController = new CPUTCameraControllerArcBall();
    m_pLightController->SetCamera( m_pDirLightOrienationCamera );
    m_pLightController->SetLookSpeed(0.002f);
}

bool COutdoorLightScatteringSample::CreateElevationDatSource()
{
    try
    {
        m_pElevDataSource.reset( new CElevationDataSource(m_strRawDEMDataFile.c_str()) );
        m_pElevDataSource->SetOffsets(m_TerrainRenderParams.m_iColOffset, m_TerrainRenderParams.m_iRowOffset);
        m_fMinElevation = m_pElevDataSource->GetGlobalMinElevation() * m_TerrainRenderParams.m_TerrainAttribs.m_fElevationScale;
        m_fMaxElevation = m_pElevDataSource->GetGlobalMaxElevation() * m_TerrainRenderParams.m_TerrainAttribs.m_fElevationScale;
    }
    catch(const std::exception &)
    {
        LOG_ERROR(_T("Failed to create elevation data source"));
        return false;
    }

    return true;
}

void COutdoorLightScatteringSample::CreateEarthHemisphere()
{
    LPCTSTR strTileTexPaths[CEarthHemsiphere::NUM_TILE_TEXTURES], strNormalMapPaths[CEarthHemsiphere::NUM_TILE_TEXTURES];
	for(int iTile=0; iTile < _countof(strTileTexPaths); ++iTile )
    {
		strTileTexPaths[iTile] = m_strTileTexPaths[iTile].c_str();
        strNormalMapPaths[iTile] = m_strNormalMapTexPaths[iTile].c_str();
    }

    HRESULT hr;
    V( m_EarthHemisphere.OnD3D11CreateDevice(m_pElevDataSource.get(), m_TerrainRenderParams, mpD3dDevice, mpContext, m_strRawDEMDataFile.c_str(), m_strMtrlMaskFile.c_str(), strTileTexPaths, strNormalMapPaths ) );
}

void COutdoorLightScatteringSample::CreateLightAttribsConstantBuffer()
{
    D3D11_BUFFER_DESC CBDesc = 
    {
        sizeof(SLightAttribs),
        D3D11_USAGE_DYNAMIC,
        D3D11_BIND_CONSTANT_BUFFER,
        D3D11_CPU_ACCESS_WRITE, //UINT CPUAccessFlags
        0, //UINT MiscFlags;
        0, //UINT StructureByteStride;
    };

    HRESULT hr;
    V( mpD3dDevice->CreateBuffer( &CBDesc, NULL, &m_pcbLightAttribs) );
}

// Handle OnCreation events
//-----------------------------------------------------------------------------
void COutdoorLightScatteringSample::Create()
{    
    HRESULT hr;
	LPCWSTR ConfigPath = L"Default_Config.txt";
    if( FAILED(ParseConfigurationFile( ConfigPath )) )
    {
        LOG_ERROR(_T("Failed to load config file %s"), ConfigPath );
        return;
    }

    InitializeGUI();

    CreateTmpBackBuffAndDepthBuff(mpD3dDevice);

    // Create shadow map before other assets!!!
    if(FAILED(CreateShadowMap(mpD3dDevice)))
        return;

    CPUTAssetLibrary::GetAssetLibrary()->SetMediaDirectoryName(  _L("Media\\"));

    // Add our programatic (and global) material parameters
    CPUTMaterial::mGlobalProperties.AddValue( _L("cbPerFrameValues"), _L("$cbPerFrameValues") );
    CPUTMaterial::mGlobalProperties.AddValue( _L("cbPerModelValues"), _L("#cbPerModelValues") );

    CreateDefaultRenderStatesBlock();

    int width, height;
    CPUTOSServices::GetOSServices()->GetClientDimensions(&width, &height);

    CreateViewCamera(width, height);

    CreateLightCamera();

    // Call ResizeWindow() because it creates some resources that our blur material needs (e.g., the back buffer)
    ResizeWindow(width, height);

    if(FAILED(m_pLightSctrPP->OnCreateDevice(mpD3dDevice, mpContext)))
        return;
    
    if(CreateElevationDatSource() == false)
       return;

    CreateEarthHemisphere();

    CreateLightAttribsConstantBuffer();
}


void GetRaySphereIntersection(D3DXVECTOR3 f3RayOrigin,
                              const D3DXVECTOR3 &f3RayDirection,
                              const D3DXVECTOR3 &f3SphereCenter,
                              float fSphereRadius,
                              D3DXVECTOR2 &f2Intersections)
{
    // https://web.archive.org/web/20130523060257/http://wiki.cgsociety.org/index.php/Ray_Sphere_Intersection
    f3RayOrigin -= f3SphereCenter;
    float A = D3DXVec3Dot(&f3RayDirection, &f3RayDirection);
    float B = 2 * D3DXVec3Dot(&f3RayOrigin, &f3RayDirection);
    float C = D3DXVec3Dot(&f3RayOrigin, &f3RayOrigin) - fSphereRadius*fSphereRadius;
    float D = B*B - 4*A*C;
    // If discriminant is negative, there are no real roots hence the ray misses the
    // sphere
    if( D<0 )
    {
        f2Intersections = D3DXVECTOR2(-1,-1);
    }
    else
    {
        D = sqrt(D);
        f2Intersections = D3DXVECTOR2(-B - D, -B + D) / (2*A); // A must be positive here!!
    }
}

static D3DXMATRIX CreateLightBasis(const D3DXVECTOR3 &LightDir)
{
    // Declare working vectors
    D3DXVECTOR3 vLightSpaceX, vLightSpaceY, vLightSpaceZ;

    // Compute an inverse vector for the direction on the sun
    vLightSpaceZ = LightDir;
    // And a vector for X light space
    vLightSpaceX = D3DXVECTOR3( 1.0f, 0.0, 0.0 );
    // Compute the cross products
    D3DXVec3Cross(&vLightSpaceY, &vLightSpaceX, &vLightSpaceZ);
    D3DXVec3Cross(&vLightSpaceX, &vLightSpaceZ, &vLightSpaceY);
    // And then normalize them
    D3DXVec3Normalize( &vLightSpaceX, &vLightSpaceX );
    D3DXVec3Normalize( &vLightSpaceY, &vLightSpaceY );
    D3DXVec3Normalize( &vLightSpaceZ, &vLightSpaceZ );

    // Declare a world to light space transformation matrix
    // Initialize to an identity matrix
    D3DXMATRIX WorldToLightViewSpaceMatr;
    D3DXMatrixIdentity( &WorldToLightViewSpaceMatr );
    // Adjust elements to the light space
    WorldToLightViewSpaceMatr._11 = vLightSpaceX.x;
    WorldToLightViewSpaceMatr._21 = vLightSpaceX.y;
    WorldToLightViewSpaceMatr._31 = vLightSpaceX.z;

    WorldToLightViewSpaceMatr._12 = vLightSpaceY.x;
    WorldToLightViewSpaceMatr._22 = vLightSpaceY.y;
    WorldToLightViewSpaceMatr._32 = vLightSpaceY.z;

    WorldToLightViewSpaceMatr._13 = vLightSpaceZ.x;
    WorldToLightViewSpaceMatr._23 = vLightSpaceZ.y;
    WorldToLightViewSpaceMatr._33 = vLightSpaceZ.z;

    return WorldToLightViewSpaceMatr;
}

static D3DXVECTOR2 GetViewCameraNearAndFarPlanes(D3DXMATRIX &ProjMatrix)
{
    /*
    from https://docs.microsoft.com/en-us/windows/win32/direct3d9/projection-transform

    proj43 = -proj33*Zn
    -proj43/proj33 = Zn

    proj33 = Zf/(Zf - Zn)
    proj33*(Zf - Zn) = Zf
    proj33*Zf - proj33*Zn = Zf
    -proj33*Zn = Zf - proj33*Zf
    proj33*Zn = proj33*Zf - Zf
    proj33*Zn = Zf*(proj33 - 1)
    (proj33*Zn)/(proj33 - 1) = Zf
    */
    D3DXVECTOR2 nearFarPlane;
    nearFarPlane.x = -ProjMatrix._43 / ProjMatrix._33;
    nearFarPlane.y = ProjMatrix._33 / (ProjMatrix._33 - 1.0f) * nearFarPlane.x;

    return nearFarPlane;
}

static void ConfigureCascadeRange(float ShadowMapDim, D3DXVECTOR3 &MinXYZ, D3DXVECTOR3 &MaxXYZ)
{
    //add one extra pixel of shadow map dimension in ortho projection space
    float fCascadeXExt = (MaxXYZ.x - MinXYZ.x) * (1.0f + 1.0f/ShadowMapDim);
    float fCascadeYExt = (MaxXYZ.y - MinXYZ.y) * (1.0f + 1.0f/ShadowMapDim);

    //round to nearest power of two
    fCascadeXExt = pow(2.0f, ceil( log(fCascadeXExt)/log(2.0f) ) );
    fCascadeYExt = pow(2.0f, ceil( log(fCascadeYExt)/log(2.0f) ) );

    // Align cascade center with the shadow map texels to alleviate temporal aliasing
    float fCascadeXCenter = (MaxXYZ.x + MinXYZ.x)/2.f;
    float fCascadeYCenter = (MaxXYZ.y + MinXYZ.y)/2.f;
    float fTexelXSize = fCascadeXExt / ShadowMapDim;
    float fTexelYSize = fCascadeXExt / ShadowMapDim;
    fCascadeXCenter = floor(fCascadeXCenter/fTexelXSize) * fTexelXSize;
    fCascadeYCenter = floor(fCascadeYCenter/fTexelYSize) * fTexelYSize;

    // Compute new cascade min/max xy coords
    MaxXYZ.x = fCascadeXCenter + fCascadeXExt/2.0f;
    MaxXYZ.y = fCascadeYCenter + fCascadeYExt/2.0f;
    MinXYZ.x = fCascadeXCenter - fCascadeXExt/2.0f;
    MinXYZ.y = fCascadeYCenter - fCascadeYExt/2.0f;
}

static D3DXMATRIX GetCascadeProj(const D3DXVECTOR3 &MinXYZ, const D3DXVECTOR3 &MaxXYZ)
{
    /*
    We use orthographic projection
    https://docs.microsoft.com/en-us/windows/win32/direct3d10/d3d10-d3dxmatrixorthooffcenterlh
    with inverted z values as listed below

    d(z) = z / (zf - zn) + zn / (zn - zf)
            = z / (zf - zn) + zn / -(zf - zn)
            = (z - zn) / (zf - zn)

    invD(z) = 1 - d(z)
            = 1 - (z - zn) / (zf - zn)
            = ((zf - zn) - (z - zn)) / (zf - zn)
            = (zf - zn - z + zn)) / (zf - zn)
            = (zf - z) / (zf - zn)
            = -z / (zf - zn) + zf / (zf - zn)

    x evaluation
    2 / (mxX - mnX) - mnX*2 / (mxX - mnX) - 1
    (2 - 2*mnX) / (mxX - mnX) - 1
    (2 - 2*mnX - (mxX - mnX)) / (mxX - mnX)
    (2 - 2*mnX - mxX + mnX) / (mxX - mnX)
    (2 - mnX - mxX ) / (mxX - mnX)
    (2 - (mnX + mxX)) / (mxX - mnX)
    2 / (mxX - mnX) - (mnX + mxX) / (mxX - mnX)
    2 / (mxX - mnX) + (mnX + mxX) / (mnX - mxX)

    y evaluation
    2 / (mxY - mnX) - mnY*2 / (mxY - mnY) - 1
    (2 - 2*mnY) / (mxY - mnY) - 1
    (2 - 2*mnY - (mxY - mnY)) / (mxY - mnY)
    (2 - 2*mnY - mxY + mnY) / (mxY - mnY)
    (2 - mnY - mxY) / (mxY - mnY)
    (2 - (mnY + mxY)) / (mxY - mnY)
    2 / (mxY - mnY) - (mnY + mxY) / (mxY - mnY)
    2 / (mxY - mnY) + (mnY + mxY) / (mnY - mxY)
    */

    float scaleX =  2.f / (MaxXYZ.x - MinXYZ.x);
    float scaleY =  2.f / (MaxXYZ.y - MinXYZ.y);
    float scaleZ = -1.f / (MaxXYZ.z - MinXYZ.z);

    // Apply bias to shift the extent to [-1,1]x[-1,1]x[1,0]
    float transX = -MinXYZ.x * scaleX - 1.f;
    float transY = -MinXYZ.y * scaleY - 1.f;
    float transZ = -MaxXYZ.z * scaleZ + 0.f;

    D3DXMATRIX ScaleMatrix;
    D3DXMatrixScaling(&ScaleMatrix, scaleX, scaleY, scaleZ);

    D3DXMATRIX ScaledBiasMatrix;
    D3DXMatrixTranslation(&ScaledBiasMatrix, transX, transY, transZ);

    // Note: bias is applied after scaling!
    return ScaleMatrix * ScaledBiasMatrix;
}

static D3DXMATRIX GetProjToUV()
{
    D3DXMATRIX ProjToUVScale, ProjToUVBias;
    D3DXMatrixScaling( &ProjToUVScale, 0.5f, -0.5f, 1.f);
    D3DXMatrixTranslation( &ProjToUVBias, 0.5f, 0.5f, 0.f);

    return ProjToUVScale * ProjToUVBias;
}

D3DXMATRIX COutdoorLightScatteringSample::CalculateCascadeProjToLight(const D3DXVECTOR2 &CascadeZNearFar, const D3DXMATRIX &WorldToLightSpace)
{
    D3DXMATRIX CascadeFrustumProjMatrix = m_CameraProjMatrix;
    CascadeFrustumProjMatrix._33 = CascadeZNearFar.y / (CascadeZNearFar.y - CascadeZNearFar.x);
    CascadeFrustumProjMatrix._43 = -CascadeZNearFar.x * CascadeFrustumProjMatrix._33;

    D3DXMATRIX CascadeFrustumViewProjMatr = m_CameraViewMatrix * CascadeFrustumProjMatrix;

    D3DXMATRIX CascadeFrustumProjSpaceToWorldSpace;
    D3DXMatrixInverse(&CascadeFrustumProjSpaceToWorldSpace, nullptr, &CascadeFrustumViewProjMatr);

    return CascadeFrustumProjSpaceToWorldSpace * WorldToLightSpace;
}

D3DXVECTOR2 COutdoorLightScatteringSample::GetCascadeZRange(const SShadowMapAttribs &ShadowMapAttribs, int Cascade, const D3DXVECTOR2 &ViewCamNearFarPlane)
{
    float fCascadeNearZ = ViewCamNearFarPlane.x;
    float fCascadeFarZ = (Cascade == 0) ? ViewCamNearFarPlane.y : ShadowMapAttribs.fCascadeCamSpaceZEnd[Cascade-1];

    if (Cascade < m_TerrainRenderParams.m_iNumShadowCascades-1) 
    {
        float ratio = ViewCamNearFarPlane.x / ViewCamNearFarPlane.y;
        float power = (float)(Cascade+1) / (float)m_TerrainRenderParams.m_iNumShadowCascades;
        float logZ = ViewCamNearFarPlane.y * pow(ratio, power);
        
        float range = ViewCamNearFarPlane.x - ViewCamNearFarPlane.y;
        float uniformZ = ViewCamNearFarPlane.y + range * power;

        fCascadeNearZ = m_fCascadePartitioningFactor * (logZ - uniformZ) + uniformZ;
    }

    return D3DXVECTOR2(fCascadeNearZ, fCascadeFarZ);
}

void COutdoorLightScatteringSample::CalculateCascadeRange(int Cascade,
                                                          const D3DXMATRIX &CascadeFrustumProjSpaceToLightSpace,
                                                          D3DXVECTOR3 &MinXYZ,
                                                          D3DXVECTOR3 &MaxXYZ)
{
    // First cascade used for ray marching must contain camera within it
    if(Cascade != m_PPAttribs.m_iFirstCascade )
    {
        MinXYZ = D3DXVECTOR3(+FLT_MAX, +FLT_MAX, +FLT_MAX);
        MaxXYZ = D3DXVECTOR3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    }

    for(int iClipPlaneCorner=0; iClipPlaneCorner < 8; ++iClipPlaneCorner)
    {
        D3DXVECTOR3 f3PlaneCornerProjSpace( (iClipPlaneCorner & 0x01) ? +1.f : - 1.f, 
                                            (iClipPlaneCorner & 0x02) ? +1.f : - 1.f,
                                            // Since we use complimentary depth buffering, 
                                            // far plane has depth 0
                                            (iClipPlaneCorner & 0x04) ? 1.f : 0.f);
        D3DXVECTOR3 f3PlaneCornerLightSpace;
        D3DXVec3TransformCoord(&f3PlaneCornerLightSpace, &f3PlaneCornerProjSpace, &CascadeFrustumProjSpaceToLightSpace);
        D3DXVec3Minimize(&MinXYZ, &MinXYZ, &f3PlaneCornerLightSpace);
        D3DXVec3Maximize(&MaxXYZ, &MaxXYZ, &f3PlaneCornerLightSpace);
    }

    // It is necessary to ensure that shadow-casting patches, which are not visible 
    // in the frustum, are still rendered into the shadow map
    MinXYZ.z -= SAirScatteringAttribs().fEarthRadius * sqrt(2.f);
}

void COutdoorLightScatteringSample::FillCascadeAttributes(SCascadeAttribs &CascadeAttribs, int CacadeInd,  const D3DXMATRIX &CascadeProj, const D3DXVECTOR2 &CascadeZNearFar)
{
    float fMaxLightShaftsDist = 3e+5f;

    CascadeAttribs.f4StartEndZ.x = (CacadeInd == m_PPAttribs.m_iFirstCascade) ? 0 : min(CascadeZNearFar.y, fMaxLightShaftsDist);
    CascadeAttribs.f4StartEndZ.y = min(CascadeZNearFar.x, fMaxLightShaftsDist);
    CascadeAttribs.f4LightSpaceScale.x = CascadeProj(0, 0);
    CascadeAttribs.f4LightSpaceScale.y = CascadeProj(1, 1);
    CascadeAttribs.f4LightSpaceScale.z = CascadeProj(2, 2);
    CascadeAttribs.f4LightSpaceScaledBias.x = CascadeProj(3, 0);
    CascadeAttribs.f4LightSpaceScaledBias.y = CascadeProj(3, 1);
    CascadeAttribs.f4LightSpaceScaledBias.z = CascadeProj(3, 2);
}

void COutdoorLightScatteringSample::RenderShadowMap(SShadowMapAttribs &ShadowMapAttribs)
{
    D3DXMATRIX projToUV = GetProjToUV();

    D3DXVECTOR3 v3DirOnLight = (D3DXVECTOR3&)m_pDirLightOrienationCamera->GetLook();

    D3DXVECTOR3 v3LightDirection = -v3DirOnLight;

    gLightDir.x = v3LightDirection.x;
    gLightDir.y = v3LightDirection.y;
    gLightDir.z = v3LightDirection.z;

    D3DXMATRIX WorldToLightSpace = CreateLightBasis(v3LightDirection);

    D3DXMatrixTranspose(&ShadowMapAttribs.mWorldToLightViewT, &WorldToLightSpace);

    D3DXVECTOR3 f3CameraPosInLightSpace;
    D3DXVec3TransformCoord(&f3CameraPosInLightSpace, &m_CameraPos, &WorldToLightSpace);

    D3DXVECTOR2 mainCamNearFarPlanes = GetViewCameraNearAndFarPlanes(m_CameraProjMatrix);

    for(int i=0; i < MAX_CASCADES; ++i)
        ShadowMapAttribs.fCascadeCamSpaceZEnd[i] = +FLT_MAX;

    CComPtr<ID3D11RenderTargetView> pOrigRTV;
    CComPtr<ID3D11DepthStencilView> pOrigDSV;
    D3D11_VIEWPORT OrigViewport;
    mpContext->OMGetRenderTargets(1, &pOrigRTV, &pOrigDSV);
    UINT uiNumVP = 1;
    mpContext->RSGetViewports(&uiNumVP, &OrigViewport);

    D3D11_VIEWPORT NewViewPort = {0.0f, 0.0f, static_cast<float>(m_uiShadowMapResolution), static_cast<float>(m_uiShadowMapResolution), 0.0f, 1.0f};
    mpContext->RSSetViewports(1, &NewViewPort);

    for(int iCascade = 0; iCascade < m_TerrainRenderParams.m_iNumShadowCascades; ++iCascade)
    {
        D3DXVECTOR2 cascadeZNearFar = GetCascadeZRange(ShadowMapAttribs, iCascade, mainCamNearFarPlanes);

        D3DXMATRIX CascadeFrustumProjSpaceToLightSpace = CalculateCascadeProjToLight(cascadeZNearFar, WorldToLightSpace);

        D3DXVECTOR3 f3MinXYZ(f3CameraPosInLightSpace), f3MaxXYZ(f3CameraPosInLightSpace);
        CalculateCascadeRange(iCascade, CascadeFrustumProjSpaceToLightSpace, f3MinXYZ, f3MaxXYZ);
        ConfigureCascadeRange((float)m_uiShadowMapResolution, f3MinXYZ, f3MaxXYZ);

        D3DXMATRIX cascadeProj = GetCascadeProj(f3MinXYZ, f3MaxXYZ);

        FillCascadeAttributes(ShadowMapAttribs.Cascades[iCascade], iCascade, cascadeProj, cascadeZNearFar);

        D3DXMATRIX WorldToLightProjSpaceMatr = WorldToLightSpace * cascadeProj;
        
        D3DXMATRIX WorldToShadowMapUVDepthMatr = WorldToLightProjSpaceMatr * projToUV;
        D3DXMatrixTranspose( &ShadowMapAttribs.mWorldToShadowMapUVDepthT[iCascade], &WorldToShadowMapUVDepthMatr );

        mpContext->OMSetRenderTargets(0, nullptr, m_pShadowMapDSVs[iCascade]);
        mpContext->ClearDepthStencilView(m_pShadowMapDSVs[iCascade], D3D11_CLEAR_DEPTH, 0.f, 0);

        m_EarthHemisphere.Render(mpContext, m_CameraPos, WorldToLightProjSpaceMatr, nullptr, nullptr, nullptr, nullptr, nullptr, true);

        ShadowMapAttribs.fCascadeCamSpaceZEnd[iCascade] = cascadeZNearFar.x;
    }

    mpContext->OMSetRenderTargets(1, &pOrigRTV.p, pOrigDSV);
    mpContext->RSSetViewports(1, &OrigViewport);
}

static float GetMaxViewDisstance(float DisstToCamSqr, float EarthRadius, float MaxElevation)
{
    float earthRadiusSqr = EarthRadius * EarthRadius;
    
    //find the first value by using disstance to camera as hypotenuse
    //and earth radius as cathetus and solving Pythagorean theorem
    float val1 = sqrtf(DisstToCamSqr - earthRadiusSqr);
    
    //same as before but now we use max elevation as hypotenuse
    float val2 = sqrtf(MaxElevation * MaxElevation - earthRadiusSqr);

    return val1 + val2;
}

static float AdjustNearPlane(float CameraElevation, float MaxRadius,  const D3DXMATRIX &ProjMatrix)
{
    /*
    as far as i understood, we correct near plane by taking fraction of length
    of one of corners of near plane with z equals to 1

    Perspective projection is 
    | 1 / r * tan(a / 2)      0                0           0 |
    |    0               1 / tan(a / 2)        0           0 |
    |    0                    0            f / (f - n)     1 |
    |    0                    0         -((f*n) / (f - n)) 0 |

    and its inverse 
    |r * tan(a / 2)      0     0        0        |
    |    0          tan(a / 2) 0        0        |
    |    0               0     0 -((f - n) / f*n)|
    |    0               0     1       1 / n     |

    so if we take point (1, 1, 0) in NDC space and multiply bi inverse with 
    following homodeneous divede, we get

    (r * tan(a / 2) * n, tan(a / 2) * n, 1)

    becaus as i said erlier depth of near plane is equal to one, n is also
    equal to one and it wil take us to

    (r * tan(a / 2), tan(a / 2), 1)
    
    and the length of resulting vector in view plane  will be

    sqrt((r * tan(a / 2)) ^ 2, (tan(a / 2)) ^ 2, 1)

    which is equal to

    sqrt((1 / P[0,0]) ^ 2, (1 / P[1,1]) ^ 2, 1)

    where P is projection matrix
    */

    float NearPlaneCornerVLen = sqrt( 1 + 1.f/(ProjMatrix._11*ProjMatrix._11) + 1.f/ (ProjMatrix._22*ProjMatrix._22));

    return (CameraElevation - MaxRadius) / NearPlaneCornerVLen;
}

static float FindApproximateFarPlaneUsingRaytracing(const D3DXMATRIX &View, const D3DXMATRIX &Proj, const D3DXVECTOR3 &EarthCenter, float MinRadius, float MaxViewDistance)
{
    D3DXVECTOR3 EarthCenterV;
    D3DXVec3TransformCoord(&EarthCenterV, &EarthCenter, &View);

    D3DXMATRIX InvProj;
    D3DXMatrixInverse(&InvProj, nullptr, &Proj);

    float fFarPlaneZ = 1000.0f;

    const int iNumTestDirections = 5;
    for(int i=0; i<iNumTestDirections; ++i)
        for(int j=0; j<iNumTestDirections; ++j)
        {
            D3DXVECTOR3 PosPS, PosV;
            PosPS.x = (float)i / (float)(iNumTestDirections-1) * 2.f - 1.f;
            PosPS.y = (float)j / (float)(iNumTestDirections-1) * 2.f - 1.f;
            PosPS.z = 0; // Far plane is at 0 in complimentary depth buffer
            D3DXVec3TransformCoord(&PosV, &PosPS, &InvProj);

            D3DXVECTOR3 DirFromCamera;
            D3DXVec3Normalize(&DirFromCamera, &PosV);

            D3DXVECTOR2 IsecsWithBottomBoundSphere;
            GetRaySphereIntersection(D3DXVECTOR3(0.0f, 0.0f, 0.0f), DirFromCamera, EarthCenterV, MinRadius, IsecsWithBottomBoundSphere);

            float fNearIsecWithBottomSphere = IsecsWithBottomBoundSphere.x > 0 ? IsecsWithBottomBoundSphere.x : IsecsWithBottomBoundSphere.y;
            if( fNearIsecWithBottomSphere > 0 )
            {
                // The ray hits the Earth. Use hit point to compute camera space Z
                D3DXVECTOR3 HitPointV = DirFromCamera*fNearIsecWithBottomSphere;
                fFarPlaneZ = max(fFarPlaneZ, HitPointV.z);
            }
            else
            {
                // The ray misses the Earth. In that case the whole earth could be seen
                fFarPlaneZ = MaxViewDistance;
            }
        }

    return fFarPlaneZ;
}

void ComputeApproximateNearFarPlaneDist(const D3DXVECTOR3 &CameraPos,
                                        const D3DXMATRIX &ViewMatr,
                                        const D3DXMATRIX &ProjMatr, 
                                        const D3DXVECTOR3 &EarthCenter,
                                        float fEarthRadius,
                                        float fMinRadius,
                                        float fMaxRadius,
                                        float &fNearPlaneZ,
                                        float &fFarPlaneZ)
{    
    // Compute maximum view distance for the current camera altitude
    D3DXVECTOR3 f3CameraGlobalPos = CameraPos - EarthCenter;
    float fCameraElevationSqr = D3DXVec3Dot(&f3CameraGlobalPos, &f3CameraGlobalPos);
    float fMaxViewDistance = GetMaxViewDisstance(fCameraElevationSqr, fEarthRadius, fMaxRadius); 
    float fCameraElev = sqrt(fCameraElevationSqr);

    fNearPlaneZ = 50.f;
    if( fCameraElev > fMaxRadius )   
        fNearPlaneZ = AdjustNearPlane(fCameraElev, fMaxRadius, ProjMatr);

    fNearPlaneZ = max(fNearPlaneZ, 50);
    fFarPlaneZ = FindApproximateFarPlaneUsingRaytracing(ViewMatr, ProjMatr, EarthCenter, fMinRadius, fMaxViewDistance);

    fFarPlaneZ  = max(max(fFarPlaneZ, fNearPlaneZ+100), 1000);
}

void COutdoorLightScatteringSample::CorrectCameraNearAndFarPalens()
{
    float fEarthRadius = SAirScatteringAttribs().fEarthRadius;
    D3DXVECTOR3 EarthCenter(0, -fEarthRadius, 0);

    float fNearPlaneZ, fFarPlaneZ;
    ComputeApproximateNearFarPlaneDist(m_CameraPos,
                                       (D3DXMATRIX&)*mpCamera->GetViewMatrix(),
                                       (D3DXMATRIX&)*mpCamera->GetProjectionMatrix(),
                                       EarthCenter,
                                       fEarthRadius,
                                       fEarthRadius + m_fMinElevation,
                                       fEarthRadius + m_fMaxElevation,
                                       fNearPlaneZ,
                                       fFarPlaneZ);
    
    mpCamera->SetNearPlaneDistance( fNearPlaneZ );
    mpCamera->SetFarPlaneDistance( fFarPlaneZ );
    mpCamera->Update();

    m_CameraViewMatrix = (D3DXMATRIX&)*mpCamera->GetViewMatrix();
    m_CameraProjMatrix = (D3DXMATRIX&)*mpCamera->GetProjectionMatrix();
    m_CameraViewProjMatrix = m_CameraViewMatrix * m_CameraProjMatrix;
}

void COutdoorLightScatteringSample::UpdateCameraPos(float deltaSeconds)
{
    if( mpCameraController )
    {
        float fSpeedScale = max((m_CameraPos.y - 5000) / 20, 200.f);
        mpCameraController->SetMoveSpeed(fSpeedScale);

        mpCameraController->Update(deltaSeconds);
    }

    mpCamera->GetPosition(&m_CameraPos.x, &m_CameraPos.y, &m_CameraPos.z);

    float elevSmpInterval = m_TerrainRenderParams.m_TerrainAttribs.m_fElevationSamplingInterval;
    float elevScale = m_TerrainRenderParams.m_TerrainAttribs.m_fElevationScale;
    D3DXVECTOR2 elevSmpCoords = D3DXVECTOR2(m_CameraPos.x, m_CameraPos.z) / elevSmpInterval; 

    float fTerrainHeightUnderCamera = 100.0f+ m_pElevDataSource->GetInterpolatedHeight(elevSmpCoords.x, elevSmpCoords.y) * elevScale;
    
    float fXZMoveRadius = 512 * elevSmpInterval;
    float fMaxCameraAltitude = SAirScatteringAttribs().fAtmTopHeight * 10;

    bool bUpdateCamPos = false;
    float fDistFromCenter = sqrt(m_CameraPos.x*m_CameraPos.x + m_CameraPos.z*m_CameraPos.z);
    if( fDistFromCenter > fXZMoveRadius )
    {
        m_CameraPos.x *= fXZMoveRadius/fDistFromCenter;
        m_CameraPos.z *= fXZMoveRadius/fDistFromCenter;
        bUpdateCamPos = true;
    }
    if( m_CameraPos.y < fTerrainHeightUnderCamera )
    {
        m_CameraPos.y = fTerrainHeightUnderCamera; 
        bUpdateCamPos = true;
    }    
    if( m_CameraPos.y > fMaxCameraAltitude )
    {
        m_CameraPos.y = fMaxCameraAltitude;
        bUpdateCamPos = true;
    }

    if( bUpdateCamPos )
    {
        mpCamera->SetPosition(m_CameraPos.x, m_CameraPos.y, m_CameraPos.z);
        mpCamera->Update();
    }
}

//-----------------------------------------------------------------------------
void COutdoorLightScatteringSample::Update(double deltaSeconds)
{
    if( m_bAnimateSun )
    {
        auto &LightOrientationMatrix = *m_pDirLightOrienationCamera->GetParentMatrix();
        float3 RotationAxis( 0.5f, 0.3f, 0.0f );
        float3 LightDir = m_pDirLightOrienationCamera->GetLook() * -1;
        float fRotationScaler = ( LightDir.y > +0.2f ) ? 50.f : 1.f;
        float4x4 RotationMatrix = float4x4RotationAxis(RotationAxis, 0.02f * (float)deltaSeconds * fRotationScaler);
        LightOrientationMatrix = LightOrientationMatrix * RotationMatrix;
        m_pDirLightOrienationCamera->SetParentMatrix(LightOrientationMatrix);
    }

    UpdateCameraPos(static_cast<float>(deltaSeconds));
    CorrectCameraNearAndFarPalens();

    UpdatePostProcessingAttribs();
}

static D3DXVECTOR2 ProjToUV(const D3DXVECTOR2& f2ProjSpaceXY)
{
    return D3DXVECTOR2(0.5f + 0.5f*f2ProjSpaceXY.x, 0.5f - 0.5f*f2ProjSpaceXY.y);
}

static D3DXVECTOR4 GetDirOnLightN(const D3DXVECTOR4 &DirOnLightW, const D3DXMATRIX &ViewProj)
{
    D3DXVECTOR4 lightPosN;
    D3DXVec4Transform(&lightPosN, &DirOnLightW, &ViewProj);

    lightPosN.x /= lightPosN.w;
    lightPosN.y /= lightPosN.w;
    lightPosN.z /= lightPosN.w;

    float fDistToLightOnScreen = D3DXVec2Length( (D3DXVECTOR2*)&lightPosN );
    float fMaxDist = 100.0f;

    if( fDistToLightOnScreen > fMaxDist )
        fMaxDist *= fMaxDist/fDistToLightOnScreen;

    return lightPosN;
}

SLightAttribs COutdoorLightScatteringSample::UpdateLightAttributes(const SShadowMapAttribs &ShadowAttribs)
{
    D3DXVECTOR3 v3DirOnLight = (D3DXVECTOR3&)m_pDirLightOrienationCamera->GetLook();

    SLightAttribs LightAttribs;
    LightAttribs.ShadowAttribs = ShadowAttribs;
    LightAttribs.ShadowAttribs.bVisualizeCascades = ((CPUTCheckbox*)CPUTGetGuiController()->GetControl(ID_SHOW_CASCADES_CHECK))->GetCheckboxState() == CPUT_CHECKBOX_CHECKED;
    LightAttribs.f4DirOnLight = D3DXVECTOR4( v3DirOnLight.x, v3DirOnLight.y, v3DirOnLight.z, 0 );
    LightAttribs.f4LightScreenPos = GetDirOnLightN(LightAttribs.f4DirOnLight, m_CameraViewProjMatrix);
    LightAttribs.f4ExtraterrestrialSunColor = D3DXVECTOR4(10.0f, 10.0f, 10.0f, 10.0f) * m_fScatteringScale;

    // Note that in fact the outermost visible screen pixels do not lie exactly on the boundary (+1 or -1), but are biased by
    // 0.5 screen pixel size inwards. Using these adjusted boundaries improves precision and results in
    // smaller number of pixels which require inscattering correction
    LightAttribs.bIsLightOnScreen = abs(LightAttribs.f4LightScreenPos.x) <= 1.f - 1.f/(float)m_uiBackBufferWidth && 
                                    abs(LightAttribs.f4LightScreenPos.y) <= 1.f - 1.f/(float)m_uiBackBufferHeight;

    UpdateConstantBuffer(mpContext, m_pcbLightAttribs, &LightAttribs, sizeof(LightAttribs));

    return LightAttribs;
}

void COutdoorLightScatteringSample::UpdatePostProcessingAttribs()
{
    UINT uiSelectedItem;
    static_cast<CPUTDropdown*>(CPUTGetGuiController()->GetControl(ID_FIRST_CASCADE_TO_RAY_MARCH_DROPDOWN))->GetSelectedItem(uiSelectedItem);
    m_PPAttribs.m_iFirstCascade = min((int)uiSelectedItem, m_TerrainRenderParams.m_iNumShadowCascades - 1);
    m_PPAttribs.m_fFirstCascade = (float)m_PPAttribs.m_iFirstCascade;

    if( m_bEnableLightScattering )
    {
        float refinementThresholdVal;
        static_cast<CPUTSlider*>(CPUTGetGuiController()->GetControl(ID_REFINEMENT_THRESHOLD))->GetValue(refinementThresholdVal);

        m_PPAttribs.m_iNumCascades = m_TerrainRenderParams.m_iNumShadowCascades;
        m_PPAttribs.m_fNumCascades = static_cast<float>(m_TerrainRenderParams.m_iNumShadowCascades);
        m_PPAttribs.m_fRefinementThreshold = refinementThresholdVal;
        m_PPAttribs.m_fMaxShadowMapStep = static_cast<float>(m_uiShadowMapResolution / 4);
        m_PPAttribs.m_f2ShadowMapTexelSize = D3DXVECTOR2(1.f, 1.f) / static_cast<float>(m_uiShadowMapResolution);
        m_PPAttribs.m_uiShadowMapResolution = m_uiShadowMapResolution;
        // During the ray marching, on each step we move by the texel size in either horz 
        // or vert direction. So resolution of min/max mipmap should be the same as the 
        // resolution of the original shadow map
        m_PPAttribs.m_uiMinMaxShadowMapResolution = m_uiShadowMapResolution;
    }
}

SFrameAttribs COutdoorLightScatteringSample::GetFrameAttributes(float deltaSeconds, SLightAttribs *LightAttribs)
{
    D3DXMATRIX mViewProjInverseMatr;
    D3DXMatrixInverse(&mViewProjInverseMatr, NULL, &m_CameraViewProjMatrix);

    SFrameAttribs FrameAttribs;

    FrameAttribs.pd3dDevice = mpD3dDevice;
    FrameAttribs.pd3dDeviceContext = mpContext;
    FrameAttribs.dElapsedTime = deltaSeconds;
    FrameAttribs.pLightAttribs = LightAttribs;

    FrameAttribs.CameraAttribs.f4CameraPos = D3DXVECTOR4(m_CameraPos.x, m_CameraPos.y, m_CameraPos.z, 0);
    FrameAttribs.CameraAttribs.fNearPlaneZ = mpCamera->GetNearPlaneDistance();
    FrameAttribs.CameraAttribs.fFarPlaneZ  = mpCamera->GetFarPlaneDistance() * 0.999999f;
    D3DXMatrixTranspose( &FrameAttribs.CameraAttribs.mViewT, &m_CameraViewMatrix);
    D3DXMatrixTranspose( &FrameAttribs.CameraAttribs.mProjT, &m_CameraProjMatrix);
    D3DXMatrixTranspose( &FrameAttribs.CameraAttribs.mViewProjInvT, &mViewProjInverseMatr);

    FrameAttribs.pcbLightAttribs = m_pcbLightAttribs;

    FrameAttribs.ptex2DSrcColorBufferSRV = m_pOffscreenRenderTarget->GetColorResourceView();
    FrameAttribs.ptex2DSrcColorBufferRTV = m_pOffscreenRenderTarget->GetActiveRenderTargetView();
    FrameAttribs.ptex2DSrcDepthBufferSRV = m_pOffscreenDepth->GetDepthResourceView();
    FrameAttribs.ptex2DSrcDepthBufferDSV = m_pOffscreenDepth->GetActiveDepthStencilView();
    FrameAttribs.ptex2DShadowMapSRV      = m_pShadowMapSRV;
    FrameAttribs.pDstRTV                 = mpBackBufferRTV;

    return FrameAttribs;
}

// DirectX 11 render callback
//-----------------------------------------------------------------------------
void COutdoorLightScatteringSample::Render(double deltaSeconds)
{
    const float srgbClearColor[] = { 0.0993f, 0.0993f, 0.0993f, 1.0f }; //sRGB - red,green,blue,alpha pow(0.350, 2.2)

    mpContext->ClearRenderTargetView( mpBackBufferRTV,  srgbClearColor );
    mpContext->ClearDepthStencilView( mpDepthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0.0f, 0);

    CPUTRenderParametersDX drawParams(mpContext);

    SShadowMapAttribs shadowMapAttribs;
	RenderShadowMap(shadowMapAttribs);

    SLightAttribs lightAttribs = UpdateLightAttributes(shadowMapAttribs);

    if( m_bEnableLightScattering )
    {
        float pClearColor[4] = {0,0,0,0};
        m_pOffscreenRenderTarget->SetRenderTarget( drawParams, m_pOffscreenDepth, 0, pClearColor, true, 0.f );
    }

    m_EarthHemisphere.Render(mpContext,
                             m_CameraPos,
                             m_CameraViewProjMatrix,
                             m_pcbLightAttribs,
                             m_pLightSctrPP->GetMediaAttribsCB(),
                             m_pShadowMapSRV,
                             m_pLightSctrPP->GetPrecomputedNetDensitySRV(),
                             m_pLightSctrPP->GetAmbientSkyLightSRV(mpD3dDevice, mpContext),
                             false);

    if( m_bEnableLightScattering )
    {
        SFrameAttribs FrameAttribs = GetFrameAttributes(deltaSeconds, &lightAttribs);

        //perform the post processing, swapping the inverseworld view  projection matrix axes.
        m_pLightSctrPP->PerformPostProcessing(FrameAttribs, m_PPAttribs);

        m_pOffscreenRenderTarget->RestoreRenderTarget(drawParams);
    }

    // Draw GUI
    //
    if( m_iGUIMode )
        CPUTDrawGUI();
}

// Handle the shutdown event - clean up everything you created
//-----------------------------------------------------------------------------
void COutdoorLightScatteringSample::Shutdown()
{
    CPUT_DX11::Shutdown();
}


// Entrypoint for your sample
//-----------------------------------------------------------------------------
int WINAPI wWinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow )
{
    UNREFERENCED_PARAMETER(hInstance);
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);
    UNREFERENCED_PARAMETER(nCmdShow);

    // tell VS to report leaks at any exit of the program
    _CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );

    CPUTResult result=CPUT_SUCCESS;
    int returnCode=0;

    // create an instance of my sample
    COutdoorLightScatteringSample* sample = new COutdoorLightScatteringSample(); 


    // Initialize the system and give it the base CPUT resource directory (location of GUI images/etc)
    sample->CPUTInitialize(_L("CPUT//resources//"));

    // window parameters
    CPUTWindowCreationParams params;
    params.startFullscreen  = false;
    params.windowPositionX = 64;
    params.windowPositionY = 64;

    // device parameters
    params.deviceParams.refreshRate         = 60;
    params.deviceParams.swapChainBufferCount= 1;
    params.deviceParams.swapChainFormat     = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    params.deviceParams.swapChainUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;

    // parse out the parameter settings
    cString AssetFilename_NotUsed;
    cString CommandLine(lpCmdLine);
    sample->CPUTParseCommandLine(CommandLine, &params, &AssetFilename_NotUsed);       

    // create the window and device context
    result = sample->CPUTCreateWindowAndContext(_L("Outdoor Light Scattering Sample"), params);
    ASSERT( CPUTSUCCESS(result), _L("CPUT Error creating window and context.") );

    // start the main message loop
    returnCode = sample->CPUTMessageLoop();

	sample->DeviceShutdown();

    // cleanup resources
    delete sample; 

    // exit
    return returnCode;
}
