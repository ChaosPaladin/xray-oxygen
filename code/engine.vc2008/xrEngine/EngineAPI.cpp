// EngineAPI.cpp: implementation of the CEngineAPI class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "EngineAPI.h"
#include "../xrcdb/xrXRC.h"
#include "XR_IOConsole.h"

extern xr_vector<xr_token> vid_quality_token;

constexpr const char* r1_name = "xrRender_R1";
constexpr const char* r2_name = "xrRender_R2";
constexpr const char* r3_name = "xrRender_R3";
constexpr const char* r4_name = "xrRender_R4";

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CEngineAPI::CEngineAPI	()
{
	hGame			= 0;
	hRender			= 0;
	pCreate			= 0;
	pDestroy		= 0;
}

CEngineAPI::~CEngineAPI()
{
	vid_quality_token.clear();
}

extern u32 renderer_value; //con cmd
ENGINE_API int g_current_renderer = 0;

ENGINE_API bool is_enough_address_space_available	()
{
	SYSTEM_INFO		system_info;
	GetSystemInfo	( &system_info );
	return			(*(u32*)&system_info.lpMaximumApplicationAddress) > 0x90000000;	
}

void CEngineAPI::InitializeRenderer()
{
	// If we failed to load render,
	// then try to fallback to lower one.

	if (psDeviceFlags.test(rsR4))
	{
		// try to initialize R4
		Log				("Loading DLL:",	r4_name);
		hRender			= LoadLibrary		(r4_name);
		if (0==hRender)	
		{
			// try to load R1
			Msg			("! ...Failed - incompatible hardware/pre-Vista OS.");
			psDeviceFlags.set	(rsR3, true);
		}
		else
			g_current_renderer = 4;
	}

	if (psDeviceFlags.test(rsR3))
	{
		// try to initialize R3
		Log				("Loading DLL:",	r3_name);
		hRender			= LoadLibrary		(r3_name);
		if (0==hRender)	
		{
			// try to load R1
			Msg			("! ...Failed - incompatible hardware/pre-Vista OS.");
			psDeviceFlags.set	(rsR2, true);
		}
		else
			g_current_renderer	= 3;
	}

	if (psDeviceFlags.test(rsR2))	
	{
		// try to initialize R2
		Log				("Loading DLL:",	r2_name);
		hRender			= LoadLibrary		(r2_name);
		if (0==hRender)	
		{
			// try to load R1
			Msg			("! ...Failed - incompatible hardware.");
			psDeviceFlags.set	(rsR1, true);
		}
		else
			g_current_renderer	= 2;
	}

	if (psDeviceFlags.test(rsR1))
	{
		// try to load R1
		renderer_value = 0; //con cmd

		Log("Loading DLL:", r1_name);
		hRender = LoadLibrary(r1_name);
		if (0 == hRender)
		{
			// try to load R1
			Msg("! ...Failed - incompatible hardware.");
		}
		else
			g_current_renderer = 1;
	}
}
#include <thread>

void CEngineAPI::Initialize(void)
{
	//////////////////////////////////////////////////////////////////////////
	// render
	InitializeRenderer();

	if (0 == hRender && vid_quality_token[0].id != -1)
	{
		// if engine failed to load renderer
		// but there is at least one available
		// then try again
		string32 buf;
		xr_sprintf(buf, "renderer %s", vid_quality_token[0].name);
		Console->Execute(buf);

		// Second attempt
		InitializeRenderer();
	}

	if (0 == hRender)
		R_CHK(GetLastError());

	R_ASSERT(hRender, "Can't load renderer");

	Device.ConnectToRender();

	// game	
	{
		LPCSTR			g_name = "xrGame";
		if (strstr(Core.Params, "-debug_game"))
		{
			g_name = "xrGame_debug";
		}
		Log				("Loading DLL:",g_name);
		hGame			= LoadLibrary	(g_name);
		if (!hGame)	R_CHK(GetLastError());
		R_ASSERT3		(hGame,"Game DLL raised exception during loading or there is no game DLL at all", g_name);
		pCreate			= (Factory_Create*)GetProcAddress(hGame,"xrFactory_Create");	R_ASSERT(pCreate);
		pDestroy		= (Factory_Destroy*)GetProcAddress(hGame,"xrFactory_Destroy");	R_ASSERT(pDestroy);
	}
}

void CEngineAPI::Destroy	(void)
{
	if (hGame)				{ FreeLibrary(hGame);	hGame	= nullptr; }
	if (hRender)			{ FreeLibrary(hRender); hRender = nullptr; }
	pCreate					= 0;
	pDestroy				= 0;
	Engine.Event._destroy	();
	XRC.r_clear_compact		();
}

extern "C" {
	typedef bool __cdecl SupportsAdvancedRendering	(void);
	typedef bool _declspec(dllexport) SupportsDX10Rendering();
	typedef bool _declspec(dllexport) SupportsDX11Rendering();
};

void CEngineAPI::CreateRendererList()
{
	if (!vid_quality_token.empty())
		return;

	xr_vector<xr_token> modes;

	// try to initialize R1
	Log("Loading DLL:", r1_name);
	hRender = LoadLibrary(r1_name);
	if (hRender)
	{
		modes.emplace_back(xr_token("renderer_r1", 1));
		FreeLibrary(hRender);
	}

	// try to initialize R2
	Log				("Loading DLL:",	r2_name);
	hRender			= LoadLibrary		(r2_name);
	if (hRender)	
	{
		modes.emplace_back(xr_token("renderer_r2", 2));
		SupportsAdvancedRendering *test_rendering = (SupportsAdvancedRendering*) GetProcAddress(hRender,"SupportsAdvancedRendering");	
		if (test_rendering && test_rendering())
			modes.emplace_back(xr_token("renderer_r2.5", 3));
		FreeLibrary(hRender);
	}

	// try to initialize R3
	Log				("Loading DLL:",	r3_name);
	//	Hide "d3d10 not found" message box for XP
	SetErrorMode(SEM_FAILCRITICALERRORS);
	hRender			= LoadLibrary		(r3_name);
	//	Restore error handling
	SetErrorMode(0);
	if (hRender)	
	{
		SupportsDX10Rendering *test_dx10_rendering = (SupportsDX10Rendering*) GetProcAddress(hRender,"SupportsDX10Rendering");
		if (test_dx10_rendering && test_dx10_rendering())
			modes.emplace_back(xr_token("renderer_r3", 4));
		FreeLibrary(hRender);
	}

	// try to initialize R4
	Log				("Loading DLL:",	r4_name);
	//	Hide "d3d10 not found" message box for XP
	SetErrorMode	(SEM_FAILCRITICALERRORS);
	hRender			= LoadLibrary		(r4_name);
	//	Restore error handling
	SetErrorMode	(0);
	if (hRender)	
	{
		SupportsDX11Rendering *test_dx11_rendering = (SupportsDX11Rendering*) GetProcAddress(hRender,"SupportsDX11Rendering");
		if (test_dx11_rendering && test_dx11_rendering())
			modes.emplace_back(xr_token("renderer_r4", 5));
		FreeLibrary(hRender);
	}

	modes.emplace_back(xr_token(nullptr, -1));

	hRender = nullptr;

	Msg("Available render modes[%d]:", modes.size());
	for (auto& mode : modes)
		if (mode.name)
			Log(mode.name);

	vid_quality_token = std::move(modes);
}
