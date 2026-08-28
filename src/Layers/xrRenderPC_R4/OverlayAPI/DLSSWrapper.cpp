#include "stdafx.h"
#include <d3d11on12.h>

#include "DLSSWrapper.h"

DLSSWrapper g_DLSSWrapper;
extern RHI_API ID3D12Fence* FakeFence;
extern RHI_API ID3D12Device* FAke_d3d12device;
extern RHI_API ID3D12CommandQueue* Fake_d3d12queue;
extern RHI_API ID3D12CommandAllocator* FAke_d3d12allocator;
extern RHI_API ID3D12GraphicsCommandList* FAke_d3d12commandList;
extern RHI_API ID3D11On12Device2* FAke_d3d11on12device;

extern ENGINE_API u32 ps_render_scale_preset;
extern ENGINE_API float ps_render_scale;

static const struct
{
	float MinScale;
	NVSDK_NGX_PerfQuality_Value Quality;
	const char* RenderPreset;
}
PresetTable[] =
{
	{ 0.9f, NVSDK_NGX_PerfQuality_Value_DLAA,             NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA },
	{ 0.7f, NVSDK_NGX_PerfQuality_Value_MaxQuality,       NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Quality },
	{ 0.6f, NVSDK_NGX_PerfQuality_Value_Balanced,         NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Balanced },
	{ 0.5f, NVSDK_NGX_PerfQuality_Value_MaxPerf,          NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_Performance },
	{ 0.0f, NVSDK_NGX_PerfQuality_Value_UltraPerformance, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_UltraPerformance },
};

static constexpr u32 PresetCount = u32(std::size(PresetTable));

static UINT64 FakeFenceValue = 0;
static UINT64 FakePendingValue = 0;
static bool FakeListRecording = true;
static HANDLE FakeFenceEvent = nullptr;

static void WaitFakeFence(UINT64 FenceValue)
{
	if (FakeFenceEvent && FakeFence->GetCompletedValue() < FenceValue)
	{
		R_CHK(FakeFence->SetEventOnCompletion(FenceValue, FakeFenceEvent));
		WaitForSingleObject(FakeFenceEvent, INFINITE);
	}
}

static void BeginFakeCommandList()
{
	if (FakeListRecording)
	{
		return;
	}

	WaitFakeFence(FakePendingValue);

	R_CHK(FAke_d3d12allocator->Reset());
	R_CHK(FAke_d3d12commandList->Reset(FAke_d3d12allocator, nullptr));
	FakeListRecording = true;
}

static bool SubmitFakeCommandList(UINT64& OutFenceValue)
{
	if (!FakeListRecording || FAILED(FAke_d3d12commandList->Close()))
	{
		return false;
	}

	FakeListRecording = false;

	ID3D12CommandList* Lists[] = { FAke_d3d12commandList };
	Fake_d3d12queue->ExecuteCommandLists(1, Lists);

	OutFenceValue = ++FakeFenceValue;

	if (FAILED(Fake_d3d12queue->Signal(FakeFence, OutFenceValue)))
	{
		return false;
	}

	FakePendingValue = OutFenceValue;
	return true;
}

u32 DLSSWrapper::GetOptimalPresetForScale(float scale)
{
	if (ps_render_scale_preset != 5)
	{
		return std::min(ps_render_scale_preset, PresetCount - 1);
	}

	u32 Index = 0;
	while (Index + 1 < PresetCount && scale < PresetTable[Index].MinScale)
	{
		++Index;
	}

	return Index;
}

void DLSSWrapper::Create()
{
	Destroy();

	if (RFeatureLevel < D3D_FEATURE_LEVEL_11_1)
	{
		return;
	}

#ifdef IXR_X64
	NVSDK_NGX_Result Result;

	if (!DLSSInited)
	{
		Result = NVSDK_NGX_D3D12_Init(1602, L"", FAke_d3d12device);

		if (Result != NVSDK_NGX_Result_Success)
		{
			return;
		}

		DLSSInited = true;
	}

	Result = NVSDK_NGX_D3D12_GetCapabilityParameters(&NgxParameters);

	if (Result != NVSDK_NGX_Result_Success)
	{
		return;
	}

	uint32_t NeedsUpdatedDriver = 0;
	Result = NgxParameters->Get(NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &NeedsUpdatedDriver);

	if (Result == NVSDK_NGX_Result_Success && NeedsUpdatedDriver)
	{
		Msg("! PLEASE UPDATE YOUR DRIVER");
	}

	uint32_t DlssAvailable = 0;
	Result = NgxParameters->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &DlssAvailable);

	if (Result != NVSDK_NGX_Result_Success || !DlssAvailable)
	{
		NVSDK_NGX_D3D12_DestroyParameters(NgxParameters);
		NgxParameters = nullptr;
		return;
	}

	FakeFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	Created = true;
#endif
}

bool DLSSWrapper::GetRenderScale(float& RenderScale)
{
	if (!Created || !NgxParameters)
	{
		Msg("! GetRenderScale DLSSWrapper not valid. Fallback!");
		return false;
	}

	const NVSDK_NGX_PerfQuality_Value PerfQualityValue = PresetTable[GetOptimalPresetForScale(ps_render_scale)].Quality;

	u32 RenderW = 0, RenderH = 0, MaxW = 0, MinW = 0, MaxH = 0, MinH = 0; float Sharp = 0;
	NVSDK_NGX_Result Result = NGX_DLSS_GET_OPTIMAL_SETTINGS(NgxParameters, Device.TargetWidth, Device.TargetHeight, PerfQualityValue, &RenderW, &RenderH, &MaxW, &MaxH, &MinW, &MinH, &Sharp);

	if (Result != NVSDK_NGX_Result_Success || !RenderH)
	{
		Msg("! NGX_DLSS_GET_OPTIMAL_SETTINGS not valid. Fallback!");
		return false;
	}

	Msg("* DLSS Target - %dx%d, Min - %dx%d, Max - %dx%d, Sharp - %f", RenderW, RenderH, MinW, MinH, MaxW, MaxH, Sharp);
	RenderScale = float(RenderH) / float(Device.TargetHeight);

	return true;
}

void DLSSWrapper::Resize(const ContextParameters& Parameters)
{
	PROF_EVENT("DLSSWrapper::Resize");

	if (!Created)
	{
		return;
	}

#ifdef IXR_X64
	BeginFakeCommandList();

	if (Handle != nullptr)
	{
		NVSDK_NGX_D3D12_ReleaseFeature(Handle);
		Handle = nullptr;
	}

	const u32 PresetID = GetOptimalPresetForScale(ps_render_scale);
	const char* RenderPreset = PresetTable[PresetID].RenderPreset;

	NgxParameters->Set(RenderPreset, static_cast<int>(NVSDK_NGX_DLSS_Hint_Render_Preset_K));

	Msg("* Resize DLSSWrapper Render Preset [%s]", RenderPreset);

	NVSDK_NGX_DLSS_Create_Params DLSSCreateParams = {};

	DLSSCreateParams.Feature.InWidth = Parameters.renderSize.x;
	DLSSCreateParams.Feature.InHeight = Parameters.renderSize.y;

	DLSSCreateParams.Feature.InTargetWidth = Parameters.displaySize.x;
	DLSSCreateParams.Feature.InTargetHeight = Parameters.displaySize.y;

	DLSSCreateParams.Feature.InPerfQualityValue = PresetTable[PresetID].Quality;
	DLSSCreateParams.InFeatureCreateFlags = 0;

	DLSSCreateParams.InFeatureCreateFlags |= NVSDK_NGX_DLSS_Feature_Flags_IsHDR;
	DLSSCreateParams.InFeatureCreateFlags |= NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
	DLSSCreateParams.InFeatureCreateFlags |= NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;

	NVSDK_NGX_Result Result = NGX_D3D12_CREATE_DLSS_EXT(FAke_d3d12commandList, 0, 0, &Handle, NgxParameters, &DLSSCreateParams);

	if (Result != NVSDK_NGX_Result_Success)
	{
		Msg("! NGX_D3D12_CREATE_DLSS_EXT not valid. Need use FSR");
		Handle = nullptr;
		Created = false;
		return;
	}

	UINT64 FenceValue = 0;
	SubmitFakeCommandList(FenceValue);

	DisplaySize = Parameters.displaySize;
#endif
}

void DLSSWrapper::Destroy()
{
#ifdef IXR_X64
	WaitFakeFence(FakePendingValue);

	if (Handle != nullptr)
	{
		NVSDK_NGX_D3D12_ReleaseFeature(Handle);
		Handle = nullptr;
	}

	if (NgxParameters != nullptr)
	{
		NVSDK_NGX_D3D12_DestroyParameters(NgxParameters);
		NgxParameters = nullptr;
	}

	if (DLSSInited)
	{
		NVSDK_NGX_D3D12_Shutdown1(nullptr);
		DLSSInited = false;
	}

	if (FakeFenceEvent != nullptr)
	{
		CloseHandle(FakeFenceEvent);
		FakeFenceEvent = nullptr;
	}

	Created = false;
#endif
}

bool DLSSWrapper::Draw(const DrawParameters& params)
{
	if(!Created)
	{
		Msg("! DLSSWrapper not created. Need use FSR");
		return false;
	}

#ifdef IXR_X64
	ID3D11Texture2D* const Wrapped[] =
	{
		params.unresolvedColorResource,
		params.resolvedColorResource,
		params.depthbufferResource,
		params.motionvectorResource
	};

	constexpr u32 ResourceCount = u32(std::size(Wrapped));
	ID3D12Resource* Unwrapped[ResourceCount] = {};

	RContext->Flush();
	BeginFakeCommandList();

	u32 UnwrappedCount = 0;

	for (; UnwrappedCount < ResourceCount; ++UnwrappedCount)
	{
		if (FAILED(FAke_d3d11on12device->UnwrapUnderlyingResource(Wrapped[UnwrappedCount], Fake_d3d12queue, IID_PPV_ARGS(&Unwrapped[UnwrappedCount]))))
		{
			break;
		}
	}

	NVSDK_NGX_Result Result = NVSDK_NGX_Result_Fail;

	if (UnwrappedCount == ResourceCount)
	{
		NVSDK_NGX_D3D12_DLSS_Eval_Params DLSSEvalParams = {};

		DLSSEvalParams.Feature.pInColor = Unwrapped[0];
		DLSSEvalParams.Feature.pInOutput = Unwrapped[1];
		DLSSEvalParams.pInDepth = Unwrapped[2];
		DLSSEvalParams.pInMotionVectors = Unwrapped[3];

		DLSSEvalParams.Feature.InSharpness = params.sharpness;

		DLSSEvalParams.InRenderSubrectDimensions.Width = params.renderWidth;
		DLSSEvalParams.InRenderSubrectDimensions.Height = params.renderHeight;

		DLSSEvalParams.InJitterOffsetX = params.cameraJitterX;
		DLSSEvalParams.InJitterOffsetY = params.cameraJitterY;

		DLSSEvalParams.InReset = params.cameraReset;

		DLSSEvalParams.InMVScaleX = -(float)params.renderWidth * 0.5f;
		DLSSEvalParams.InMVScaleY = (float)params.renderHeight * 0.5f;

		DLSSEvalParams.InFrameTimeDeltaInMsec = params.frameTimeDelta;

		Result = NGX_D3D12_EVALUATE_DLSS_EXT(FAke_d3d12commandList, Handle, NgxParameters, &DLSSEvalParams);
	}

	UINT64 FenceValue = 0;
	const bool Submitted = SubmitFakeCommandList(FenceValue);

	ID3D12Fence* Fences[] = { FakeFence };
	UINT64 Values[] = { FenceValue };

	for (u32 i = 0; i < UnwrappedCount; ++i)
	{
		R_CHK(FAke_d3d11on12device->ReturnUnderlyingResource(Wrapped[i], Submitted ? 1 : 0, Values, Fences));
		Unwrapped[i]->Release();
	}

	if (!Submitted)
	{
		Msg("! DLSSWrapper command list submit failed. Need use FSR");
		Created = false;
		return false;
	}

	if(Result != NVSDK_NGX_Result_Success)
	{
		Msg("! NGX_D3D12_EVALUATE_DLSS_EXT not valid. Need use FSR");
		return false;
	}
#endif

	return true;
}

DLSSWrapper::~DLSSWrapper()
{
	Destroy();
}
