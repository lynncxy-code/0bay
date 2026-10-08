// Copyright 2025 Tracer Interactive, LLC. All Rights Reserved.
#pragma once

#ifdef TRACERWEBACCELERATEDPAINT_EXPORTS
#define TRACERWEBACCELERATEDPAINT_API __declspec(dllexport)
#else
#define TRACERWEBACCELERATEDPAINT_API __declspec(dllimport)
#endif

struct CBrowserWindowSharedTextureImpl;
struct TRACERWEBACCELERATEDPAINT_API CBrowserWindowSharedTexture
{
	friend class CBrowserWindowRenderHelper;

	CBrowserWindowSharedTexture();
	CBrowserWindowSharedTexture(const CBrowserWindowSharedTexture& Other);
	CBrowserWindowSharedTexture& operator=(const CBrowserWindowSharedTexture& Other);

	~CBrowserWindowSharedTexture();

	UINT GetWidth() const;
	UINT GetHeight() const;

private:

	CBrowserWindowSharedTextureImpl* pImpl;
};

struct CBrowserWindowTextureImpl;
struct TRACERWEBACCELERATEDPAINT_API CBrowserWindowTexture
{
	friend class CBrowserWindowRenderHelper;

	CBrowserWindowTexture();
	CBrowserWindowTexture(const CBrowserWindowTexture& Other);
	CBrowserWindowTexture& operator=(const CBrowserWindowTexture& Other);

	virtual ~CBrowserWindowTexture();

	virtual bool IsValid() const;

	virtual void Reset();
	virtual void Resize(UINT Width, UINT Height);

	virtual void* GetNativeResource() const;

	virtual UINT GetNativeResourceWidth() const;
	virtual UINT GetNativeResourceHeight() const;

	virtual bool HasInterop() const;
	virtual void ResetInterop();

	virtual bool CreateInterop(class CBrowserWindowRenderHelper* Helper);

private:

	CBrowserWindowTextureImpl* pImpl;
};

class CBrowserWindowRenderHelperImpl;
class TRACERWEBACCELERATEDPAINT_API CBrowserWindowRenderHelper
{
	friend struct CBrowserWindowTexture;

public:

	CBrowserWindowRenderHelper();
	CBrowserWindowRenderHelper(const CBrowserWindowRenderHelper& Other);
	CBrowserWindowRenderHelper& operator=(const CBrowserWindowRenderHelper& Other);

	virtual ~CBrowserWindowRenderHelper();

protected:
	
	bool OpenSharedTextureDX11(void* SharedHandle, CBrowserWindowSharedTexture& SharedTexture);
	bool OpenSharedTextureDX12(void* SharedHandle, CBrowserWindowSharedTexture& SharedTexture);
	
	bool OpenSharedTextureGroupDX11(void* SharedHandle, CBrowserWindowSharedTexture& SharedTexture, CBrowserWindowTexture& TextureGroup, bool& bResize);
	bool OpenSharedTextureGroupDX12(void* SharedHandle, CBrowserWindowSharedTexture& SharedTexture, CBrowserWindowTexture& TextureGroup, bool& bResize);

	bool CopySharedTextureGroupDX11(CBrowserWindowTexture& DstTextureGroup, const CBrowserWindowSharedTexture& SrcTexture, INT SyncTime = -1, bool bRegion = false, UINT xMin = 0, UINT yMin = 0, UINT xMax = 0, UINT yMax = 0);
	bool CopySharedTextureGroupDX12(CBrowserWindowTexture& DstTextureGroup, const CBrowserWindowSharedTexture& SrcTexture, INT SyncTime = -1, bool bRegion = false, UINT xMin = 0, UINT yMin = 0, UINT xMax = 0, UINT yMax = 0);

public:

	virtual bool HasDevice() const;
	virtual bool CreateDevice();

	virtual void* GetNativeDevice() const;
	virtual void* GetNativeCommandQueue() const;

private:

	CBrowserWindowRenderHelperImpl* pImpl;
};

TRACERWEBACCELERATEDPAINT_API bool IsTracerAcceleratedPaintSupported();
TRACERWEBACCELERATEDPAINT_API void SetTracerAcceleratedPaintLicenseKey(const char* Key, const char* Project);
