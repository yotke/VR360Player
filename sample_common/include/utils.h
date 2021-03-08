#pragma once

#include "d3d11.h"
#include "DirectXMath.h"
#include "SimpleMath.h"
#include "OVR_CAPI_D3D.h"

namespace DX
{
	inline void ThrowIfFailed(HRESULT hr)
	{
		if (FAILED(hr))
		{
			// Set a breakpoint on this line to catch DirectX API errors
			throw std::exception();
		}
	}
}

// clean up member COM pointers
template<typename T> void Release(T *&obj)
{
	if (!obj) return;
	obj->Release();
	obj = nullptr;
}

#ifndef VALIDATE
#define VALIDATE(x, msg) if (!(x)) { MessageBoxA(NULL, (msg), "VR360Player", MB_ICONERROR | MB_OK); exit(-1); }
#endif

//------------------------------------------------------------
// ovrSwapTextureSet wrapper class that also maintains the render target views
// needed for D3D11 rendering.
struct OculusTexture
{
	ovrHmd                   hmd;
	ovrSwapTextureSet      * TextureSet;
	static const int         TextureCount = 2;
	ID3D11RenderTargetView * TexRtv[TextureCount];

	OculusTexture() :
		hmd(nullptr),
		TextureSet(nullptr)
	{
		TexRtv[0] = TexRtv[1] = nullptr;
	}

	bool Init(ovrHmd _hmd, int sizeW, int sizeH, ID3D11Device * Device)
	{
		hmd = _hmd;

		D3D11_TEXTURE2D_DESC dsDesc;
		dsDesc.Width = sizeW;
		dsDesc.Height = sizeH;
		dsDesc.MipLevels = 1;
		dsDesc.ArraySize = 1;
		dsDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		dsDesc.SampleDesc.Count = 1;   // No multi-sampling allowed
		dsDesc.SampleDesc.Quality = 0;
		dsDesc.Usage = D3D11_USAGE_DEFAULT;
		dsDesc.CPUAccessFlags = 0;
		dsDesc.MiscFlags = 0;
		dsDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

		ovrResult result = ovr_CreateSwapTextureSetD3D11(hmd, Device, &dsDesc, ovrSwapTextureSetD3D11_Typeless, &TextureSet);
		if (!OVR_SUCCESS(result))
			return false;

		VALIDATE(TextureSet->TextureCount == TextureCount, "TextureCount mismatch.");

		for (int i = 0; i < TextureCount; ++i)
		{
			ovrD3D11Texture* tex = (ovrD3D11Texture*)&TextureSet->Textures[i];
			D3D11_RENDER_TARGET_VIEW_DESC rtvd = {};
			rtvd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			rtvd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
			Device->CreateRenderTargetView(tex->D3D11.pTexture, &rtvd, &TexRtv[i]);
		}

		return true;
	}

	~OculusTexture()
	{
		for (int i = 0; i < TextureCount; ++i)
		{
			Release(TexRtv[i]);
		}
		if (TextureSet)
		{
			ovr_DestroySwapTextureSet(hmd, TextureSet);
		}
	}

	void AdvanceToNextTexture()
	{
		TextureSet->CurrentIndex = (TextureSet->CurrentIndex + 1) % TextureSet->TextureCount;
	}
};

//------------------------------------------------------------
struct DepthBuffer
{
	ID3D11DepthStencilView * TexDsv;

	DepthBuffer(ID3D11Device * Device, int sizeW, int sizeH, int sampleCount = 1)
	{
		DXGI_FORMAT format = DXGI_FORMAT_D32_FLOAT;
		D3D11_TEXTURE2D_DESC dsDesc;
		dsDesc.Width = sizeW;
		dsDesc.Height = sizeH;
		dsDesc.MipLevels = 1;
		dsDesc.ArraySize = 1;
		dsDesc.Format = format;
		dsDesc.SampleDesc.Count = sampleCount;
		dsDesc.SampleDesc.Quality = 0;
		dsDesc.Usage = D3D11_USAGE_DEFAULT;
		dsDesc.CPUAccessFlags = 0;
		dsDesc.MiscFlags = 0;
		dsDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		ID3D11Texture2D * Tex;
		Device->CreateTexture2D(&dsDesc, NULL, &Tex);
		Device->CreateDepthStencilView(Tex, NULL, &TexDsv);
	}
	~DepthBuffer()
	{
		Release(TexDsv);
	}
};

//----------------------------------------------------------------
struct Camera
{
	DirectX::XMVECTOR Pos;
	DirectX::XMVECTOR Rot;
	Camera() {};
	Camera(DirectX::XMVECTOR * pos, DirectX::XMVECTOR * rot) : Pos(*pos), Rot(*rot) {};
	DirectX::XMMATRIX GetViewMatrix()
	{
		DirectX::XMVECTOR forward = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 0, -1, 0), Rot);
		return(DirectX::XMMatrixLookAtRH(Pos, DirectX::XMVectorAdd(Pos, forward), DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 1, 0, 0), Rot)));
	}

	static void* operator new(std::size_t /*size*/)
	{
		return _aligned_malloc(sizeof(Camera), __alignof(Camera));
	}

	static void operator delete(void* p)
	{
		_aligned_free(p);
	}
};