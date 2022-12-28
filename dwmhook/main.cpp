#include "includes.hpp"
#include "render.hpp"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"

ID3D11Device* pD3DXDevice = nullptr;
ID3D11DeviceContext* pD3DXDeviceCtx = nullptr;
ID3D11Texture2D* pBackBuffer = nullptr;
ID3D11RenderTargetView* pRenderTarget = nullptr;

IFW1Factory* pFontFactory = nullptr;
IFW1FontWrapper* pFontWrapper = nullptr;

BOOL bDataCompare(const BYTE* pData, const BYTE* bMask, const char* szMask)
{
	for (; *szMask; ++szMask, ++pData, ++bMask)
	{
		if (*szMask == 'x' && *pData != *bMask)
			return FALSE;
	}
	return (*szMask) == NULL;
}

DWORD64 FindPattern(const char* szModule, BYTE* bMask, const char* szMask)
{
	MODULEINFO mi{ };
	GetModuleInformation(GetCurrentProcess(), GetModuleHandleA(szModule), &mi, sizeof(mi));

	DWORD64 dwBaseAddress = DWORD64(mi.lpBaseOfDll);
	const auto dwModuleSize = mi.SizeOfImage;

	for (auto i = 0ul; i < dwModuleSize; i++)
	{
		if (bDataCompare(PBYTE(dwBaseAddress + i), bMask, szMask))
			return DWORD64(dwBaseAddress + i);
	}
	return NULL;
}

void AddToLog(const char* fmt, ...)
{

	//va_list va;
	//va_start( va, fmt );

	//char buff[ 1024 ]{ };
	//vsnprintf_s( buff, sizeof( buff ), fmt, va );

	//va_end( va );

	//FILE* f = nullptr;
	//fopen_s( &f, LOG_FILE_PATH, "a" );

	//if ( !f )
	//{
	//	char szDst[ 256 ];
	//	sprintf_s( szDst, "Failed to create file %d", GetLastError() );
	//	MessageBoxA( 0, szDst, 0, 0 );
	//	return;
	//}

	//OutputDebugStringA( buff );
	//fprintf_s( f, buff );
	//printf( buff );
	//fclose( f );

}

using Fn_Present = __int64(__fastcall*)(void* thisptr, IDXGISwapChain* a2, __int64 a3, char a4);

Fn_Present Original_Present = NULL;

static std::once_flag IsInitialized;


static ID3D11Device* g_pd3dDevice = NULL;
static ID3D11DeviceContext* g_pd3dDeviceContext = NULL;
static IDXGISwapChain* g_pSwapChain = NULL;
static ID3D11RenderTargetView* g_mainRenderTargetView = NULL;

bool UIIsCurrentlyOpen = true;
typedef LRESULT(CALLBACK* OWindowProc) (
	_In_ HWND   hwnd,
	_In_ UINT   uMsg,
	_In_ WPARAM wParam,
	_In_ LPARAM lParam
	);
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);


OWindowProc WNDProc = nullptr;


static LRESULT WINAPI HWNDProcHandle(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam) {
	ImGuiIO& io = ImGui::GetIO();
	switch (Msg) {
	case WM_LBUTTONDOWN:
		io.MouseDown[0] = true;
		return true;
	case WM_LBUTTONUP:
		io.MouseDown[0] = false;
		return true;
	case WM_RBUTTONDOWN:
		io.MouseDown[1] = true;
		return true;
	case WM_RBUTTONUP:
		io.MouseDown[1] = false;
		return true;
	case WM_MBUTTONDOWN:
		io.MouseDown[2] = true;
		return true;
	case WM_MBUTTONUP:
		io.MouseDown[2] = false;
		return true;
	case WM_XBUTTONDOWN:
		if ((GET_KEYSTATE_WPARAM(wParam) & MK_XBUTTON1) == MK_XBUTTON1)
			io.MouseDown[3] = true;
		else if ((GET_KEYSTATE_WPARAM(wParam) & MK_XBUTTON2) == MK_XBUTTON2)
			io.MouseDown[4] = true;
		return true;
	case WM_XBUTTONUP:
		if ((GET_KEYSTATE_WPARAM(wParam) & MK_XBUTTON1) == MK_XBUTTON1)
			io.MouseDown[3] = false;
		else if ((GET_KEYSTATE_WPARAM(wParam) & MK_XBUTTON2) == MK_XBUTTON2)
			io.MouseDown[4] = false;
		return true;
	case WM_MOUSEWHEEL:
		io.MouseWheel += GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? +1.0f : -1.0f;
		return true;
	case WM_MOUSEMOVE:
		io.MousePos.x = (signed short)(lParam);
		io.MousePos.y = (signed short)(lParam >> 16);
		return true;
	case WM_KEYDOWN:
		if (wParam < 256)
			io.KeysDown[wParam] = 1;
		return true;
	case WM_KEYUP:
		if (wParam < 256)
			io.KeysDown[wParam] = 0;
		return true;
	case WM_CHAR:
		if (wParam > 0 && wParam < 0x10000)
			io.AddInputCharacter((unsigned short)wParam);
		return true;
	}
	return 0;
}

void ImGuiInit()
{
	if (SUCCEEDED(g_pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&g_pd3dDevice))) {
		g_pd3dDevice->GetImmediateContext(&g_pd3dDeviceContext);
	}
	ID3D11Texture2D* RenderTargetTexture = nullptr;
	if (SUCCEEDED(g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&RenderTargetTexture)))) {
		g_pd3dDevice->CreateRenderTargetView(RenderTargetTexture, NULL, &g_mainRenderTargetView);
		if (!g_mainRenderTargetView)
			return;
		RenderTargetTexture->Release();
	}
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	HWND hwnd = FindWindowA("Progman", "Program Manager");
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
	WNDProc = (OWindowProc)SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)HWNDProcHandle);

	ImGui::StyleColorsLight();

}

bool checkbox = true;

void DrawEverything(IDXGISwapChain* pDxgiSwapChain)
{
	static bool b = true;
	if (b)
	{
		pDxgiSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&pD3DXDevice);
		pDxgiSwapChain->AddRef();
		pD3DXDevice->GetImmediateContext(&pD3DXDeviceCtx);

		pDxgiSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&backbuffer_ptr);
		pD3DXDevice->CreateRenderTargetView(*backbuffer_ptr, NULL, &rtview_ptr);

		D3D11_RASTERIZER_DESC raster_desc;
		ZeroMemory(&raster_desc, sizeof(raster_desc));
		raster_desc.FillMode = D3D11_FILL_SOLID; // D3D11_FILL_WIREFRAME;
		raster_desc.CullMode = D3D11_CULL_BACK; //D3D11_CULL_NONE;
		pD3DXDevice->CreateRasterizerState(&raster_desc, &rasterizer_state_ov);

		// shader

		D3D_SHADER_MACRO shader_macro[] = { NULL, NULL };
		ID3DBlob* vs_blob_ptr = NULL, * ps_blob_ptr = NULL, * error_blob = NULL;

		D3DCompile(shader_code, strlen(shader_code), NULL, shader_macro, NULL, "VS", "vs_4_0", 0, 0, &vs_blob_ptr, &error_blob);
		D3DCompile(shader_code, strlen(shader_code), NULL, shader_macro, NULL, "PS", "ps_4_0", 0, 0, &ps_blob_ptr, &error_blob);

		pD3DXDevice->CreateVertexShader(vs_blob_ptr->GetBufferPointer(), vs_blob_ptr->GetBufferSize(), NULL, &vertex_shader_ptr);
		pD3DXDevice->CreatePixelShader(ps_blob_ptr->GetBufferPointer(), ps_blob_ptr->GetBufferSize(), NULL, &pixel_shader_ptr);

		// layout

		D3D11_INPUT_ELEMENT_DESC element_desc[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};

		pD3DXDevice->CreateInputLayout(element_desc, ARRAYSIZE(element_desc), vs_blob_ptr->GetBufferPointer(), vs_blob_ptr->GetBufferSize(), &input_layout_ptr);

		// buffers

		SimpleVertex vertices[] =
		{
			{ XMFLOAT3(-1.0f, 1.0f, -1.0f), XMFLOAT4(0.0f, 0.0f, 1.0f, 1.0f) },
			{ XMFLOAT3(1.0f, 1.0f, -1.0f), XMFLOAT4(0.0f, 1.0f, 0.0f, 1.0f) },
			{ XMFLOAT3(1.0f, 1.0f, 1.0f), XMFLOAT4(0.0f, 1.0f, 1.0f, 1.0f) },
			{ XMFLOAT3(-1.0f, 1.0f, 1.0f), XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f) },
			{ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT4(1.0f, 0.0f, 1.0f, 1.0f) },
			{ XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT4(1.0f, 1.0f, 0.0f, 1.0f) },
			{ XMFLOAT3(1.0f, -1.0f, 1.0f), XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f) },
			{ XMFLOAT3(-1.0f, -1.0f, 1.0f), XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f) },
		};

		WORD indices[] =
		{
			3,1,0,
			2,1,3,

			0,5,4,
			1,5,0,

			3,4,7,
			0,4,3,

			1,6,5,
			2,6,1,

			2,7,6,
			3,7,2,

			6,4,5,
			7,4,6,
		};

		D3D11_BUFFER_DESC bd = {};
		D3D11_SUBRESOURCE_DATA data = {};

		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.ByteWidth = sizeof(SimpleVertex) * 8;
		bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		bd.CPUAccessFlags = 0;
		data.pSysMem = vertices;
		pD3DXDevice->CreateBuffer(&bd, &data, &vertex_buffer_ptr);

		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.ByteWidth = sizeof(WORD) * 36;
		bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
		bd.CPUAccessFlags = 0;
		data.pSysMem = indices;
		pD3DXDevice->CreateBuffer(&bd, &data, &index_buffer_ptr);

		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.ByteWidth = sizeof(ConstantBuffer);
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = 0;
		pD3DXDevice->CreateBuffer(&bd, NULL, &const_buffer_ptr);

		t0 = GetTickCount64();

		FW1CreateFactory(FW1_VERSION, &pFontFactory);
		pFontFactory->CreateFontWrapper(pD3DXDevice, L"Arial", &pFontWrapper);
		SAFE_RELEASE(pFontFactory);

		render.Initialize(pFontWrapper);

		b = false;
	}
	else
	{
		fix_renderstate();

		render.BeginScene();
		render.RenderText(L"we are obama gaming.", 10.f, 50.f, -1, false, true);

		g_pSwapChain = pDxgiSwapChain;

		std::call_once(IsInitialized, [] {ImGuiInit(); });
		ImGuiIO& io = ImGui::GetIO();
		io.MouseDown[0] = GetAsyncKeyState(VK_LBUTTON) & 0x8000 ? true : false;
		io.MouseDown[1] = GetAsyncKeyState(VK_RBUTTON) & 0x8000 ? true : false;
		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();
		WNDProc = (OWindowProc)SetWindowLongPtr(GetForegroundWindow(), GWLP_WNDPROC, (LONG_PTR)HWNDProcHandle);
		ImGui::Begin("Hello, world!");
		ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
		ImGui::Checkbox("CheckBox", &checkbox);
		if (ImGui::Button("Button"))
			MessageBoxA(0, "1", "1", 0);
		ImGui::End();
		ImGui::GetForegroundDrawList()->AddText(ImVec2(10, 10), IM_COL32_WHITE, " Hello world !");

		g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
		ImGui::Render();
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		render.EndScene();
	}
}

using PresentMPO_ = __int64(__fastcall*)(void*, IDXGISwapChain*, unsigned int, unsigned int, int, __int64, __int64, int);
PresentMPO_ oPresentMPO = NULL;

__int64 __fastcall hkPresentMPO(void* thisptr, IDXGISwapChain* pDxgiSwapChain, unsigned int a3, unsigned int a4, int a5, __int64 a6, __int64 a7, int a8)
{
	DrawEverything(pDxgiSwapChain);
	return oPresentMPO(thisptr, pDxgiSwapChain, a3, a4, a5, a6, a7, a8);
}

using PresentDWM_ = __int64(__fastcall*)(void*, IDXGISwapChain*, unsigned int, unsigned int, const struct tagRECT*, unsigned int, const struct DXGI_SCROLL_RECT*, unsigned int, struct IDXGIResource*, unsigned int);
PresentDWM_ oPresentDWM = NULL;

__int64 __fastcall hkPresentDWM(void* thisptr, IDXGISwapChain* pDxgiSwapChain, unsigned int a3, unsigned int a4, const struct tagRECT* a5, unsigned int a6, const struct DXGI_SCROLL_RECT* a7, unsigned int a8, struct IDXGIResource* a9, unsigned int a10)
{
	DrawEverything(pDxgiSwapChain);
	return oPresentDWM(thisptr, pDxgiSwapChain, a3, a4, a5, a6, a7, a8, a9, a10);
}

typedef __int64(_fastcall* CHwFullScreenRenderTarget_Present_Proto)(class CHwFullScreenRenderTarget* pThis, __int64 a2, char a3, const struct RenderTargetPresentParameters* a4);
CHwFullScreenRenderTarget_Present_Proto oCHwFullScreenRenderTarget_Present = NULL;

static UINT32 CHwFullScreenRenderTarget_SwapChainBase_Offset = 0;
static UINT32 _CDWMSwapChain_DxgiSwapChain_Offset = 0;
static PVOID CHwFullScreenRenderTarget_Present_Loc = 0;

static bool Find_CHwFullScreenRenderTarget_Present()
{
	// TODO: fix this idiotism in a smart way :D
	auto Pattern = CkParseByteArray(
		"48 89 5C 24 10" // mov     [rsp-38h+arg_8], rbx
		"4C 89 4C 24 20" // mov     [rsp-38h+arg_18], r9
		"55" // push    rbp
		"56" // push    rsi
		"57" // push    rdi
		"41 54" // push    r12
		"41 55" // push    r13
		"41 56" // push    r14
		"41 57" // push    r15
		"48 8B EC" // mov     rbp, rsp
		"48 83 EC 50" // sub     rsp, 50h
		"48 8D B9 70 FF FF FF" // lea     rdi, [rcx-90h]
		"45 8A F0" // mov     r14b, r8b
		"48 8B 07" // mov     rax, [rdi]
		"4C 8D 4D EC" // lea     r9, [rbp+var_14]
		"48 8B F1" // mov     rsi, rcx
		"4C 8D 45 E0" // lea     r8, [rbp+var_20]
		"45 33 E4" // xor     r12d, r12d
		"48 8B CF" // mov     rcx, rdi
		"8A DA" // mov     bl, dl
		"45 8B EC" // mov     r13d, r12d
		"48 8B 80 10 01 00 00" // mov     rax, [rax+110h]
		"FF 15 CC CC CC CC" // call    cs:__guard_dispatch_icall_fptr
		"85 C0" // test    eax, eax
		"78 33" // js      short loc_180045DD3
		"44 38 65 E0" // cmp     [rbp+var_20], r12b
		"74 2D" // jz      short loc_180045DD3
		"F6 45 EC 02" // test    [rbp+var_14], 2
		"75 27" // jnz     short loc_180045DD3
		"44 38 A6 63 01 00 00" // cmp     [rsi+163h], r12b
		"75 1E" // jnz     short loc_180045DD3
		"48 8B 06" // mov     rax, [rsi]
		"48 8B CE" // mov     rcx, rsi
		"48 8B 80 F8 00 00 00" // mov     rax, [rax+0F8h]
		"FF 15 CC CC CC CC" // call    cs:__guard_dispatch_icall_fptr
		"44 8B E8" // mov     r13d, eax
		"85 C0" // test    eax, eax
		"0F 88 CC CC CC CC" // js      loc_18010731E
		"48 8B 0F" // mov     rcx, [rdi]
		"4C 8D 4D E4" // lea     r9, [rbp+var_1C]
		"4C 8D 45 40" // lea     r8, [rbp+arg_0]
		"44 88 65 40" // mov     [rbp+arg_0], r12b
		"8A D3" // mov     dl, bl
		"44 89 65 E4" // mov     [rbp+var_1C], r12d
		"48 8B 81 10 01 00 00" // mov     rax, [rcx+110h]
		"48 8B CF" // mov     rcx, rdi
		"FF 15 CC CC CC CC" // call    cs:__guard_dispatch_icall_fptr
		"8B D8" // mov     ebx, eax
		"41 BF 01 00 00 00" // mov     r15d, 1
		"85 C0" // test    eax, eax
		"0F 88 CC CC CC CC" // js      loc_180107630
		"8B 45 E4" // mov     eax, [rbp+var_1C]
		"45 84 F6" // test    r14b, r14b
		"0F 85 CC CC CC CC" // jnz     loc_18010733E
		"89 45 E8" // mov     [rbp+var_18], eax
		"44 38 65 40" // cmp     [rbp+arg_0], r12b
		"0F 84 CC CC CC CC" // jz      loc_180045FB1
		"8B 46 78" // mov     eax, [rsi+78h]
		"48 8D BE 80 00 00 00" // lea     rdi, [rsi+80h]
		"4C 8B 76 08" // mov     r14, [rsi+8]
		"4C 8B 66 20" // mov     r12, [rsi+20h] // ### struct CSwapChainBase* v20 = (_DWORD *)*((_QWORD *)pThis + 4)
		"89 45 F0" // mov     [rbp+var_10], eax
		"33 C0" // xor     eax, eax
		"41 8B 9E 70 03 00 00" // mov     ebx, [r14+370h]
		"85 DB" // test    ebx, ebx
		"0F 88 CC CC CC CC" // js      loc_18010740D
		"38 05 CC CC CC CC" // cmp     cs:?g_fForceDeviceLost@@3_NA, al ; bool g_fForceDeviceLost
		"0F 85 CC CC CC CC" // jnz     loc_18010734A
	);

	auto Loc = CkFindPatternIntern<CkWildcardCC>(Pattern, 0);
	CK_GUARD_RET(Loc.size() == 1, false);

	CK_TRACE_INFO("FOUND: CHwFullScreenRenderTarget_Present=%p", Loc[0]);
	CHwFullScreenRenderTarget_Present_Loc = Loc[0];

	auto Offset = *(UINT8*)(Loc[0] + 223 + 3);
	CK_TRACE_INFO("FOUND: CHwFullScreenRenderTarget_SwapChainBase_Offset=0x%X", (UINT)Offset);
	CK_GUARD_RET(Offset, false);
	CHwFullScreenRenderTarget_SwapChainBase_Offset = Offset;

	return true;
}
static bool Find_SwapChain_Offset()
{
	/*
	__int64 __fastcall CDWMSwapChain::GetFrameStatisticsInternal(CDWMSwapChain *this, struct DXGI_FRAME_STATISTICS_DWM *a2)
	{
	  v2 = (*(__int64 (__fastcall **)(_QWORD, struct DXGI_FRAME_STATISTICS_DWM *))(**((_QWORD **)this + 53) + 160i64))(
			 *((_QWORD *)this + 53),
			 a2);
	}
	*/
	auto Pattern = CkParseByteArray(
		"40 53" // push    rbx
		"48 83 EC 30" // sub     rsp, 30h
		"48 8B 89 A8 01 00 00" // mov     rcx, [rcx+1A8h] // <-- OFFSET: ((_QWORD *)this + 53) = 53*8 = 1A8
		"48 8B 01" // mov     rax, [rcx]
		"48 8B 80 A0 00 00 00" // mov     rax, [rax+0A0h]
		"FF 15 CC CC CC CC" // call    cs:__guard_dispatch_icall_fptr
		"89 44 24 40" // mov     [rsp+38h+arg_0], eax
		"8B D8" // mov     ebx, eax
		"85 C0" // test    eax, eax
		"0F 88 CC CC CC CC" // js      loc_1800FC382
		"4C 8D 44 24 40" // lea     r8, [rsp+38h+arg_0]
		"33 D2" // xor     edx, edx
		"8B CB" // mov     ecx, ebx
		"E8 CC CC CC CC" // call    ?TranslateDXGIorD3DErrorInContext@@YA_NJW4Enum@DXGIFunctionContext@@PEAJ@Z ; TranslateDXGIorD3DErrorInContext(long,DXGIFunctionContext::Enum,long *)
		"8B 44 24 40" // mov     eax, [rsp+38h+arg_0]
		"48 83 C4 30" // add     rsp, 30h
		"5B" // pop     rbx
		"C3" // retn
	);

	auto Loc = CkFindPatternIntern<CkWildcardCC>(Pattern, 0);
	CK_GUARD_RET(Loc.size() == 1, false);

	CK_TRACE_INFO("FOUND: CDWMSwapChain_GetFrameStatisticsInternal=%p", Loc[0]);

	auto Offset = *(UINT32*)(Loc[0] + 9);
	CK_TRACE_INFO("FOUND: SwapChainOffset=0x%X", (UINT)Offset);
	CK_GUARD_RET(Offset, false);
	_CDWMSwapChain_DxgiSwapChain_Offset = Offset;

	return true;
}


static __int64 __fastcall CHwFullScreenRenderTarget_Present_Hook(class CHwFullScreenRenderTarget* pThis, __int64 a2, char a3, const struct RenderTargetPresentParameters* a4)
{


	LPVOID pSwapChainBase = *(LPVOID*)((UINT8*)pThis + CHwFullScreenRenderTarget_SwapChainBase_Offset);
	CK_TRACE_INFO("pSwapChainBase=%p", pSwapChainBase);
	if (pSwapChainBase)
	{
		IDXGISwapChain* pDxgiSwapChain = *(IDXGISwapChain**)((UINT8*)pSwapChainBase + _CDWMSwapChain_DxgiSwapChain_Offset);
		CK_TRACE_INFO("pDxgiSwapChain=%p", pDxgiSwapChain);
		if (pDxgiSwapChain)
		{
			DrawEverything(pDxgiSwapChain);
		}
	}

	return oCHwFullScreenRenderTarget_Present(pThis, a2, a3, a4);
}


UINT WINAPI MainThread(PVOID)
{
	freopen("CONOUT$", "w", stdout);
	MH_Initialize();

	while (!GetModuleHandleA("dwmcore.dll"))
		Sleep(150);

	//
	// [ E8 ? ? ? ? ] the relative addr will be converted to absolute addr
	auto ResolveCall = [](DWORD_PTR sig)
	{
		return sig = sig + *reinterpret_cast<PULONG>(sig + 1) + 5;
	};

	//
	// [ 48 8D 05 ? ? ? ? ] the relative addr will be converted to absolute addr
	auto ResolveRelative = [](DWORD_PTR sig)
	{
		return sig = sig + *reinterpret_cast<PULONG>(sig + 0x3) + 0x7;
	};

	//lea     rax, ??_7DrawingContext@@6BIMILRefCount@@@ ; const DrawingContext::`vftable'{for `IMILRefCount'}
	//xor     ebp, ebp
	//lea     rsi, [rcx+8]
	//mov     [rcx], rax

	auto dwRender = FindPattern("d2d1.dll", PBYTE("\x48\x8D\x05\x00\x00\x00\x00\x33\xED\x48\x8D\x71\x08"), "xxx????xxxxxx");
	printf("dwRender:%p\r\n", dwRender);
	if (dwRender)
	{
		dwRender = ResolveRelative(dwRender);
		printf("dwRender2:%p\r\n", dwRender);
		PDWORD_PTR Vtbl = PDWORD_PTR(dwRender);

		AddToLog("table 0x%llx\n", dwRender);

		printf("dwRender5:%p\r\n", (Vtbl[6]));
		printf("dwRender6:%p\r\n", (Vtbl[7]));


		//查一下偏移

		Find_SwapChain_Offset();
		Find_CHwFullScreenRenderTarget_Present();

		//改下这里的
		MH_CreateHook(PVOID(CHwFullScreenRenderTarget_Present_Loc), PVOID(&CHwFullScreenRenderTarget_Present_Hook), reinterpret_cast<PVOID*>(&oCHwFullScreenRenderTarget_Present));
		MH_CreateHook(PVOID(Vtbl[6]), PVOID(&hkPresentDWM), reinterpret_cast<PVOID*>(&oPresentDWM));
		MH_CreateHook(PVOID(Vtbl[7]), PVOID(&hkPresentMPO), reinterpret_cast<PVOID*>(&oPresentMPO));
		MH_EnableHook(MH_ALL_HOOKS);

		AddToLog("hooked!\n");
	}

	return 0;
}



BOOL WINAPI DllMain(HMODULE hDll, DWORD dwReason, PVOID)
{
	if (dwReason == DLL_PROCESS_ATTACH)
	{
		AllocConsole();
		DeleteFileA(LOG_FILE_PATH);
		_beginthreadex(nullptr, NULL, MainThread, nullptr, NULL, nullptr);
	}
	return true;
}
