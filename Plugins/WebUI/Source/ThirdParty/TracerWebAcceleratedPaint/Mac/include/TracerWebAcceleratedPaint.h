// Copyright 2025 Tracer Interactive, LLC. All Rights Reserved.
#pragma once

#if defined(TRACERWEBACCELERATEDPAINT_EXPORTS)
	#define TRACERWEBACCELERATEDPAINT_API __attribute__((visibility("default")))
#else
	#define TRACERWEBACCELERATEDPAINT_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct CBrowserWindowSharedTextureImpl;
struct TRACERWEBACCELERATEDPAINT_API CBrowserWindowSharedTexture
{
	friend class CBrowserWindowRenderHelper;

	CBrowserWindowSharedTexture();
	CBrowserWindowSharedTexture(const CBrowserWindowSharedTexture& Other);
	CBrowserWindowSharedTexture& operator=(const CBrowserWindowSharedTexture& Other);

	~CBrowserWindowSharedTexture();

	unsigned int GetWidth() const;
	unsigned int GetHeight() const;

private:

	CBrowserWindowSharedTextureImpl* pImpl;
};

struct TRACERWEBACCELERATEDPAINT_API CBrowserWindowTexture
{
	friend class CBrowserWindowRenderHelper;

	virtual bool IsValid() const;
	virtual void Resize(unsigned int Width, unsigned int Height);

	virtual void* GetNativeResource() const;

	virtual unsigned int GetNativeResourceWidth() const;
	virtual unsigned int GetNativeResourceHeight() const;
};

class TRACERWEBACCELERATEDPAINT_API CBrowserWindowRenderHelper
{
	friend struct CBrowserWindowTexture;

public:

	CBrowserWindowRenderHelper();
	CBrowserWindowRenderHelper(const CBrowserWindowRenderHelper& Other);

protected:

	bool OpenSharedTextureMetal(void* SharedHandle, CBrowserWindowSharedTexture& SharedTexture);
	bool OpenSharedTextureGroupMetal(void* SharedHandle, CBrowserWindowSharedTexture& SharedTexture, CBrowserWindowTexture& TextureGroup, bool& bResize);
	bool CopySharedTextureGroupMetal(CBrowserWindowTexture& DstTextureGroup, const CBrowserWindowSharedTexture& SrcTexture, bool bRegion = false, unsigned int xMin = 0, unsigned int yMin = 0, unsigned int xMax = 0, unsigned int yMax = 0);

public:

	virtual void* GetNativeDevice() const;
	virtual void* GetNativeCommandQueue() const;
};

TRACERWEBACCELERATEDPAINT_API bool IsTracerAcceleratedPaintSupported();
TRACERWEBACCELERATEDPAINT_API void SetTracerAcceleratedPaintLicenseKey(const char* Key, const char* Project);

#ifdef __cplusplus
}
#endif
