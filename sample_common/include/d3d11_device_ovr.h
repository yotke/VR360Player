#pragma once

#include <wrl/client.h>
#include "d3d11_device.h"
#include "utils.h"
#include "GeometricPrimitive.h"
#include "Effects.h"
#include "SimpleMath.h"

class CD3D11DeviceOVR : public CHWDevice
{
public:
	CD3D11DeviceOVR();
	virtual ~CD3D11DeviceOVR();
	virtual mfxStatus Init(mfxHDL hWindow, mfxU16 nViews, mfxU32 nAdapterNum);
	virtual mfxStatus Reset();
	virtual mfxStatus GetHandle(mfxHandleType type, mfxHDL *pHdl);
	virtual mfxStatus SetHandle(mfxHandleType type, mfxHDL hdl);
	virtual mfxStatus RenderFrame(mfxFrameSurface1 * pSurface, mfxFrameAllocator * pmfxAlloc);
	virtual void Close();
	virtual void UpdateTitle(mfxHDL /*h_wnd*/, double /*fps*/) { }
	virtual void SetMondelloInput(bool /*isMondelloInputEnabled*/) { }
	virtual void OnKey(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);

private:

	bool InitDevice(int, int, const LUID*);
	void SetAndClearRenderTarget(ID3D11RenderTargetView*, struct DepthBuffer*, float = 0, float = 0, float = 0, float = 0);
	void SetViewport(float, float, float, float);
	void ReleaseDevice();

	/*-------------------------------------------- d3d11 params --------------------------------------------*/
	HWND															Window;
	int																WinSizeW;
	int																WinSizeH;
	bool															Key[256];
	IDXGIFactory2												  * DXGIFactory;
	ID3D11Device												  * Device;
	ID3D11DeviceContext											  * Context;
	IDXGISwapChain												  * SwapChain;
	ID3D11Texture2D												  * BackBuffer;
	ID3D11RenderTargetView										  * BackBufferRT;

	/*-------------------------------------------- ovr params --------------------------------------------*/
	ovrTexture													  * mirrorTexture;
	OculusTexture												  * pEyeRenderTexture[2];
	DepthBuffer													  * pEyeDepthBuffer[2];
	Camera														  * mainCam;
	D3D11_TEXTURE2D_DESC											td;
	ovrHmd															HMD;
	ovrGraphicsLuid													luid;
	ovrHmdDesc														hmdDesc;
	ovrRecti														eyeRenderViewport[2];
	ovrEyeRenderDesc												eyeRenderDesc[2];

	/*-------------------------------------------- player params --------------------------------------------*/
	std::unique_ptr<DirectX::GeometricPrimitive>					m_shape;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>				m_texture;
	std::unique_ptr<DirectX::BasicEffect>							m_effect;
	Microsoft::WRL::ComPtr<ID3D11InputLayout>						m_inputLayout;
};