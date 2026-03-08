/*
* (C) 2026 badasahog. All Rights Reserved
*
* The above copyright notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#undef _CRT_SECURE_NO_WARNINGS
#include <shellscalingapi.h>

#include <dxgi1_6.h>
#include <d3d12.h>

#ifdef _DEBUG
#include <dxgidebug.h>
#endif

#include <cglm/cglm.h>

#include <math.h>
#include <stdio.h>
#include <stdalign.h>
#include <stdbool.h>

#pragma comment(linker, "/DEFAULTLIB:D3d12.lib")
#pragma comment(linker, "/DEFAULTLIB:Shcore.lib")
#pragma comment(linker, "/DEFAULTLIB:DXGI.lib")
#pragma comment(linker, "/DEFAULTLIB:dxguid.lib")

__declspec(dllexport) DWORD NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
__declspec(dllexport) UINT D3D12SDKVersion = 612;
__declspec(dllexport) char* D3D12SDKPath = ".\\D3D12\\";

HANDLE ConsoleHandle;
ID3D12Device10* Device;

inline void THROW_ON_FAIL_IMPL(HRESULT hr, int line)
{
	if (hr == 0x887A0005)//device removed
	{
		THROW_ON_FAIL_IMPL(ID3D12Device10_GetDeviceRemovedReason(Device), line);
	}

	if (FAILED(hr))
	{
		LPWSTR messageBuffer;
		DWORD formattedErrorLength = FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			hr,
			MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
			(LPWSTR)&messageBuffer,
			0,
			NULL
		);

		if (formattedErrorLength == 0)
			WriteConsoleA(ConsoleHandle, "an error occured, unable to retrieve error message\n", 51, NULL, NULL);
		else
		{
			WriteConsoleA(ConsoleHandle, "an error occured: ", 18, NULL, NULL);
			WriteConsoleW(ConsoleHandle, messageBuffer, formattedErrorLength, NULL, NULL);
			WriteConsoleA(ConsoleHandle, "\n", 1, NULL, NULL);
			LocalFree(messageBuffer);
		}

		char buffer[50];
		int stringlength = _snprintf_s(buffer, 50, _TRUNCATE, "error code: 0x%X\nlocation:line %i\n", hr, line);
		WriteConsoleA(ConsoleHandle, buffer, stringlength, NULL, NULL);

		RaiseException(0, EXCEPTION_NONCONTINUABLE, 0, NULL);
	}
}

#define THROW_ON_FAIL(x) THROW_ON_FAIL_IMPL(x, __LINE__)

#define THROW_ON_FALSE(x) if((x) == FALSE) THROW_ON_FAIL(HRESULT_FROM_WIN32(GetLastError()))

#define VALIDATE_HANDLE(x) if((x) == NULL || (x) == INVALID_HANDLE_VALUE) THROW_ON_FAIL(HRESULT_FROM_WIN32(GetLastError()))

inline void MEMCPY_VERIFY_IMPL(errno_t error, int line)
{
	if (error != 0)
	{
		char buffer[28];
		int stringlength = _snprintf_s(buffer, 28, _TRUNCATE, "memcpy failed on line %i\n", line);
		WriteConsoleA(ConsoleHandle, buffer, stringlength, NULL, NULL);
		RaiseException(0, EXCEPTION_NONCONTINUABLE, 0, NULL);
	}
}

#define MEMCPY_VERIFY(x) MEMCPY_VERIFY_IMPL(x, __LINE__)

#define OffsetPointer(x, offset) ((typeof(x))((char*)x + (offset)))

LRESULT CALLBACK PreInitProc(HWND Window, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK IdleProc(HWND Window, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WindowProc(HWND Window, UINT Message, WPARAM wParam, LPARAM lParam);

#define WM_INIT (WM_USER + 1)

#define BUFFER_COUNT 3
static const DXGI_FORMAT RTV_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
static const DXGI_FORMAT DSV_FORMAT = DXGI_FORMAT_D32_FLOAT;

#define CITY_ROW_COUNT 15
#define CITY_COLUMN_COUNT 8
#define CITY_MATERIAL_COUNT (CITY_ROW_COUNT * CITY_COLUMN_COUNT)
#define CITY_MATERIAL_TEXTURE_WIDTH 64
#define CITY_MATERIAL_TEXTURE_HEIGHT 64
#define CITY_MATERIAL_TEXTURE_CHANNEL_COUNT 4
#define CITY_MATERIAL_TEXTURE_SIZE (CITY_MATERIAL_TEXTURE_WIDTH * CITY_MATERIAL_TEXTURE_HEIGHT * CITY_MATERIAL_TEXTURE_CHANNEL_COUNT)
static const DXGI_FORMAT CITY_MATERIAL_TEXTURE_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
static const float CitySpacingInterval = 16.0f;

static const UINT SPECULAR_TEXTURE_WIDTH = 1024;
static const UINT SPECULAR_TEXTURE_HEIGHT = 1024;
static const DXGI_FORMAT SPECULAR_TEXTURE_FORMAT = DXGI_FORMAT_BC1_UNORM;
static const UINT SPECULAR_TEXTURE_SIZE = 524288;

struct Vertex
{
	vec3 Position;
	vec2 Uv;
};

static const UINT VERTEX_COUNT = 18642;
static const UINT VERTEX_BUFFER_SIZE = 18642 * sizeof(struct Vertex);

static const DXGI_FORMAT INDEX_BUFFER_FORMAT = DXGI_FORMAT_R32_UINT;
static const UINT INDEX_COUNT = 18642;
static const UINT INDEX_BUFFER_SIZE = 18642 * sizeof(uint32_t);

struct SceneConstantBuffer
{
	alignas(256) mat4 MVP;
};

struct FrameResource
{
	ID3D12CommandAllocator* CommandAllocator;
	ID3D12CommandAllocator* BundleAllocator;
	ID3D12GraphicsCommandList7* Bundle;
	ID3D12Resource* CbvUploadHeap;
	struct SceneConstantBuffer* pConstantBuffers;
	UINT64 FenceValue;

	mat4 ModelMatrices[CITY_ROW_COUNT * CITY_COLUMN_COUNT];
};

struct DxObjects
{
	IDXGISwapChain3* SwapChain;
	ID3D12Resource* RenderTargets[BUFFER_COUNT];
	ID3D12Resource* DepthStencil;
	ID3D12CommandAllocator* CommandAllocator;
	ID3D12CommandQueue* CommandQueue;
	ID3D12RootSignature* RootSignature;

	D3D12_CPU_DESCRIPTOR_HANDLE RtvHandle;

	ID3D12DescriptorHeap* CbvSrvHeap;
	D3D12_GPU_DESCRIPTOR_HANDLE CbvGpuHandle;

	D3D12_CPU_DESCRIPTOR_HANDLE DsvHandle;

	ID3D12DescriptorHeap* SamplerHeap;
	D3D12_GPU_DESCRIPTOR_HANDLE SamplerGpuHandle;

	ID3D12PipelineState* PipelineState;
	ID3D12GraphicsCommandList7* CommandList;

	D3D12_VERTEX_BUFFER_VIEW VertexBufferView;
	D3D12_INDEX_BUFFER_VIEW IndexBufferView;
	UINT CbvSrvDescriptorSize;
	UINT RtvDescriptorSize;

	struct FrameResource FrameResources[BUFFER_COUNT];

	UINT FrameIndex;
	UINT FrameCounter;
	HANDLE FenceEvent;
	ID3D12Fence* Fence[BUFFER_COUNT];
	UINT64 FenceValue[BUFFER_COUNT];
};

static const bool bWarp = false;
static const bool bBundles = false; //bundles are broken

void PopulateCommandList(
	struct FrameResource* pFrameResource,
	ID3D12GraphicsCommandList7* pCommandList,
	UINT frameResourceIndex,
	D3D12_INDEX_BUFFER_VIEW* IndexBufferViewDesc,
	D3D12_VERTEX_BUFFER_VIEW* pVertexBufferViewDesc,
	ID3D12DescriptorHeap* pCbvSrvDescriptorHeap,
	D3D12_GPU_DESCRIPTOR_HANDLE CbvGpuHandle,
	UINT cbvSrvDescriptorSize,
	ID3D12DescriptorHeap* pSamplerDescriptorHeap,
	D3D12_GPU_DESCRIPTOR_HANDLE SamplerGpuHandle,
	ID3D12RootSignature* pRootSignature);

inline void WaitForPreviousFrame(struct DxObjects* restrict DxObjects);

int main()
{
	ConsoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
	THROW_ON_FAIL(SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE));

	HINSTANCE hInstance = GetModuleHandleW(NULL);

	HICON Icon = LoadIconW(NULL, IDI_APPLICATION);
	HCURSOR Cursor = LoadCursorW(NULL, IDC_ARROW);

	WNDCLASSEXW WindowClass = { 0 };
	WindowClass.cbSize = sizeof(WNDCLASSEXW);
	WindowClass.style = CS_HREDRAW | CS_VREDRAW;
	WindowClass.lpfnWndProc = PreInitProc;
	WindowClass.cbClsExtra = 0;
	WindowClass.cbWndExtra = 0;
	WindowClass.hInstance = hInstance;
	WindowClass.hIcon = Icon;
	WindowClass.hCursor = Cursor;
	WindowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 2);
	WindowClass.lpszMenuName = NULL;
	WindowClass.lpszClassName = L"DXSampleClass";
	WindowClass.hIconSm = Icon;
	RegisterClassExW(&WindowClass);

	RECT WindowRect = { 0 };
	WindowRect.left = 0;
	WindowRect.top = 0;
	WindowRect.right = 1280;
	WindowRect.bottom = 720;
	AdjustWindowRect(&WindowRect, WS_OVERLAPPEDWINDOW, FALSE);

	HWND Window = CreateWindowExW(
		0L,
		WindowClass.lpszClassName,
		L"D3D12 Dynamic Indexing Sample",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		WindowRect.right - WindowRect.left,
		WindowRect.bottom - WindowRect.top,
		NULL,
		NULL,
		hInstance,
		NULL);

	VALIDATE_HANDLE(Window);

	THROW_ON_FALSE(ShowWindow(Window, SW_SHOW));

#ifdef _DEBUG
	ID3D12Debug6* DebugController;

	{
		ID3D12Debug* DebugControllerV1;
		THROW_ON_FAIL(D3D12GetDebugInterface(&IID_ID3D12Debug, &DebugControllerV1));
		THROW_ON_FAIL(ID3D12Debug_QueryInterface(DebugControllerV1, &IID_ID3D12Debug6, &DebugController));
		ID3D12Debug_Release(DebugControllerV1);
	}

	ID3D12Debug6_EnableDebugLayer(DebugController);
	ID3D12Debug6_SetEnableSynchronizedCommandQueueValidation(DebugController, TRUE);
	ID3D12Debug6_SetGPUBasedValidationFlags(DebugController, D3D12_GPU_BASED_VALIDATION_FLAGS_DISABLE_STATE_TRACKING);
	ID3D12Debug6_SetEnableGPUBasedValidation(DebugController, TRUE);
#endif

	IDXGIFactory6* Factory;

#ifdef _DEBUG
	THROW_ON_FAIL(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, &IID_IDXGIFactory6, &Factory));
#else
	THROW_ON_FAIL(CreateDXGIFactory2(0, &IID_IDXGIFactory6, &Factory));
#endif

	{
		IDXGIAdapter1* Adapter;
		if (bWarp)
		{
			THROW_ON_FAIL(IDXGIFactory6_EnumWarpAdapter(Factory, &IID_IDXGIAdapter1, &Adapter));
		}
		else
		{
			THROW_ON_FAIL(IDXGIFactory6_EnumAdapterByGpuPreference(Factory, 0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, &IID_IDXGIAdapter1, &Adapter));
		}

		THROW_ON_FAIL(D3D12CreateDevice(Adapter, D3D_FEATURE_LEVEL_12_2, &IID_ID3D12Device10, &Device));
		THROW_ON_FAIL(IDXGIAdapter1_Release(Adapter));
	}
	
#ifdef _DEBUG
	ID3D12InfoQueue* InfoQueue;
	THROW_ON_FAIL(ID3D12Device10_QueryInterface(Device, &IID_ID3D12InfoQueue, &InfoQueue));

	THROW_ON_FAIL(ID3D12InfoQueue_SetBreakOnSeverity(InfoQueue, D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE));
	THROW_ON_FAIL(ID3D12InfoQueue_SetBreakOnSeverity(InfoQueue, D3D12_MESSAGE_SEVERITY_ERROR, TRUE));
	THROW_ON_FAIL(ID3D12InfoQueue_SetBreakOnSeverity(InfoQueue, D3D12_MESSAGE_SEVERITY_WARNING, TRUE));
#endif

	struct DxObjects DxObjects = { 0 };

	{
		D3D12_COMMAND_QUEUE_DESC QueueDesc = { 0 };
		QueueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		QueueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		QueueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		THROW_ON_FAIL(ID3D12Device10_CreateCommandQueue(Device, &QueueDesc, &IID_ID3D12CommandQueue, &DxObjects.CommandQueue));
	}

	{
		DXGI_SWAP_CHAIN_DESC SwapChainDesc = { 0 };
		SwapChainDesc.BufferDesc.Width = 1;
		SwapChainDesc.BufferDesc.Height = 1;
		SwapChainDesc.BufferDesc.Format = RTV_FORMAT;
		SwapChainDesc.SampleDesc.Count = 1;
		SwapChainDesc.SampleDesc.Quality = 0;
		SwapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		SwapChainDesc.BufferCount = BUFFER_COUNT;
		SwapChainDesc.OutputWindow = Window;
		SwapChainDesc.Windowed = TRUE;
		SwapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		SwapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
		THROW_ON_FAIL(IDXGIFactory6_CreateSwapChain(Factory, DxObjects.CommandQueue, &SwapChainDesc, &DxObjects.SwapChain));
	}

	THROW_ON_FAIL(IDXGIFactory6_MakeWindowAssociation(Factory, Window, DXGI_MWA_NO_ALT_ENTER));
	THROW_ON_FAIL(IDXGIFactory6_Release(Factory));

	DxObjects.FrameIndex = IDXGISwapChain3_GetCurrentBackBufferIndex(DxObjects.SwapChain);

	ID3D12DescriptorHeap* RtvHeap;

	{
		D3D12_DESCRIPTOR_HEAP_DESC RtvHeapDesc = { 0 };
		RtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		RtvHeapDesc.NumDescriptors = BUFFER_COUNT;
		RtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		THROW_ON_FAIL(ID3D12Device10_CreateDescriptorHeap(Device, &RtvHeapDesc, &IID_ID3D12DescriptorHeap, &RtvHeap));
	}
	
	ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(RtvHeap, &DxObjects.RtvHandle);

	ID3D12DescriptorHeap* DsvHeap;
	
	{
		D3D12_DESCRIPTOR_HEAP_DESC DsvHeapDesc = { 0 };
		DsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		DsvHeapDesc.NumDescriptors = 1;
		DsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		THROW_ON_FAIL(ID3D12Device10_CreateDescriptorHeap(Device, &DsvHeapDesc, &IID_ID3D12DescriptorHeap, &DsvHeap));
	}

	ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(DsvHeap, &DxObjects.DsvHandle);
	
	{
		D3D12_DESCRIPTOR_HEAP_DESC CbvSrvHeapDesc = { 0 };
		CbvSrvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		CbvSrvHeapDesc.NumDescriptors =
			BUFFER_COUNT * CITY_ROW_COUNT * CITY_COLUMN_COUNT +    // FrameCount frames * CITY_ROW_COUNT * CITY_COLUMN_COUNT.
			CITY_MATERIAL_COUNT + 1;                            // CITY_MATERIAL_COUNT + 1 for the SRVs.
		CbvSrvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		THROW_ON_FAIL(ID3D12Device10_CreateDescriptorHeap(Device, &CbvSrvHeapDesc, &IID_ID3D12DescriptorHeap, &DxObjects.CbvSrvHeap));
	}

	ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(DxObjects.CbvSrvHeap, &DxObjects.CbvGpuHandle);

	{
		D3D12_DESCRIPTOR_HEAP_DESC SamplerHeapDesc = { 0 };
		SamplerHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
		SamplerHeapDesc.NumDescriptors = 1;
		SamplerHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		THROW_ON_FAIL(ID3D12Device10_CreateDescriptorHeap(Device, &SamplerHeapDesc, &IID_ID3D12DescriptorHeap, &DxObjects.SamplerHeap));
	}

	ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(DxObjects.SamplerHeap, &DxObjects.SamplerGpuHandle);

	DxObjects.RtvDescriptorSize = ID3D12Device10_GetDescriptorHandleIncrementSize(Device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	DxObjects.CbvSrvDescriptorSize = ID3D12Device10_GetDescriptorHandleIncrementSize(Device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	THROW_ON_FAIL(ID3D12Device10_CreateCommandAllocator(Device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, &DxObjects.CommandAllocator));

	{
		D3D12_DESCRIPTOR_RANGE1 Ranges[3] = { 0 };
		Ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		Ranges[0].NumDescriptors = 1 + CITY_MATERIAL_COUNT;// Diffuse texture + array of materials.
		Ranges[0].BaseShaderRegister = 0;
		Ranges[0].RegisterSpace = 0;
		Ranges[0].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;
		Ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		Ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
		Ranges[1].NumDescriptors = 1;
		Ranges[1].BaseShaderRegister = 0;
		Ranges[1].RegisterSpace = 0;
		Ranges[1].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
		Ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		Ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
		Ranges[2].NumDescriptors = 1;
		Ranges[2].BaseShaderRegister = 0;
		Ranges[2].RegisterSpace = 0;
		Ranges[2].Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;
		Ranges[2].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		
		D3D12_ROOT_PARAMETER1 RootParameters[4] = { 0 };
		RootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		RootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		RootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
		RootParameters[0].DescriptorTable.pDescriptorRanges = &Ranges[0];
		
		RootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		RootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		RootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
		RootParameters[1].DescriptorTable.pDescriptorRanges = &Ranges[1];

		RootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		RootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
		RootParameters[2].DescriptorTable.NumDescriptorRanges = 1;
		RootParameters[2].DescriptorTable.pDescriptorRanges = &Ranges[2];
		
		RootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		RootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		RootParameters[3].Constants.Num32BitValues = 1;
		RootParameters[3].Constants.ShaderRegister = 0;
		RootParameters[3].Constants.RegisterSpace = 0;

		D3D12_VERSIONED_ROOT_SIGNATURE_DESC RootSignatureDesc = { 0 };
		RootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
		RootSignatureDesc.Desc_1_1.NumParameters = ARRAYSIZE(RootParameters);
		RootSignatureDesc.Desc_1_1.pParameters = RootParameters;
		RootSignatureDesc.Desc_1_1.NumStaticSamplers = 0;
		RootSignatureDesc.Desc_1_1.pStaticSamplers = NULL;
		RootSignatureDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

		ID3D10Blob* Signature;
		THROW_ON_FAIL(D3D12SerializeVersionedRootSignature(&RootSignatureDesc, &Signature, NULL));
		THROW_ON_FAIL(ID3D12Device10_CreateRootSignature(Device, 0, ID3D10Blob_GetBufferPointer(Signature), ID3D10Blob_GetBufferSize(Signature), &IID_ID3D12RootSignature, &DxObjects.RootSignature));
		THROW_ON_FAIL(ID3D10Blob_Release(Signature));
	}

	{
		HANDLE VertexShaderFile = CreateFileW(L"VertexShader.cso", GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		VALIDATE_HANDLE(VertexShaderFile);

		LONGLONG VertexShaderSize;
		THROW_ON_FALSE(GetFileSizeEx(VertexShaderFile, &VertexShaderSize));

		HANDLE VertexShaderFileMap = CreateFileMappingW(VertexShaderFile, NULL, PAGE_READONLY, 0, 0, NULL);
		VALIDATE_HANDLE(VertexShaderFileMap);

		const void* VertexShaderBytecode = MapViewOfFile(VertexShaderFileMap, FILE_MAP_READ, 0, 0, 0);


		HANDLE PixelShaderFile = CreateFileW(L"PixelShader.cso", GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		VALIDATE_HANDLE(PixelShaderFile);

		LONGLONG PixelShaderSize;
		THROW_ON_FALSE(GetFileSizeEx(PixelShaderFile, &PixelShaderSize));

		HANDLE PixelShaderFileMap = CreateFileMappingW(PixelShaderFile, NULL, PAGE_READONLY, 0, 0, NULL);
		VALIDATE_HANDLE(PixelShaderFileMap);

		const void* PixelShaderBytecode = MapViewOfFile(PixelShaderFileMap, FILE_MAP_READ, 0, 0, 0);

		struct
		{
			alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE ObjectTypepRootSignature;
			ID3D12RootSignature* pRootSignature;

			alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE ObjectTypeInputLayout;
			D3D12_INPUT_LAYOUT_DESC InputLayout;

			alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE ObjectTypeVS;
			D3D12_SHADER_BYTECODE VS;

			alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE ObjectTypePS;
			D3D12_SHADER_BYTECODE PS;

			alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE ObjectTypeDepthStencilState;
			D3D12_DEPTH_STENCIL_DESC DepthStencilState;

			alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE ObjectTypeDSVFormat;
			DXGI_FORMAT DSVFormat;

			alignas(void*) D3D12_PIPELINE_STATE_SUBOBJECT_TYPE ObjectTypeRTVFormats;
			struct D3D12_RT_FORMAT_ARRAY RTVFormats;
		} PipelineStateObject = { 0 };

		PipelineStateObject.ObjectTypepRootSignature = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE;
		PipelineStateObject.pRootSignature = DxObjects.RootSignature;

		PipelineStateObject.ObjectTypeVS = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS;
		PipelineStateObject.VS.pShaderBytecode = VertexShaderBytecode;
		PipelineStateObject.VS.BytecodeLength = VertexShaderSize;

		PipelineStateObject.ObjectTypePS = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS;
		PipelineStateObject.PS.pShaderBytecode = PixelShaderBytecode;
		PipelineStateObject.PS.BytecodeLength = PixelShaderSize;

		PipelineStateObject.ObjectTypeDepthStencilState = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL;
		PipelineStateObject.DepthStencilState.DepthEnable = TRUE;
		PipelineStateObject.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		PipelineStateObject.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		PipelineStateObject.DepthStencilState.StencilEnable = FALSE;
		PipelineStateObject.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
		PipelineStateObject.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
		PipelineStateObject.DepthStencilState.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		PipelineStateObject.DepthStencilState.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		PipelineStateObject.DepthStencilState.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
		PipelineStateObject.DepthStencilState.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		PipelineStateObject.DepthStencilState.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		PipelineStateObject.DepthStencilState.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		PipelineStateObject.DepthStencilState.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
		PipelineStateObject.DepthStencilState.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

		PipelineStateObject.ObjectTypeInputLayout = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT;
		PipelineStateObject.InputLayout.pInputElementDescs = (D3D12_INPUT_ELEMENT_DESC[]){
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(struct Vertex, Position),  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, offsetof(struct Vertex, Uv), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 } };
		PipelineStateObject.InputLayout.NumElements = 2;

		PipelineStateObject.ObjectTypeDSVFormat = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT;
		PipelineStateObject.DSVFormat = DSV_FORMAT;

		PipelineStateObject.ObjectTypeRTVFormats = D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS;
		PipelineStateObject.RTVFormats.RTFormats[0] = RTV_FORMAT;
		PipelineStateObject.RTVFormats.NumRenderTargets = 1;

		D3D12_PIPELINE_STATE_STREAM_DESC PsoStreamDesc = { 0 };
		PsoStreamDesc.SizeInBytes = sizeof(PipelineStateObject);
		PsoStreamDesc.pPipelineStateSubobjectStream = &PipelineStateObject;

		THROW_ON_FAIL(ID3D12Device10_CreatePipelineState(Device, &PsoStreamDesc, &IID_ID3D12PipelineState, &DxObjects.PipelineState));

		THROW_ON_FALSE(UnmapViewOfFile(VertexShaderBytecode));
		THROW_ON_FALSE(CloseHandle(VertexShaderFileMap));
		THROW_ON_FALSE(CloseHandle(VertexShaderFile));

		THROW_ON_FALSE(UnmapViewOfFile(PixelShaderBytecode));
		THROW_ON_FALSE(CloseHandle(PixelShaderFileMap));
		THROW_ON_FALSE(CloseHandle(PixelShaderFile));
	}

	THROW_ON_FAIL(ID3D12Device10_CreateCommandList(Device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, DxObjects.CommandAllocator, NULL, &IID_ID3D12GraphicsCommandList7, &DxObjects.CommandList));
	
	{
		D3D12_CPU_DESCRIPTOR_HANDLE RtvHandle = DxObjects.RtvHandle;
		for (UINT i = 0; i < BUFFER_COUNT; i++)
		{
			THROW_ON_FAIL(IDXGISwapChain3_GetBuffer(DxObjects.SwapChain, i, &IID_ID3D12Resource, &DxObjects.RenderTargets[i]));
			ID3D12Device10_CreateRenderTargetView(Device, DxObjects.RenderTargets[i], NULL, RtvHandle);
			RtvHandle.ptr += DxObjects.RtvDescriptorSize;
		}
	}

	ID3D12Resource* CityDiffuseTexture;

	{
		D3D12_HEAP_PROPERTIES HeapProperties = { 0 };
		HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
		HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		HeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

		D3D12_RESOURCE_DESC1 SpecularTextureResourceDesc = { 0 };
		SpecularTextureResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		SpecularTextureResourceDesc.Alignment = 0;
		SpecularTextureResourceDesc.Width = SPECULAR_TEXTURE_WIDTH;
		SpecularTextureResourceDesc.Height = SPECULAR_TEXTURE_HEIGHT;
		SpecularTextureResourceDesc.DepthOrArraySize = 1;
		SpecularTextureResourceDesc.MipLevels = 1;
		SpecularTextureResourceDesc.Format = SPECULAR_TEXTURE_FORMAT;
		SpecularTextureResourceDesc.SampleDesc.Count = 1;
		SpecularTextureResourceDesc.SampleDesc.Quality = 0;
		SpecularTextureResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		SpecularTextureResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		THROW_ON_FAIL(ID3D12Device10_CreateCommittedResource3(
			Device,
			&HeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&SpecularTextureResourceDesc,
			D3D12_BARRIER_LAYOUT_COMMON,
			NULL,
			NULL,
			0,
			NULL,
			&IID_ID3D12Resource,
			&CityDiffuseTexture));
	}

	ID3D12Resource* TextureUploadHeap;

	{
		D3D12_HEAP_PROPERTIES HeapProperties = { 0 };
		HeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
		HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		HeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

		D3D12_RESOURCE_DESC1 ResourceDesc = { 0 };
		ResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		ResourceDesc.Alignment = 0;
		ResourceDesc.Width = SPECULAR_TEXTURE_SIZE;
		ResourceDesc.Height = 1;
		ResourceDesc.DepthOrArraySize = 1;
		ResourceDesc.MipLevels = 1;
		ResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		ResourceDesc.SampleDesc.Count = 1;
		ResourceDesc.SampleDesc.Quality = 0;
		ResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		ResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		THROW_ON_FAIL(ID3D12Device10_CreateCommittedResource3(
			Device,
			&HeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&ResourceDesc,
			D3D12_BARRIER_LAYOUT_UNDEFINED,
			NULL,
			NULL,
			0,
			NULL,
			&IID_ID3D12Resource,
			&TextureUploadHeap));
	}


	ID3D12Resource* VertexBuffer;
	ID3D12Resource* VertexBufferUploadHeap;

	{
		D3D12_RESOURCE_DESC1 ResourceDesc = { 0 };
		ResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		ResourceDesc.Alignment = 0;
		ResourceDesc.Width = VERTEX_BUFFER_SIZE;
		ResourceDesc.Height = 1;
		ResourceDesc.DepthOrArraySize = 1;
		ResourceDesc.MipLevels = 1;
		ResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		ResourceDesc.SampleDesc.Count = 1;
		ResourceDesc.SampleDesc.Quality = 0;
		ResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		ResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		{
			D3D12_HEAP_PROPERTIES HeapProperties = { 0 };
			HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
			HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			HeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

			THROW_ON_FAIL(ID3D12Device10_CreateCommittedResource3(
				Device,
				&HeapProperties,
				D3D12_HEAP_FLAG_NONE,
				&ResourceDesc,
				D3D12_BARRIER_LAYOUT_UNDEFINED,
				NULL,
				NULL,
				0,
				NULL,
				&IID_ID3D12Resource,
				&VertexBuffer));
		}

		{
			D3D12_HEAP_PROPERTIES HeapProperties = { 0 };
			HeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
			HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			HeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

			THROW_ON_FAIL(ID3D12Device10_CreateCommittedResource3(
				Device,
				&HeapProperties,
				D3D12_HEAP_FLAG_NONE,
				&ResourceDesc,
				D3D12_BARRIER_LAYOUT_UNDEFINED,
				NULL,
				NULL,
				0,
				NULL,
				&IID_ID3D12Resource,
				&VertexBufferUploadHeap));
		}
	}

	ID3D12Resource* IndexBuffer;
	ID3D12Resource* IndexBufferUploadHeap;

	{
		D3D12_RESOURCE_DESC1 ResourceDesc = { 0 };
		ResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		ResourceDesc.Alignment = 0;
		ResourceDesc.Width = INDEX_BUFFER_SIZE;
		ResourceDesc.Height = 1;
		ResourceDesc.DepthOrArraySize = 1;
		ResourceDesc.MipLevels = 1;
		ResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		ResourceDesc.SampleDesc.Count = 1;
		ResourceDesc.SampleDesc.Quality = 0;
		ResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		ResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		{
			D3D12_HEAP_PROPERTIES HeapProperties = { 0 };
			HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
			HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			HeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

			THROW_ON_FAIL(ID3D12Device10_CreateCommittedResource3(
				Device,
				&HeapProperties,
				D3D12_HEAP_FLAG_NONE,
				&ResourceDesc,
				D3D12_BARRIER_LAYOUT_UNDEFINED,
				NULL,
				NULL,
				0,
				NULL,
				&IID_ID3D12Resource,
				&IndexBuffer));
		}

		{
			D3D12_HEAP_PROPERTIES HeapProperties = { 0 };
			HeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
			HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			HeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

			THROW_ON_FAIL(ID3D12Device10_CreateCommittedResource3(
				Device,
				&HeapProperties,
				D3D12_HEAP_FLAG_NONE,
				&ResourceDesc,
				D3D12_BARRIER_LAYOUT_UNDEFINED,
				NULL,
				NULL,
				0,
				NULL,
				&IID_ID3D12Resource,
				&IndexBufferUploadHeap));
		}
	}

	{
		HANDLE MeshDataFile = CreateFileW(L"occcity.bin", GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
		VALIDATE_HANDLE(MeshDataFile);

		LONGLONG MeshDataSize;
		THROW_ON_FALSE(GetFileSizeEx(MeshDataFile, &MeshDataSize));

		HANDLE MeshDataFileMap = CreateFileMappingW(MeshDataFile, NULL, PAGE_READONLY, 0, 0, NULL);
		VALIDATE_HANDLE(MeshDataFileMap);

		const void* MeshDataBytecode = MapViewOfFile(MeshDataFileMap, FILE_MAP_READ, 0, 0, 0);
		const void* MeshDataReadPtr = MeshDataBytecode;
		
		{
			void* pData;
			THROW_ON_FAIL(ID3D12Resource_Map(TextureUploadHeap, 0, NULL, &pData));
			memcpy(pData, MeshDataReadPtr, SPECULAR_TEXTURE_SIZE);
			ID3D12Resource_Unmap(TextureUploadHeap, 0, NULL);
		}

		MeshDataReadPtr = OffsetPointer(MeshDataReadPtr, SPECULAR_TEXTURE_SIZE);

		{
			void* pData;
			THROW_ON_FAIL(ID3D12Resource_Map(VertexBufferUploadHeap, 0, NULL, &pData));
			memcpy(pData, MeshDataReadPtr, VERTEX_BUFFER_SIZE);
			ID3D12Resource_Unmap(VertexBufferUploadHeap, 0, NULL);
		}

		MeshDataReadPtr = OffsetPointer(MeshDataReadPtr, VERTEX_BUFFER_SIZE);

		{
			void* pData;
			THROW_ON_FAIL(ID3D12Resource_Map(IndexBufferUploadHeap, 0, NULL, &pData));
			memcpy(pData, MeshDataReadPtr, INDEX_BUFFER_SIZE);
			ID3D12Resource_Unmap(IndexBufferUploadHeap, 0, NULL);
		}

		THROW_ON_FALSE(UnmapViewOfFile(MeshDataBytecode));
		THROW_ON_FALSE(CloseHandle(MeshDataFileMap));
		THROW_ON_FALSE(CloseHandle(MeshDataFile));
	}

	{
		D3D12_TEXTURE_COPY_LOCATION Dest = { 0 };
		Dest.pResource = CityDiffuseTexture;
		Dest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		Dest.SubresourceIndex = 0;

		D3D12_TEXTURE_COPY_LOCATION Src = { 0 };
		Src.pResource = TextureUploadHeap;
		Src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		Src.PlacedFootprint.Offset = 0;
		Src.PlacedFootprint.Footprint.Format = SPECULAR_TEXTURE_FORMAT;
		Src.PlacedFootprint.Footprint.Width = SPECULAR_TEXTURE_WIDTH;
		Src.PlacedFootprint.Footprint.Height = SPECULAR_TEXTURE_HEIGHT;
		Src.PlacedFootprint.Footprint.Depth = 1;
		Src.PlacedFootprint.Footprint.RowPitch = (SPECULAR_TEXTURE_WIDTH / 2) * 4;

		ID3D12GraphicsCommandList7_CopyTextureRegion(DxObjects.CommandList, &Dest, 0, 0, 0, &Src, NULL);
	}

	{
		D3D12_TEXTURE_BARRIER TextureBarrier = { 0 };
		TextureBarrier.SyncBefore = D3D12_BARRIER_SYNC_COPY;
		TextureBarrier.SyncAfter = D3D12_BARRIER_SYNC_ALL;
		TextureBarrier.AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST;
		TextureBarrier.AccessAfter = D3D12_BARRIER_ACCESS_COMMON;
		TextureBarrier.LayoutBefore = D3D12_BARRIER_LAYOUT_COMMON;
		TextureBarrier.LayoutAfter = D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
		TextureBarrier.pResource = CityDiffuseTexture;
		TextureBarrier.Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;

		D3D12_BARRIER_GROUP ResourceBarrier = { 0 };
		ResourceBarrier.Type = D3D12_BARRIER_TYPE_TEXTURE;
		ResourceBarrier.NumBarriers = 1;
		ResourceBarrier.pTextureBarriers = &TextureBarrier;
		ID3D12GraphicsCommandList7_Barrier(DxObjects.CommandList, 1, &ResourceBarrier);
	}

	ID3D12GraphicsCommandList7_CopyBufferRegion(
		DxObjects.CommandList,
		VertexBuffer,
		0,
		VertexBufferUploadHeap,
		0,
		VERTEX_BUFFER_SIZE);

	{
		D3D12_BUFFER_BARRIER BufferBarrier = { 0 };
		BufferBarrier.SyncBefore = D3D12_BARRIER_SYNC_COPY;
		BufferBarrier.SyncAfter = D3D12_BARRIER_SYNC_DRAW;
		BufferBarrier.AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST;
		BufferBarrier.AccessAfter = D3D12_BARRIER_ACCESS_VERTEX_BUFFER;
		BufferBarrier.pResource = VertexBuffer;
		BufferBarrier.Offset = 0;
		BufferBarrier.Size = VERTEX_BUFFER_SIZE;

		D3D12_BARRIER_GROUP ResourceBarrier = { 0 };
		ResourceBarrier.Type = D3D12_BARRIER_TYPE_BUFFER;
		ResourceBarrier.NumBarriers = 1;
		ResourceBarrier.pBufferBarriers = &BufferBarrier;
		ID3D12GraphicsCommandList7_Barrier(DxObjects.CommandList, 1, &ResourceBarrier);
	}

	DxObjects.VertexBufferView.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(VertexBuffer);
	DxObjects.VertexBufferView.SizeInBytes = VERTEX_BUFFER_SIZE;
	DxObjects.VertexBufferView.StrideInBytes = sizeof(struct Vertex);

	ID3D12GraphicsCommandList7_CopyBufferRegion(
		DxObjects.CommandList,
		IndexBuffer,
		0,
		IndexBufferUploadHeap,
		0,
		INDEX_BUFFER_SIZE);

	{
		D3D12_BUFFER_BARRIER BufferBarrier = { 0 };
		BufferBarrier.SyncBefore = D3D12_BARRIER_SYNC_COPY;
		BufferBarrier.SyncAfter = D3D12_BARRIER_SYNC_DRAW;
		BufferBarrier.AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST;
		BufferBarrier.AccessAfter = D3D12_BARRIER_ACCESS_INDEX_BUFFER;
		BufferBarrier.pResource = IndexBuffer;
		BufferBarrier.Offset = 0;
		BufferBarrier.Size = INDEX_BUFFER_SIZE;

		D3D12_BARRIER_GROUP ResourceBarrier = { 0 };
		ResourceBarrier.Type = D3D12_BARRIER_TYPE_BUFFER;
		ResourceBarrier.NumBarriers = 1;
		ResourceBarrier.pBufferBarriers = &BufferBarrier;
		ID3D12GraphicsCommandList7_Barrier(DxObjects.CommandList, 1, &ResourceBarrier);
	}

	DxObjects.IndexBufferView.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(IndexBuffer);
	DxObjects.IndexBufferView.Format = INDEX_BUFFER_FORMAT;
	DxObjects.IndexBufferView.SizeInBytes = INDEX_BUFFER_SIZE;

	ID3D12Resource* CityMaterialTextures[CITY_MATERIAL_COUNT];

	for (int i = 0; i < CITY_MATERIAL_COUNT; i++)
	{
		D3D12_HEAP_PROPERTIES HeapProperties = { 0 };
		HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
		HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		HeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

		D3D12_RESOURCE_DESC1 TextureResourceDesc = { 0 };
		TextureResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		TextureResourceDesc.Alignment = 0;
		TextureResourceDesc.Width = CITY_MATERIAL_TEXTURE_WIDTH;
		TextureResourceDesc.Height = CITY_MATERIAL_TEXTURE_HEIGHT;
		TextureResourceDesc.DepthOrArraySize = 1;
		TextureResourceDesc.MipLevels = 0;
		TextureResourceDesc.Format = CITY_MATERIAL_TEXTURE_FORMAT;
		TextureResourceDesc.SampleDesc.Count = 1;
		TextureResourceDesc.SampleDesc.Quality = 0;
		TextureResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		TextureResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

		THROW_ON_FAIL(ID3D12Device10_CreateCommittedResource3(
			Device,
			&HeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&TextureResourceDesc,
			D3D12_BARRIER_LAYOUT_COMMON,
			NULL,
			NULL,
			0,
			NULL,
			&IID_ID3D12Resource,
			&CityMaterialTextures[i]));
	}

	ID3D12Resource* MaterialsUploadHeap;

	{
		D3D12_HEAP_PROPERTIES HeapProperties = { 0 };
		HeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
		HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		HeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

		D3D12_RESOURCE_DESC1 ResourceDesc = { 0 };
		ResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		ResourceDesc.Alignment = 0;
		ResourceDesc.Width = CITY_MATERIAL_TEXTURE_SIZE * (15 * 8);
		ResourceDesc.Height = 1;
		ResourceDesc.DepthOrArraySize = 1;
		ResourceDesc.MipLevels = 1;
		ResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		ResourceDesc.SampleDesc.Count = 1;
		ResourceDesc.SampleDesc.Quality = 0;
		ResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		ResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		THROW_ON_FAIL(ID3D12Device10_CreateCommittedResource3(
			Device,
			&HeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&ResourceDesc,
			D3D12_BARRIER_LAYOUT_UNDEFINED,
			NULL,
			NULL,
			0,
			NULL,
			&IID_ID3D12Resource,
			&MaterialsUploadHeap));
	}

	{
		void* pData;
		THROW_ON_FAIL(ID3D12Resource_Map(MaterialsUploadHeap, 0, NULL, &pData));

		for (int i = 0; i < CITY_MATERIAL_COUNT; i++)
		{
			static unsigned char CityTextureData[CITY_MATERIAL_TEXTURE_SIZE] = { 0 };
			const float MaterialGradStep = 1.0f / (float)CITY_MATERIAL_COUNT;
			const float t = i * MaterialGradStep;
			for (int y = 0; y < CITY_MATERIAL_TEXTURE_HEIGHT; y++)
			{
				for (int x = 0; x < CITY_MATERIAL_TEXTURE_WIDTH; x++)
				{
					const int PixelIndex = (y * CITY_MATERIAL_TEXTURE_CHANNEL_COUNT * CITY_MATERIAL_TEXTURE_WIDTH) + (x * CITY_MATERIAL_TEXTURE_CHANNEL_COUNT);

					const float tPrime = t + (((float)y / (float)CITY_MATERIAL_TEXTURE_HEIGHT) * MaterialGradStep);

					const vec4 HSL = { tPrime, 0.5f, 0.5f, 1.0f };
					const float h = HSL[0], s = HSL[1], l = HSL[2];
					const float c = (1.0f - fabsf(2.0f * l - 1.0f)) * s;
					const float h_prime = h * 6.0f;
					const float x = c * (1.0f - fabsf(fmodf(h_prime, 2.0f) - 1.0f));
					const float m = l - c * 0.5f;

					vec4 RGB;

					if (h_prime < 1.0f)
					{
						RGB[0] = c + m; RGB[1] = x + m; RGB[2] = m;
					}
					else if (h_prime < 2.0f)
					{
						RGB[0] = x + m; RGB[1] = c + m; RGB[2] = m;
					}
					else if (h_prime < 3.0f)
					{
						RGB[0] = m; RGB[1] = c + m; RGB[2] = x + m;
					}
					else if (h_prime < 4.0f)
					{
						RGB[0] = m; RGB[1] = x + m; RGB[2] = c + m;
					}
					else if (h_prime < 5.0f)
					{
						RGB[0] = x + m; RGB[1] = m; RGB[2] = c + m;
					}
					else
					{
						RGB[0] = c + m; RGB[1] = m; RGB[2] = x + m;
					}
					RGB[3] = HSL[3];

					CityTextureData[PixelIndex + 0] = (unsigned char)(255 * RGB[0]);
					CityTextureData[PixelIndex + 1] = (unsigned char)(255 * RGB[1]);
					CityTextureData[PixelIndex + 2] = (unsigned char)(255 * RGB[2]);
					CityTextureData[PixelIndex + 3] = 255;
				}
			}

			memcpy(OffsetPointer(pData, CITY_MATERIAL_TEXTURE_SIZE * i), CityTextureData, CITY_MATERIAL_TEXTURE_SIZE);
		}

		ID3D12Resource_Unmap(MaterialsUploadHeap, 0, NULL);
	}

	for (int i = 0; i < CITY_MATERIAL_COUNT; i++)
	{
		D3D12_TEXTURE_COPY_LOCATION Dest = { 0 };
		Dest.pResource = CityMaterialTextures[i];
		Dest.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		Dest.SubresourceIndex = 0;

		D3D12_TEXTURE_COPY_LOCATION Src = { 0 };
		Src.pResource = MaterialsUploadHeap;
		Src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		Src.PlacedFootprint.Offset = i * CITY_MATERIAL_TEXTURE_SIZE;
		Src.PlacedFootprint.Footprint.Format = CITY_MATERIAL_TEXTURE_FORMAT;
		Src.PlacedFootprint.Footprint.Width = CITY_MATERIAL_TEXTURE_WIDTH;
		Src.PlacedFootprint.Footprint.Height = CITY_MATERIAL_TEXTURE_HEIGHT;
		Src.PlacedFootprint.Footprint.Depth = 1;
		Src.PlacedFootprint.Footprint.RowPitch = CITY_MATERIAL_TEXTURE_WIDTH * CITY_MATERIAL_TEXTURE_CHANNEL_COUNT;

		ID3D12GraphicsCommandList7_CopyTextureRegion(DxObjects.CommandList, &Dest, 0, 0, 0, &Src, NULL);
	}

	D3D12_CPU_DESCRIPTOR_HANDLE SrvHandle;
	ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(DxObjects.CbvSrvHeap, &SrvHandle);

	{
		D3D12_SHADER_RESOURCE_VIEW_DESC DiffuseSrvDesc = { 0 };
		DiffuseSrvDesc.Format = SPECULAR_TEXTURE_FORMAT;
		DiffuseSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		DiffuseSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		DiffuseSrvDesc.Texture2D.MipLevels = 1;
		ID3D12Device10_CreateShaderResourceView(Device, CityDiffuseTexture, &DiffuseSrvDesc, SrvHandle);
	}

	{
		D3D12_CPU_DESCRIPTOR_HANDLE MaterialSrvHandle = SrvHandle;
		MaterialSrvHandle.ptr += DxObjects.CbvSrvDescriptorSize;

		for (int i = 0; i < CITY_MATERIAL_COUNT; i++)
		{
			D3D12_SHADER_RESOURCE_VIEW_DESC MRaterialSrvDesc = { 0 };
			MRaterialSrvDesc.Format = CITY_MATERIAL_TEXTURE_FORMAT;
			MRaterialSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			MRaterialSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
			MRaterialSrvDesc.Texture2D.MipLevels = 1;
			ID3D12Device10_CreateShaderResourceView(Device, CityMaterialTextures[i], &MRaterialSrvDesc, MaterialSrvHandle);

			MaterialSrvHandle.ptr += DxObjects.CbvSrvDescriptorSize;
		}
	}

	{
		D3D12_TEXTURE_BARRIER TextureBarriers[CITY_MATERIAL_COUNT] = { 0 };
		for (int i = 0; i < ARRAYSIZE(TextureBarriers); i++)
		{
			TextureBarriers[i].SyncBefore = D3D12_BARRIER_SYNC_COPY;
			TextureBarriers[i].SyncAfter = D3D12_BARRIER_SYNC_ALL;
			TextureBarriers[i].AccessBefore = D3D12_BARRIER_ACCESS_COPY_DEST;
			TextureBarriers[i].AccessAfter = D3D12_BARRIER_ACCESS_COMMON;
			TextureBarriers[i].LayoutBefore = D3D12_BARRIER_LAYOUT_COMMON;
			TextureBarriers[i].LayoutAfter = D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
			TextureBarriers[i].pResource = CityMaterialTextures[i];
			TextureBarriers[i].Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;
		}

		D3D12_BARRIER_GROUP ResourceBarrier = { 0 };
		ResourceBarrier.Type = D3D12_BARRIER_TYPE_TEXTURE;
		ResourceBarrier.NumBarriers = ARRAYSIZE(TextureBarriers);
		ResourceBarrier.pTextureBarriers = TextureBarriers;
		ID3D12GraphicsCommandList7_Barrier(DxObjects.CommandList, 1, &ResourceBarrier);
	}

	{
		D3D12_SAMPLER_DESC SamplerDesc = { 0 };
		SamplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		SamplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		SamplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		SamplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		SamplerDesc.MipLODBias = 0.0f;
		SamplerDesc.MaxAnisotropy = 1;
		SamplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NONE;
		SamplerDesc.MinLOD = 0;
		SamplerDesc.MaxLOD = D3D12_FLOAT32_MAX;

		D3D12_CPU_DESCRIPTOR_HANDLE SamplerHandle;
		ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(DxObjects.SamplerHeap, &SamplerHandle);

		ID3D12Device10_CreateSampler(Device, &SamplerDesc, SamplerHandle);
	}

	THROW_ON_FAIL(ID3D12GraphicsCommandList7_Close(DxObjects.CommandList));
	ID3D12CommandQueue_ExecuteCommandLists(DxObjects.CommandQueue, 1, &DxObjects.CommandList);

	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		THROW_ON_FAIL(ID3D12Device10_CreateFence(Device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, &DxObjects.Fence[i]));
		DxObjects.FenceValue[i] = 0;
	}

	DxObjects.FenceValue[DxObjects.FrameIndex]++;
	THROW_ON_FAIL(ID3D12CommandQueue_Signal(DxObjects.CommandQueue, DxObjects.Fence[DxObjects.FrameIndex], DxObjects.FenceValue[DxObjects.FrameIndex]));

	DxObjects.FenceEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
	VALIDATE_HANDLE(DxObjects.FenceEvent);

	WaitForPreviousFrame(&DxObjects);

	THROW_ON_FAIL(ID3D12Resource_Release(VertexBufferUploadHeap));
	THROW_ON_FAIL(ID3D12Resource_Release(IndexBufferUploadHeap));
	THROW_ON_FAIL(ID3D12Resource_Release(TextureUploadHeap));
	THROW_ON_FAIL(ID3D12Resource_Release(MaterialsUploadHeap));

	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		THROW_ON_FAIL(ID3D12Device10_CreateCommandAllocator(Device, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, &DxObjects.FrameResources[i].CommandAllocator));
	}

	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		if (bBundles)
		{
			THROW_ON_FAIL(ID3D12Device10_CreateCommandAllocator(Device, D3D12_COMMAND_LIST_TYPE_BUNDLE, &IID_ID3D12CommandAllocator, &DxObjects.FrameResources[i].BundleAllocator));
		}
	}

	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		D3D12_HEAP_PROPERTIES HeapProperties = { 0 };
		HeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
		HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
		HeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

		D3D12_RESOURCE_DESC1 ResourceDesc = { 0 };
		ResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		ResourceDesc.Alignment = 0;
		ResourceDesc.Width = sizeof(struct SceneConstantBuffer) * CITY_ROW_COUNT * CITY_COLUMN_COUNT;
		ResourceDesc.Height = 1;
		ResourceDesc.DepthOrArraySize = 1;
		ResourceDesc.MipLevels = 1;
		ResourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		ResourceDesc.SampleDesc.Count = 1;
		ResourceDesc.SampleDesc.Quality = 0;
		ResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		ResourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		THROW_ON_FAIL(ID3D12Device10_CreateCommittedResource3(
			Device,
			&HeapProperties,
			D3D12_HEAP_FLAG_NONE,
			&ResourceDesc,
			D3D12_BARRIER_LAYOUT_UNDEFINED,
			NULL,
			NULL,
			0,
			NULL,
			&IID_ID3D12Resource,
			&DxObjects.FrameResources[i].CbvUploadHeap));
	}

	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		THROW_ON_FAIL(ID3D12Resource_Map(DxObjects.FrameResources[i].CbvUploadHeap, 0, NULL, &DxObjects.FrameResources[i].pConstantBuffers));

		for (int j = 0; j < CITY_ROW_COUNT; j++)
		{
			float CityOffsetZ = j * -CitySpacingInterval;
			for (int k = 0; k < CITY_COLUMN_COUNT; k++)
			{
				float CityOffsetX = k * CitySpacingInterval;

				glm_mat4_identity(DxObjects.FrameResources[i].ModelMatrices[j * CITY_COLUMN_COUNT + k]);
				glm_translate(DxObjects.FrameResources[i].ModelMatrices[j * CITY_COLUMN_COUNT + k], (vec3) { CityOffsetX, 0.02f * (j * CITY_COLUMN_COUNT + k), CityOffsetZ });
			}
		}
	}

	{
		D3D12_CPU_DESCRIPTOR_HANDLE MaterialSrvHandle = SrvHandle;
		MaterialSrvHandle.ptr += (CITY_MATERIAL_COUNT + 1) * DxObjects.CbvSrvDescriptorSize; // Move past the SRVs.
		for (int i = 0; i < BUFFER_COUNT; i++)
		{
			UINT64 CbOffset = 0;
			for (int j = 0; j < CITY_MATERIAL_COUNT; j++)
			{
				D3D12_CONSTANT_BUFFER_VIEW_DESC CbvDesc = { 0 };
				CbvDesc.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(DxObjects.FrameResources[i].CbvUploadHeap) + CbOffset;
				CbvDesc.SizeInBytes = sizeof(struct SceneConstantBuffer);
				ID3D12Device10_CreateConstantBufferView(Device, &CbvDesc, MaterialSrvHandle);

				CbOffset += sizeof(struct SceneConstantBuffer);
				MaterialSrvHandle.ptr += DxObjects.CbvSrvDescriptorSize;
			}
		}
	}

	if (bBundles)
	{
		for (int i = 0; i < BUFFER_COUNT; i++)
		{
			THROW_ON_FAIL(ID3D12Device10_CreateCommandList(Device, 0, D3D12_COMMAND_LIST_TYPE_BUNDLE, DxObjects.FrameResources[i].BundleAllocator, DxObjects.PipelineState, &IID_ID3D12GraphicsCommandList7, &DxObjects.FrameResources[i].Bundle));

			PopulateCommandList(
				&DxObjects.FrameResources[i],
				DxObjects.FrameResources[i].Bundle,
				i,
				&DxObjects.IndexBufferView,
				&DxObjects.VertexBufferView,
				DxObjects.CbvSrvHeap,
				DxObjects.CbvGpuHandle,
				DxObjects.CbvSrvDescriptorSize,
				DxObjects.SamplerHeap,
				DxObjects.SamplerGpuHandle,
				DxObjects.RootSignature);

			THROW_ON_FAIL(ID3D12GraphicsCommandList7_Close(DxObjects.FrameResources[i].Bundle));
		}
	}

	THROW_ON_FALSE(SetWindowLongPtrW(Window, GWLP_WNDPROC, (LONG_PTR)WindowProc) != 0);

	DispatchMessageW(&(MSG) {
		.hwnd = Window,
		.message = WM_INIT,
		.wParam = (WPARAM)&DxObjects,
		.lParam = 0
	});

	DispatchMessageW(&(MSG) {
		.hwnd = Window,
		.message = WM_SIZE,
		.wParam = SIZE_RESTORED,
		.lParam = MAKELONG(WindowRect.right - WindowRect.left, WindowRect.bottom - WindowRect.top)
	});

	THROW_ON_FALSE(ShowWindow(Window, SW_SHOW));

	MSG Message = { 0 };
	while (Message.message != WM_QUIT)
	{
		if (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&Message);
			DispatchMessageW(&Message);
		}
	}

	WaitForPreviousFrame(&DxObjects);

	for (int i = 0; i < BUFFER_COUNT; i++)
	{
		ID3D12Resource_Unmap(DxObjects.FrameResources[i].CbvUploadHeap, 0, NULL);
		DxObjects.FrameResources[i].pConstantBuffers = NULL;
	}

	THROW_ON_FAIL(IDXGISwapChain3_Release(DxObjects.SwapChain));

	for(int i = 0; i < ARRAYSIZE(DxObjects.RenderTargets); i++)
	{
		THROW_ON_FAIL(ID3D12Resource_Release(DxObjects.RenderTargets[i]));
	}
	
	THROW_ON_FAIL(ID3D12Resource_Release(DxObjects.DepthStencil));
	THROW_ON_FAIL(ID3D12CommandAllocator_Release(DxObjects.CommandAllocator));
	THROW_ON_FAIL(ID3D12CommandQueue_Release(DxObjects.CommandQueue));
	THROW_ON_FAIL(ID3D12RootSignature_Release(DxObjects.RootSignature));
	THROW_ON_FAIL(ID3D12DescriptorHeap_Release(RtvHeap));
	THROW_ON_FAIL(ID3D12DescriptorHeap_Release(DxObjects.CbvSrvHeap));
	THROW_ON_FAIL(ID3D12DescriptorHeap_Release(DsvHeap));
	THROW_ON_FAIL(ID3D12DescriptorHeap_Release(DxObjects.SamplerHeap));
	THROW_ON_FAIL(ID3D12PipelineState_Release(DxObjects.PipelineState));
	THROW_ON_FAIL(ID3D12GraphicsCommandList7_Release(DxObjects.CommandList));

	THROW_ON_FAIL(ID3D12Resource_Release(VertexBuffer));
	THROW_ON_FAIL(ID3D12Resource_Release(IndexBuffer));
	THROW_ON_FAIL(ID3D12Resource_Release(CityDiffuseTexture));
	for (int i = 0; i < ARRAYSIZE(CityMaterialTextures); i++)
	{
		THROW_ON_FAIL(ID3D12Resource_Release(CityMaterialTextures[i]));
	}

	for (int i = 0; i < ARRAYSIZE(DxObjects.FrameResources); i++)
	{
		THROW_ON_FAIL(ID3D12CommandAllocator_Release(DxObjects.FrameResources[i].CommandAllocator));
		if (bBundles)
		{
			THROW_ON_FAIL(ID3D12CommandAllocator_Release(DxObjects.FrameResources[i].BundleAllocator));
			THROW_ON_FAIL(ID3D12GraphicsCommandList7_Release(DxObjects.FrameResources[i].Bundle));
		}
		THROW_ON_FAIL(ID3D12Resource_Release(DxObjects.FrameResources[i].CbvUploadHeap));
	}

	for (int i = 0; i < ARRAYSIZE(DxObjects.Fence); i++)
	{
		THROW_ON_FAIL(ID3D12Fence_Release(DxObjects.Fence[i]));
	}

#ifdef _DEBUG
	THROW_ON_FAIL(ID3D12InfoQueue_Release(InfoQueue));
#endif

	THROW_ON_FAIL(ID3D12Device10_Release(Device));

#ifdef _DEBUG
	THROW_ON_FAIL(ID3D12Debug6_Release(DebugController));
#endif

	THROW_ON_FALSE(UnregisterClassW(WindowClass.lpszClassName, hInstance));

	THROW_ON_FALSE(DestroyCursor(Cursor));
	THROW_ON_FALSE(DestroyIcon(Icon));

#ifdef _DEBUG
	{
		IDXGIDebug1* DxgiDebug;
		THROW_ON_FAIL(DXGIGetDebugInterface1(0, &IID_IDXGIDebug1, &DxgiDebug));
		THROW_ON_FAIL(IDXGIDebug1_ReportLiveObjects(DxgiDebug, DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
	}
#endif

	return Message.wParam;
}

LRESULT CALLBACK PreInitProc(HWND Window, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProcW(Window, message, wParam, lParam);
	}
	return 0;
}

LRESULT CALLBACK IdleProc(HWND Window, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_PAINT:
		Sleep(25);
		break;
	case WM_SIZE:
		if (wParam == SIZE_RESTORED)
			THROW_ON_FALSE(SetWindowLongPtrW(Window, GWLP_WNDPROC, (LONG_PTR)WindowProc) != 0);
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProcW(Window, message, wParam, lParam);
	}
	return 0;
}

LRESULT CALLBACK WindowProc(HWND Window, UINT Message, WPARAM wParam, LPARAM lParam)
{
	static struct
	{
		bool w;
		bool a;
		bool s;
		bool d;

		bool Left;
		bool Right;
		bool Up;
		bool Down;
	} KeyStates;

	static struct
	{
		UINT WindowWidth;
		UINT WindowHeight;

		float AspectRatio;

		bool bFullScreen;
		bool bVsync;

		D3D12_VIEWPORT Viewport;
		D3D12_RECT ScissorRect;
	} WindowDetails = { 0 };

	static struct
	{
		LARGE_INTEGER QPCFrequency;
		LARGE_INTEGER QPCLastTime;
		UINT64 QPCMaxDelta;

		UINT32 FrameCount;
		UINT32 FramesPerSecond;
		UINT32 FramesThisSecond;
		UINT64 QPCSecondCounter;
	} Timer = { 0 };

	static struct
	{
		vec3 InitialPosition;
		vec3 Position;
		float Yaw; // Relative to the +z axis.
		float Pitch; // Relative to the xz plane.
		vec3 LookDirection;
		vec3 UpDirection;
		float MoveSpeed;
		float TurnSpeed;
	} Camera = { 0 };

	static struct DxObjects *restrict DxObjects = NULL;

	static const int TICKS_PER_SECOND = 10000000;

	switch (Message)
	{
	case WM_INIT:
	{
		WindowDetails.Viewport.TopLeftX = 0.0f;
		WindowDetails.Viewport.TopLeftY = 0.0f;
		WindowDetails.Viewport.MinDepth = 0.0f;
		WindowDetails.Viewport.MaxDepth = 1.0f;

		WindowDetails.ScissorRect.left = 0;
		WindowDetails.ScissorRect.top = 0;

		QueryPerformanceFrequency(&Timer.QPCFrequency);
		QueryPerformanceCounter(&Timer.QPCLastTime);

		Timer.QPCMaxDelta = Timer.QPCFrequency.QuadPart / 10;

		glm_vec3_copy((vec3) { 0, 0, 0 }, Camera.InitialPosition);
		glm_vec3_copy((vec3) { 0, 0, 0 }, Camera.Position);
		Camera.Yaw = M_PI;
		Camera.Pitch = 0.0f;
		glm_vec3_copy((vec3) { 0, 0, -1 }, Camera.LookDirection);
		glm_vec3_copy((vec3) { 0, 1, 0 }, Camera.UpDirection);
		Camera.MoveSpeed = 20.0f;
		Camera.TurnSpeed = M_PI_2;

		glm_vec3_copy((vec3) { (CITY_COLUMN_COUNT / 2.0f)* CitySpacingInterval - (CitySpacingInterval / 2.0f), 15, 50 }, Camera.InitialPosition);
		glm_vec3_copy(Camera.InitialPosition, Camera.Position);
		Camera.Yaw = M_PI;
		Camera.Pitch = 0.0f;
		glm_vec3_copy((vec3) { 0, 0, -1 }, Camera.LookDirection);

		Camera.MoveSpeed = CitySpacingInterval * 2.0f;

		DxObjects = ((struct DxObjects*)wParam);
		break;
	}
	case WM_KEYDOWN:
		switch (wParam)
		{
		case 'W':
			KeyStates.w = true;
			break;
		case 'A':
			KeyStates.a = true;
			break;
		case 'S':
			KeyStates.s = true;
			break;
		case 'D':
			KeyStates.d = true;
			break;
		case VK_LEFT:
			KeyStates.Left = true;
			break;
		case VK_RIGHT:
			KeyStates.Right = true;
			break;
		case VK_UP:
			KeyStates.Up = true;
			break;
		case VK_DOWN:
			KeyStates.Down = true;
			break;
		case 'V':
			if (!(lParam & 1 << 30))
				WindowDetails.bVsync = !WindowDetails.bVsync;
			break;
		case VK_ESCAPE:
			glm_vec3_copy(Camera.InitialPosition, Camera.Position);
			Camera.Yaw = M_PI;
			Camera.Pitch = 0.0f;
			glm_vec3_copy((vec3){ 0, 0, -1 }, Camera.LookDirection);
			break;
		}
		break;
	case WM_KEYUP:
		switch (wParam)
		{
		case 'W':
			KeyStates.w = false;
			break;
		case 'A':
			KeyStates.a = false;
			break;
		case 'S':
			KeyStates.s = false;
			break;
		case 'D':
			KeyStates.d = false;
			break;
		case VK_LEFT:
			KeyStates.Left = false;
			break;
		case VK_RIGHT:
			KeyStates.Right = false;
			break;
		case VK_UP:
			KeyStates.Up = false;
			break;
		case VK_DOWN:
			KeyStates.Down = false;
			break;
		}
		break;
	case WM_SYSKEYDOWN:
		if (wParam == VK_RETURN && (lParam & 0x60000000) == 0x20000000)
		{
			WindowDetails.bFullScreen = !WindowDetails.bFullScreen;

			if (WindowDetails.bFullScreen)
			{
				THROW_ON_FALSE(SetWindowLongPtrW(Window, GWL_EXSTYLE, WS_EX_TOPMOST) != 0);
				THROW_ON_FALSE(SetWindowLongPtrW(Window, GWL_STYLE, 0) != 0);

				THROW_ON_FALSE(ShowWindow(Window, SW_SHOWMAXIMIZED));
			}
			else
			{
				THROW_ON_FALSE(SetWindowLongPtrW(Window, GWL_STYLE, WS_OVERLAPPEDWINDOW) != 0);
				THROW_ON_FALSE(SetWindowLongPtrW(Window, GWL_EXSTYLE, 0) != 0);

				THROW_ON_FALSE(ShowWindow(Window, SW_SHOWMAXIMIZED));
			}
		}
		break;
	case WM_SIZE:
		if (wParam == SIZE_MINIMIZED)
		{
			THROW_ON_FALSE(SetWindowLongPtrW(Window, GWLP_WNDPROC, (LONG_PTR)IdleProc) != 0);
			break;
		}

		if (WindowDetails.WindowWidth == LOWORD(lParam) && WindowDetails.WindowHeight == HIWORD(lParam))
			break;

		WindowDetails.WindowWidth = LOWORD(lParam);
		WindowDetails.WindowHeight = HIWORD(lParam);

		WindowDetails.AspectRatio = (float)WindowDetails.WindowWidth / (float)WindowDetails.WindowHeight;

		WindowDetails.Viewport.Width = WindowDetails.WindowWidth;
		WindowDetails.Viewport.Height = WindowDetails.WindowHeight;

		WindowDetails.ScissorRect.right = WindowDetails.WindowWidth;
		WindowDetails.ScissorRect.bottom = WindowDetails.WindowHeight;

		WaitForPreviousFrame(DxObjects);
		
		if (DxObjects->RenderTargets[0])
		{
			for (int i = 0; i < BUFFER_COUNT; i++)
			{
				THROW_ON_FAIL(ID3D12Resource_Release(DxObjects->RenderTargets[i]));
				DxObjects->FenceValue[i] = DxObjects->FenceValue[DxObjects->FrameIndex] + 1;
			}
		}

		THROW_ON_FAIL(IDXGISwapChain3_ResizeBuffers(DxObjects->SwapChain, BUFFER_COUNT, WindowDetails.WindowWidth, WindowDetails.WindowHeight, RTV_FORMAT, DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING));

		{
			D3D12_CPU_DESCRIPTOR_HANDLE RtvHandle = DxObjects->RtvHandle;
			for (int i = 0; i < BUFFER_COUNT; i++)
			{
				THROW_ON_FAIL(IDXGISwapChain3_GetBuffer(DxObjects->SwapChain, i, &IID_ID3D12Resource, &DxObjects->RenderTargets[i]));
				ID3D12Device10_CreateRenderTargetView(Device, DxObjects->RenderTargets[i], NULL, RtvHandle);
				RtvHandle.ptr += DxObjects->RtvDescriptorSize;
			}
		}

		if (DxObjects->DepthStencil)
		{
			THROW_ON_FAIL(ID3D12Resource_Release(DxObjects->DepthStencil));
		}

		{
			D3D12_HEAP_PROPERTIES HeapProperties = { 0 };
			HeapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
			HeapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
			HeapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

			D3D12_RESOURCE_DESC1 ResourceDesc = { 0 };
			ResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
			ResourceDesc.Alignment = 0;
			ResourceDesc.Width = WindowDetails.WindowWidth;
			ResourceDesc.Height = WindowDetails.WindowHeight;
			ResourceDesc.DepthOrArraySize = 1;
			ResourceDesc.MipLevels = 1;
			ResourceDesc.Format = DSV_FORMAT;
			ResourceDesc.SampleDesc.Count = 1;
			ResourceDesc.SampleDesc.Quality = 0;
			ResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
			ResourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;

			D3D12_CLEAR_VALUE ScreenClearValue = { 0 };
			ScreenClearValue.Format = DSV_FORMAT;
			ScreenClearValue.DepthStencil.Depth = 1.0f;
			ScreenClearValue.DepthStencil.Stencil = 0;

			THROW_ON_FAIL(ID3D12Device10_CreateCommittedResource3(Device, &HeapProperties, D3D12_HEAP_FLAG_NONE, &ResourceDesc, D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE, &ScreenClearValue, NULL, 0, NULL, &IID_ID3D12Resource, &DxObjects->DepthStencil));
		}

		{
			D3D12_DEPTH_STENCIL_VIEW_DESC DepthStencilViewDesc = { 0 };
			DepthStencilViewDesc.Format = DSV_FORMAT;
			DepthStencilViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
			DepthStencilViewDesc.Flags = D3D12_DSV_FLAG_NONE;
			ID3D12Device10_CreateDepthStencilView(Device, DxObjects->DepthStencil, &DepthStencilViewDesc, DxObjects->DsvHandle);
		}

		break;
	case WM_PAINT:
	{
		WaitForPreviousFrame(DxObjects);
		LARGE_INTEGER CurrentTime;
		QueryPerformanceCounter(&CurrentTime);

		UINT64 TimeDelta = CurrentTime.QuadPart - Timer.QPCLastTime.QuadPart;

		Timer.QPCLastTime = CurrentTime;
		Timer.QPCSecondCounter += TimeDelta;

		if (TimeDelta > Timer.QPCMaxDelta)
		{
			TimeDelta = Timer.QPCMaxDelta;
		}

		TimeDelta *= TICKS_PER_SECOND;
		TimeDelta /= Timer.QPCFrequency.QuadPart;

		UINT32 lastFrameCount = Timer.FrameCount;

		UINT64 ElapsedTicks = TimeDelta;
		Timer.FrameCount++;

		if (Timer.FrameCount != lastFrameCount)
		{
			Timer.FramesThisSecond++;
		}

		if (Timer.QPCSecondCounter >= (UINT64)Timer.QPCFrequency.QuadPart)
		{
			Timer.FramesPerSecond = Timer.FramesThisSecond;
			Timer.FramesThisSecond = 0;
			Timer.QPCSecondCounter %= Timer.QPCFrequency.QuadPart;
		}

		{
			wchar_t FPS[64];
			_snwprintf_s(FPS, ARRAYSIZE(FPS), _TRUNCATE, L"D3D12 Dynamic Indexing Sample: %ufps", Timer.FramesPerSecond);
			SetWindowTextW(Window, FPS);
		}

		DxObjects->FrameCounter++;

		vec3 Move = { 0, 0, 0 };

		if (KeyStates.a)
			Move[0] -= 1.0f;
		if (KeyStates.d)
			Move[0] += 1.0f;
		if (KeyStates.w)
			Move[2] -= 1.0f;
		if (KeyStates.s)
			Move[2] += 1.0f;

		if (fabs(Move[0]) > 0.1f && fabs(Move[2]) > 0.1f)
		{
			glm_vec3_normalize(Move);
		}

		const float ElapsedSeconds = (double)ElapsedTicks / TICKS_PER_SECOND;
		const float MoveInterval = Camera.MoveSpeed * ElapsedSeconds;
		const float RotateInterval = Camera.TurnSpeed * ElapsedSeconds;

		if (KeyStates.Left)
			Camera.Yaw += RotateInterval;
		if (KeyStates.Right)
			Camera.Yaw -= RotateInterval;
		if (KeyStates.Up)
			Camera.Pitch += RotateInterval;
		if (KeyStates.Down)
			Camera.Pitch -= RotateInterval;

		Camera.Pitch = min(Camera.Pitch, M_PI_4);
		Camera.Pitch = max(-M_PI_4, Camera.Pitch);

		float x = Move[0] * -cosf(Camera.Yaw) - Move[2] * sinf(Camera.Yaw);
		float z = Move[0] * sinf(Camera.Yaw) - Move[2] * cosf(Camera.Yaw);
		Camera.Position[0] += x * MoveInterval;
		Camera.Position[2] += z * MoveInterval;
	}

		{
			float r = cosf(Camera.Pitch);
			Camera.LookDirection[0] = r * sinf(Camera.Yaw);
			Camera.LookDirection[1] = sinf(Camera.Pitch);
			Camera.LookDirection[2] = r * cosf(Camera.Yaw);
		}

		{
			mat4 ViewMatrix;
			glm_look(Camera.Position, Camera.LookDirection, Camera.UpDirection, ViewMatrix);

			mat4 ProjectionMatrix;
			glm_perspective(0.8f, WindowDetails.AspectRatio, 1.0f, 1000.0f, ProjectionMatrix);

			for (int i = 0; i < CITY_MATERIAL_COUNT; i++)
			{
				mat4 ModelMatrix;
				glm_mat4_copy(DxObjects->FrameResources[DxObjects->FrameIndex].ModelMatrices[i], ModelMatrix);

				mat4 MVP;
				glm_mat4_copy(ProjectionMatrix, MVP);
				glm_mat4_mul(MVP, ViewMatrix, MVP);
				glm_mat4_mul(MVP, ModelMatrix, MVP);
				glm_mat4_transpose(MVP);

				memcpy(&DxObjects->FrameResources[DxObjects->FrameIndex].pConstantBuffers[i], &MVP, sizeof(MVP));
			}
		}

		THROW_ON_FAIL(ID3D12CommandAllocator_Reset(DxObjects->FrameResources[DxObjects->FrameIndex].CommandAllocator));

		THROW_ON_FAIL(ID3D12GraphicsCommandList7_Reset(DxObjects->CommandList, DxObjects->FrameResources[DxObjects->FrameIndex].CommandAllocator, DxObjects->PipelineState));

		ID3D12GraphicsCommandList7_SetGraphicsRootSignature(DxObjects->CommandList, DxObjects->RootSignature);

		{
			ID3D12DescriptorHeap* ppHeaps[] = { DxObjects->CbvSrvHeap, DxObjects->SamplerHeap };
			ID3D12GraphicsCommandList7_SetDescriptorHeaps(DxObjects->CommandList, ARRAYSIZE(ppHeaps), ppHeaps);
		}

		ID3D12GraphicsCommandList7_RSSetViewports(DxObjects->CommandList, 1, &WindowDetails.Viewport);
		ID3D12GraphicsCommandList7_RSSetScissorRects(DxObjects->CommandList, 1, &WindowDetails.ScissorRect);

		{
			D3D12_TEXTURE_BARRIER TextureBarrier = { 0 };
			TextureBarrier.SyncBefore = D3D12_BARRIER_SYNC_ALL;
			TextureBarrier.SyncAfter = D3D12_BARRIER_SYNC_RENDER_TARGET;
			TextureBarrier.AccessBefore = D3D12_BARRIER_ACCESS_COMMON;
			TextureBarrier.AccessAfter = D3D12_BARRIER_ACCESS_RENDER_TARGET;
			TextureBarrier.LayoutBefore = D3D12_BARRIER_LAYOUT_PRESENT;
			TextureBarrier.LayoutAfter = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
			TextureBarrier.pResource = DxObjects->RenderTargets[DxObjects->FrameIndex];
			TextureBarrier.Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;

			D3D12_BARRIER_GROUP ResourceBarrier = { 0 };
			ResourceBarrier.Type = D3D12_BARRIER_TYPE_TEXTURE;
			ResourceBarrier.NumBarriers = 1;
			ResourceBarrier.pTextureBarriers = &TextureBarrier;
			ID3D12GraphicsCommandList7_Barrier(DxObjects->CommandList, 1, &ResourceBarrier);
		}

		{
			D3D12_CPU_DESCRIPTOR_HANDLE RtvHandle = DxObjects->RtvHandle;
			RtvHandle.ptr += DxObjects->FrameIndex * DxObjects->RtvDescriptorSize;

			ID3D12GraphicsCommandList7_OMSetRenderTargets(DxObjects->CommandList, 1, &RtvHandle, FALSE, &DxObjects->DsvHandle);

			const float ClearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
			ID3D12GraphicsCommandList7_ClearRenderTargetView(DxObjects->CommandList, RtvHandle, ClearColor, 0, NULL);
			ID3D12GraphicsCommandList7_ClearDepthStencilView(DxObjects->CommandList, DxObjects->DsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, NULL);
		}

		if (bBundles)
		{
			ID3D12GraphicsCommandList7_ExecuteBundle(DxObjects->CommandList, DxObjects->FrameResources[DxObjects->FrameIndex].Bundle);
		}
		else
		{
			PopulateCommandList(
				&DxObjects->FrameResources[DxObjects->FrameIndex],
				DxObjects->CommandList,
				DxObjects->FrameIndex,
				&DxObjects->IndexBufferView,
				&DxObjects->VertexBufferView,
				DxObjects->CbvSrvHeap,
				DxObjects->CbvGpuHandle,
				DxObjects->CbvSrvDescriptorSize,
				DxObjects->SamplerHeap,
				DxObjects->SamplerGpuHandle,
				DxObjects->RootSignature);
		}

		{
			D3D12_TEXTURE_BARRIER TextureBarrier = { 0 };
			TextureBarrier.SyncBefore = D3D12_BARRIER_SYNC_RENDER_TARGET;
			TextureBarrier.SyncAfter = D3D12_BARRIER_SYNC_ALL;
			TextureBarrier.AccessBefore = D3D12_BARRIER_ACCESS_RENDER_TARGET;
			TextureBarrier.AccessAfter = D3D12_BARRIER_ACCESS_COMMON;
			TextureBarrier.LayoutBefore = D3D12_BARRIER_LAYOUT_RENDER_TARGET;
			TextureBarrier.LayoutAfter = D3D12_BARRIER_LAYOUT_PRESENT;
			TextureBarrier.pResource = DxObjects->RenderTargets[DxObjects->FrameIndex];
			TextureBarrier.Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;

			D3D12_BARRIER_GROUP ResourceBarrier = { 0 };
			ResourceBarrier.Type = D3D12_BARRIER_TYPE_TEXTURE;
			ResourceBarrier.NumBarriers = 1;
			ResourceBarrier.pTextureBarriers = &TextureBarrier;
			ID3D12GraphicsCommandList7_Barrier(DxObjects->CommandList, 1, &ResourceBarrier);
		}

		THROW_ON_FAIL(ID3D12GraphicsCommandList7_Close(DxObjects->CommandList));

		ID3D12CommandQueue_ExecuteCommandLists(DxObjects->CommandQueue, 1, &DxObjects->CommandList);
		
		THROW_ON_FAIL(IDXGISwapChain3_Present(DxObjects->SwapChain, WindowDetails.bVsync ? 1 : 0, WindowDetails.bVsync ? 0 : DXGI_PRESENT_ALLOW_TEARING));

		THROW_ON_FAIL(ID3D12CommandQueue_Signal(DxObjects->CommandQueue, DxObjects->Fence[DxObjects->FrameIndex], DxObjects->FenceValue[DxObjects->FrameIndex]));
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProcW(Window, Message, wParam, lParam);
	}
	return 0;
}

void PopulateCommandList(
	struct FrameResource* pFrameResource,
	ID3D12GraphicsCommandList7* pCommandList,
	UINT frameResourceIndex,
	D3D12_INDEX_BUFFER_VIEW* IndexBufferViewDesc,
	D3D12_VERTEX_BUFFER_VIEW* pVertexBufferViewDesc,
	ID3D12DescriptorHeap* pCbvSrvDescriptorHeap,
	D3D12_GPU_DESCRIPTOR_HANDLE CbvGpuHandle,
	UINT CbvSrvDescriptorSize,
	ID3D12DescriptorHeap* pSamplerDescriptorHeap,
	D3D12_GPU_DESCRIPTOR_HANDLE SamplerGpuHandle,
	ID3D12RootSignature* pRootSignature)
{
	ID3D12GraphicsCommandList7_SetGraphicsRootSignature(pCommandList, pRootSignature);

	{
		ID3D12DescriptorHeap* ppHeaps[] = { pCbvSrvDescriptorHeap, pSamplerDescriptorHeap };
		ID3D12GraphicsCommandList7_SetDescriptorHeaps(pCommandList, ARRAYSIZE(ppHeaps), ppHeaps);
	}

	ID3D12GraphicsCommandList7_IASetPrimitiveTopology(pCommandList, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ID3D12GraphicsCommandList7_IASetIndexBuffer(pCommandList, IndexBufferViewDesc);
	ID3D12GraphicsCommandList7_IASetVertexBuffers(pCommandList, 0, 1, pVertexBufferViewDesc);
	
	ID3D12GraphicsCommandList7_SetGraphicsRootDescriptorTable(pCommandList, 0, CbvGpuHandle);
	ID3D12GraphicsCommandList7_SetGraphicsRootDescriptorTable(pCommandList, 1, SamplerGpuHandle);

	const UINT FrameResourceDescriptorOffset = (CITY_MATERIAL_COUNT + 1) + (frameResourceIndex * CITY_ROW_COUNT * CITY_COLUMN_COUNT);

	D3D12_GPU_DESCRIPTOR_HANDLE CbvSrvHandle = CbvGpuHandle;
	CbvSrvHandle.ptr += FrameResourceDescriptorOffset * CbvSrvDescriptorSize;

	for (UINT i = 0; i < CITY_MATERIAL_COUNT; i++)
	{
		ID3D12GraphicsCommandList7_SetGraphicsRoot32BitConstant(pCommandList, 3, i, 0);

		ID3D12GraphicsCommandList7_SetGraphicsRootDescriptorTable(pCommandList, 2, CbvSrvHandle);
		CbvSrvHandle.ptr += CbvSrvDescriptorSize;

		ID3D12GraphicsCommandList7_DrawIndexedInstanced(pCommandList, INDEX_COUNT, 1, 0, 0, 0);
	}
}

inline void WaitForPreviousFrame(struct DxObjects* restrict DxObjects)
{
	DxObjects->FrameIndex = IDXGISwapChain3_GetCurrentBackBufferIndex(DxObjects->SwapChain);
	THROW_ON_FAIL(ID3D12CommandQueue_Signal(DxObjects->CommandQueue, DxObjects->Fence[DxObjects->FrameIndex], ++DxObjects->FenceValue[DxObjects->FrameIndex]));

	if (ID3D12Fence_GetCompletedValue(DxObjects->Fence[DxObjects->FrameIndex]) < DxObjects->FenceValue[DxObjects->FrameIndex])
	{
		THROW_ON_FAIL(ID3D12Fence_SetEventOnCompletion(DxObjects->Fence[DxObjects->FrameIndex], DxObjects->FenceValue[DxObjects->FrameIndex], DxObjects->FenceEvent));
		THROW_ON_FALSE(WaitForSingleObject(DxObjects->FenceEvent, INFINITE) == WAIT_OBJECT_0);
	}
}
