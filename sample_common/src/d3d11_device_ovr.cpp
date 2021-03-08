
#include "mfx_samples_config.h"

#if defined(_WIN32) || defined(_WIN64)

#include "sample_defs.h"

#if MFX_D3D11_SUPPORT

#include "d3d11_device_ovr.h"

#include <ittnotify.h>
__itt_domain* devDomain = __itt_domain_create(L"CD3D11DeiceOVR.Domain.Global");
__itt_string_handle* handle_buffers = __itt_string_handle_create(L"Clear buffers");
__itt_string_handle* handle_draw = __itt_string_handle_create(L"Draw Sphere");
__itt_string_handle* handle_submit = __itt_string_handle_create(L"ovr_SubmitFrame");
__itt_string_handle* handle_copy = __itt_string_handle_create(L"CopyResource");
__itt_string_handle* handle_present = __itt_string_handle_create(L"Present");

using namespace DirectX;
using namespace DirectX::SimpleMath;

extern bool pauseAction;
extern bool ffAction;
extern bool noffAction;

CD3D11DeviceOVR::CD3D11DeviceOVR() :
	Window(nullptr),
	WinSizeW(0),
	WinSizeH(0),
	DXGIFactory(nullptr),
	Device(nullptr),
	Context(nullptr),
	SwapChain(nullptr),
	BackBuffer(nullptr),
	BackBufferRT(nullptr),
	mirrorTexture(nullptr),
	mainCam(nullptr)
{
	td = {};
	pEyeRenderTexture[0] = nullptr;
	pEyeRenderTexture[1] = nullptr;
	pEyeDepthBuffer[0] = nullptr;
	pEyeDepthBuffer[1] = nullptr;
	// Clear input
	for (int i = 0; i < sizeof(Key) / sizeof(Key[0]); ++i)
		Key[i] = false;
}

CD3D11DeviceOVR::~CD3D11DeviceOVR()
{
	Close();
}

void CD3D11DeviceOVR::Close()
{
	m_effect.reset();
	m_inputLayout.Reset();
	m_texture.Reset();
	m_shape.reset();
	delete mainCam;
	if (mirrorTexture) ovr_DestroyMirrorTexture(HMD, mirrorTexture);
	for (int eye = 0; eye < 2; ++eye)
	{
		delete pEyeRenderTexture[eye];
		delete pEyeDepthBuffer[eye];
	}
	ReleaseDevice();
	ovr_Destroy(HMD);
	ovr_Shutdown();
}

mfxStatus CD3D11DeviceOVR::Init(mfxHDL hWindow, mfxU16 /*nViews*/, mfxU32 /*nAdapterNum*/)
{
	Window = (HWND)hWindow;

	mfxStatus sts = MFX_ERR_NONE;

	// Initializes LibOVR, and the Rift
	ovrResult result = ovr_Initialize(nullptr);
	VALIDATE(OVR_SUCCESS(result), "Failed to initialize libOVR.");

	result = ovr_Create(&HMD, &luid);
	VALIDATE(OVR_SUCCESS(result), "Failed to initialize session.");

	hmdDesc = ovr_GetHmdDesc(HMD);

	VALIDATE(InitDevice(hmdDesc.Resolution.w / 2, hmdDesc.Resolution.h / 2, reinterpret_cast<LUID*>(&luid)), "Failed to initiallize device.");

	for (int eye = 0; eye < 2; ++eye)
	{
		ovrSizei idealSize = ovr_GetFovTextureSize(HMD, (ovrEyeType)eye, hmdDesc.DefaultEyeFov[eye], 1.0f);
		pEyeRenderTexture[eye] = new OculusTexture();
		if (!pEyeRenderTexture[eye]->Init(HMD, idealSize.w, idealSize.h, Device))
		{
			VALIDATE(OVR_SUCCESS(result), "Failed to create eye texture.");
		}
		pEyeDepthBuffer[eye] = new DepthBuffer(Device, idealSize.w, idealSize.h);
		eyeRenderViewport[eye].Pos.x = 0;
		eyeRenderViewport[eye].Pos.y = 0;
		eyeRenderViewport[eye].Size = idealSize;
		if (!pEyeRenderTexture[eye]->TextureSet)
		{
			VALIDATE(false, "Failed to create texture.");
		}
	}

	// Create a mirror to see on the monitor.
	td.ArraySize = 1;
	td.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	td.Width = WinSizeW;
	td.Height = WinSizeH;
	td.Usage = D3D11_USAGE_DEFAULT;
	td.SampleDesc.Count = 1;
	td.MipLevels = 1;
	result = ovr_CreateMirrorTextureD3D11(HMD, Device, &td, 0, &mirrorTexture);
	VALIDATE(OVR_SUCCESS(result), "Failed to create mirror texture.");
	

	// Create the sphere
	m_effect = std::make_unique<BasicEffect>(Device);
	m_effect->SetTextureEnabled(true);
	m_effect->SetLightingEnabled(false);
	m_shape = GeometricPrimitive::CreateGeoSphere(Context, 10.0F, 5U, false);
	m_shape->CreateInputLayout(m_effect.get(), m_inputLayout.ReleaseAndGetAddressOf());

	// Create camera
	XMVECTOR cameraPos = XMVectorSet(0.0f, 0.0f, 0.0f, 0);
	XMVECTOR quadIdentity = XMQuaternionIdentity();
	mainCam = new Camera(&cameraPos, &quadIdentity);

	eyeRenderDesc[0] = ovr_GetRenderDesc(HMD, ovrEye_Left, hmdDesc.DefaultEyeFov[0]);
	eyeRenderDesc[1] = ovr_GetRenderDesc(HMD, ovrEye_Right, hmdDesc.DefaultEyeFov[1]);

	// turn on multithreading for the Context
	CComQIPtr<ID3D10Multithread> p_mt(Context);

	if (p_mt)
		p_mt->SetMultithreadProtected(true);
	else
		return MFX_ERR_DEVICE_FAILED;

	return sts;
}

mfxStatus CD3D11DeviceOVR::RenderFrame(mfxFrameSurface1 * pSrf, mfxFrameAllocator * pAlloc)
{
	mfxHDLPair pair = { NULL };
	mfxStatus sts = pAlloc->GetHDL(pAlloc->pthis, pSrf->Data.MemId, (mfxHDL*)&pair);
	MSDK_CHECK_STATUS(sts, "pAlloc->GetHDL failed");

	ID3D11Texture2D  *pRTTexture2D = reinterpret_cast<ID3D11Texture2D*>(pair.first);

	XMVECTOR forward = XMVector3Rotate(XMVectorSet(0, 0, -0.05f, 0), mainCam->Rot);
	XMVECTOR right = XMVector3Rotate(XMVectorSet(0.05f, 0, 0, 0), mainCam->Rot);
	XMVECTOR up = XMVector3Rotate(XMVectorSet(0, 0.05f, 0, 0), mainCam->Rot);
	if (Key['W'] || Key[VK_UP])	  mainCam->Pos = XMVectorAdd(mainCam->Pos, forward);
	if (Key['S'] || Key[VK_DOWN]) mainCam->Pos = XMVectorSubtract(mainCam->Pos, forward);
	if (Key['D'])                         mainCam->Pos = XMVectorAdd(mainCam->Pos, right);
	if (Key['A'])                         mainCam->Pos = XMVectorSubtract(mainCam->Pos, right);
	if (Key['E'])                         mainCam->Pos = XMVectorAdd(mainCam->Pos, up);
	if (Key['C'])                         mainCam->Pos = XMVectorSubtract(mainCam->Pos, up);
	static float Yaw = 0;
	static float Pitch = 0;
	if (Key[VK_LEFT])  mainCam->Rot = XMQuaternionRotationRollPitchYaw(Pitch, Yaw += 0.02f, 0);
	if (Key[VK_RIGHT]) mainCam->Rot = XMQuaternionRotationRollPitchYaw(Pitch, Yaw -= 0.02f, 0);
	if (Key['Q'])	mainCam->Rot = XMQuaternionRotationRollPitchYaw(Pitch += 0.02f, Yaw, 0);
	if (Key['Z'])	mainCam->Rot = XMQuaternionRotationRollPitchYaw(Pitch -= 0.02f, Yaw, 0);

	// Get both eye poses simultaneously, with IPD offset already included. 
	ovrPosef         EyeRenderPose[2];
	ovrVector3f      HmdToEyeViewOffset[2] = { eyeRenderDesc[0].HmdToEyeViewOffset,
		eyeRenderDesc[1].HmdToEyeViewOffset };
	double frameTime = ovr_GetPredictedDisplayTime(HMD, 0);
	// Keeping sensorSampleTime as close to ovr_GetTrackingState as possible - fed into the layer
	double           sensorSampleTime = ovr_GetTimeInSeconds();
	ovrTrackingState hmdState = ovr_GetTrackingState(HMD, frameTime, ovrTrue);
	ovr_CalcEyePoses(hmdState.HeadPose.ThePose, HmdToEyeViewOffset, EyeRenderPose);

	// Render Scene to Eye Buffers
	for (int eye = 0; eye < 2; ++eye)
	{
		// Increment to use next texture, just before writing
		pEyeRenderTexture[eye]->AdvanceToNextTexture();

		// Clear and set up rendertarget
		int texIndex = pEyeRenderTexture[eye]->TextureSet->CurrentIndex;
		__itt_task_begin(devDomain, __itt_null, __itt_null, handle_buffers);
		SetAndClearRenderTarget(pEyeRenderTexture[eye]->TexRtv[texIndex], pEyeDepthBuffer[eye]);
		__itt_task_end(devDomain);
		SetViewport((float)eyeRenderViewport[eye].Pos.x, (float)eyeRenderViewport[eye].Pos.y,
			(float)eyeRenderViewport[eye].Size.w, (float)eyeRenderViewport[eye].Size.h);

		//Get the pose information in XM format
		XMVECTOR eyeQuat = XMVectorSet(EyeRenderPose[eye].Orientation.x, EyeRenderPose[eye].Orientation.y,
			EyeRenderPose[eye].Orientation.z, EyeRenderPose[eye].Orientation.w);
		XMVECTOR eyePos = XMVectorSet(EyeRenderPose[eye].Position.x, EyeRenderPose[eye].Position.y, EyeRenderPose[eye].Position.z, 0);

		// Get view and projection matrices for the Rift camera
		XMVECTOR CombinedPos = XMVectorAdd(mainCam->Pos, XMVector3Rotate(eyePos, mainCam->Rot));
		XMVECTOR finalRot = XMQuaternionMultiply(eyeQuat, mainCam->Rot);
		Camera finalCam(&CombinedPos, &finalRot);
		XMMATRIX view = finalCam.GetViewMatrix();
		ovrMatrix4f p = ovrMatrix4f_Projection(eyeRenderDesc[eye].Fov, 0.2f, 1000.0f, ovrProjection_RightHanded);
		XMMATRIX proj = XMMatrixSet(p.M[0][0], p.M[1][0], p.M[2][0], p.M[3][0],
									p.M[0][1], p.M[1][1], p.M[2][1], p.M[3][1],
									p.M[0][2], p.M[1][2], p.M[2][2], p.M[3][2],
									p.M[0][3], p.M[1][3], p.M[2][3], p.M[3][3]);

		// render texture on the sphere
		Device->CreateShaderResourceView(pRTTexture2D, nullptr, m_texture.ReleaseAndGetAddressOf());
		m_effect->SetTexture(m_texture.Get());
		m_effect->SetView(view);
		m_effect->SetProjection(proj);
		__itt_task_begin(devDomain, __itt_null, __itt_null, handle_draw);
		m_shape->Draw(m_effect.get(), m_inputLayout.Get());
		__itt_task_end(devDomain);
	}

	// Initialize our single full screen Fov layer.
	ovrLayerEyeFov ld = {};
	ld.Header.Type = ovrLayerType_EyeFov;
	ld.Header.Flags = 0;

	for (int eye = 0; eye < 2; ++eye)
	{
		ld.ColorTexture[eye] = pEyeRenderTexture[eye]->TextureSet;
		ld.Viewport[eye] = eyeRenderViewport[eye];
		ld.Fov[eye] = hmdDesc.DefaultEyeFov[eye];
		ld.RenderPose[eye] = EyeRenderPose[eye];
		ld.SensorSampleTime = sensorSampleTime;
	}

	ovrLayerHeader* layers = &ld.Header;
	__itt_task_begin(devDomain, __itt_null, __itt_null, handle_submit);
	ovr_SubmitFrame(HMD, 0, nullptr, &layers, 1);
	__itt_task_end(devDomain);

	// Render mirror
	ovrD3D11Texture* tex = (ovrD3D11Texture*)mirrorTexture;
	__itt_task_begin(devDomain, __itt_null, __itt_null, handle_copy);
	Context->CopyResource(BackBuffer, tex->D3D11.pTexture);
	__itt_task_end(devDomain);
	__itt_task_begin(devDomain, __itt_null, __itt_null, handle_present);
	SwapChain->Present(0, 0);
	__itt_task_end(devDomain);

	return MFX_ERR_NONE;
}

mfxStatus CD3D11DeviceOVR::Reset()
{
	RECT size = { 0, 0, WinSizeW, WinSizeH };
	AdjustWindowRect(&size, WS_OVERLAPPEDWINDOW, false);
	const UINT flags = SWP_NOMOVE | SWP_NOZORDER | SWP_SHOWWINDOW;
	SetWindowPos(Window, nullptr, 0, 0, size.right - size.left, size.bottom - size.top, flags);

	// Create swap chain
	DXGI_SWAP_CHAIN_DESC scDesc;
	memset(&scDesc, 0, sizeof(scDesc));
	scDesc.BufferCount = 2;
	scDesc.BufferDesc.Width = WinSizeW;
	scDesc.BufferDesc.Height = WinSizeH;
	scDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	scDesc.BufferDesc.RefreshRate.Denominator = 1;
	scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	scDesc.OutputWindow = Window;
	scDesc.SampleDesc.Count = 1;
	scDesc.Windowed = true;
	scDesc.SwapEffect = DXGI_SWAP_EFFECT_SEQUENTIAL;
	HRESULT hr = DXGIFactory->CreateSwapChain(Device, &scDesc, &SwapChain);
	VALIDATE((hr == ERROR_SUCCESS), "CreateSwapChain failed");

	// Create backbuffer
	SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&BackBuffer);
	hr = Device->CreateRenderTargetView(BackBuffer, NULL, &BackBufferRT);
	VALIDATE((hr == ERROR_SUCCESS), "CreateRenderTargetView failed");

	// Set max frame latency to 1
	IDXGIDevice1* DXGIDevice1 = nullptr;
	hr = Device->QueryInterface(__uuidof(IDXGIDevice1), (void**)&DXGIDevice1);
	DXGIDevice1->SetMaximumFrameLatency(1);
	Release(DXGIDevice1);
	VALIDATE((hr == ERROR_SUCCESS), "QueryInterface failed");

	return MFX_ERR_NONE;
}

mfxStatus CD3D11DeviceOVR::GetHandle(mfxHandleType type, mfxHDL * pHdl)
{
	{
		if (MFX_HANDLE_D3D11_DEVICE == type)
		{
			*pHdl = Device;
			return MFX_ERR_NONE;
		}
		return MFX_ERR_UNSUPPORTED;
	}
}

mfxStatus CD3D11DeviceOVR::SetHandle(mfxHandleType type, mfxHDL hdl)
{
	if (MFX_HANDLE_DEVICEWINDOW == type && hdl != NULL) //for render window handle
	{
		Window = (HWND)hdl;
		return MFX_ERR_NONE;
	}
	return MFX_ERR_UNSUPPORTED;
}

bool CD3D11DeviceOVR::InitDevice(int vpW, int vpH, const LUID* pLuid)
{
	WinSizeW = vpW;
	WinSizeH = vpH;

	HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory), (void**)(&DXGIFactory));
	VALIDATE((hr == ERROR_SUCCESS), "CreateDXGIFactory1 failed");

	IDXGIAdapter * Adapter = nullptr;
	for (UINT iAdapter = 0; DXGIFactory->EnumAdapters(iAdapter, &Adapter) != DXGI_ERROR_NOT_FOUND; ++iAdapter)
	{
		DXGI_ADAPTER_DESC adapterDesc;
		Adapter->GetDesc(&adapterDesc);
		if ((pLuid == nullptr) || memcmp(&adapterDesc.AdapterLuid, pLuid, sizeof(LUID)) == 0)
			break;
		Release(Adapter);
	}

	auto DriverType = Adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;
	hr = D3D11CreateDevice(Adapter, DriverType, 0, 0, 0, 0, D3D11_SDK_VERSION, &Device, 0, &Context);
	Release(Adapter);
	VALIDATE((hr == ERROR_SUCCESS), "D3D11CreateDevice failed");

	return true;
}

void CD3D11DeviceOVR::SetAndClearRenderTarget(ID3D11RenderTargetView * rendertarget, struct DepthBuffer * depthbuffer, float R, float G, float B, float A)
{
	float black[] = { R, G, B, A }; // Important that alpha=0, if want pixels to be transparent, for manual layers
	Context->OMSetRenderTargets(1, &rendertarget, (depthbuffer ? depthbuffer->TexDsv : nullptr));
	Context->ClearRenderTargetView(rendertarget, black);
	if (depthbuffer)
		Context->ClearDepthStencilView(depthbuffer->TexDsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1, 0);
}

void CD3D11DeviceOVR::SetViewport(float vpX, float vpY, float vpW, float vpH)
{
	D3D11_VIEWPORT D3Dvp;
	D3Dvp.Width = vpW;    D3Dvp.Height = vpH;
	D3Dvp.MinDepth = 0;   D3Dvp.MaxDepth = 1;
	D3Dvp.TopLeftX = vpX; D3Dvp.TopLeftY = vpY;
	Context->RSSetViewports(1, &D3Dvp);
}

void CD3D11DeviceOVR::ReleaseDevice()
{
	Release(BackBuffer);
	Release(BackBufferRT);
	if (SwapChain)
	{
		SwapChain->SetFullscreenState(FALSE, NULL);
		Release(SwapChain);
	}
	Release(Context);
	Release(Device);
	Release(DXGIFactory);
}

void CD3D11DeviceOVR::OnKey(HWND /*hWnd*/, UINT Msg, WPARAM wParam, LPARAM /*lParam*/)
{
	switch (Msg)
	{
	case WM_KEYDOWN:
		Key[wParam] = true;
		break;
	case WM_KEYUP:
		Key[wParam] = false;
		break;
	}
	if (Key[' '])
	{
		pauseAction = !pauseAction;
	}
	if (Key['F'])
	{
		ffAction = true;
	}
	if (Key['N'])
	{
		noffAction = true;
	}
}

#endif // #if MFX_D3D11_SUPPORT
#endif // #if defined(_WIN32) || defined(_WIN64)
