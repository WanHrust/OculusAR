/*
This is free and unencumbered software released into the public domain.
Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a compiled
binary, for any purpose, commercial or non-commercial, and by any
means.
In jurisdictions that recognize copyright laws, the author or authors
of this software dedicate any and all copyright interest in the
software to the public domain. We make this dedication for the benefit
of the public at large and to the detriment of our heirs and
successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this
software under copyright law.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.
For more information, please refer to <http://unlicense.org>
*/

/*
This example demonstrates how to get a very simple scene running in your HMD using Direct3D 11,
with as little clutter as possible. Error handling and architecture is largely ignored. You
probably do not want to base your own project on this code, or at least replace it as you go.
https://github.com/poppeman/SimpleOVR
In order to get this compiling, you will need to do some changes:
Visual Studio 2013:
* Right click the project in Solution Explorer and click "Properties".
* Select VC++ Directories.
* Change the Include Directories and Library Directories to (also) point at the appropriate locations.
Example (ovr_sdk_win_0.4.3.zip extracted in C:\Src):
Include Directories: C:\Src\OculusSDK\LibOVR\Include;C:\Src\OculusSDK\LibOVR\Src;$(IncludePath)
Library Directories: C:\Src\OculusSDK\LibOVR\Lib\Win32\VS2013;$(LibraryPath)
This demo ONLY handles Direct HMD Access mode.
Known issues:
* Running with DWM disabled ("Basic Theme") will eat CPU and possibly result in low FPS, at
least with mirroring enabled.
*/



// Prevent windows.h from breaking std::min and std::max.
#define NOMINMAX

// D3D support requires you to define which D3D version you use at compile time.
#define OVR_D3D_VERSION 11
#include <d3d11.h>
#include <d3dcompiler.h>
#include <OVR.h>
#include <OVR_CAPI_D3D.h>
#include <D3DX11.h>
#include <D3Dcompiler.h>

// Entrypoint
//#include <EntryPoint.h>     //!<Cross platform for common entry point

#include <ovrvision.h>        //Ovrvision SDK

#include <algorithm>
#include <vector>
#include <xnamath.h>
#include "Log.h"
#include "InputMgr.h"
#include "MyInput.h"
using namespace D3D11Framework;

const LPWSTR ClassName = L"SimpleOVR_D3D11";


//Oculus Rift screen size
#define RIFTSCREEN_WIDTH    (1920)
#define RIFTPSCREEN_HEIGHT  (1080)

//Application screen size
#define APPSCREEN_WIDTH     (1280)
#define APPSCREEN_HEIGHT    (800)

//Camera image size
#define CAM_WIDTH           640
#define CAM_HEIGHT          480


//Interocular distance
float eye_interocular = 0.0f;
//Eye scale
float eye_scale = 0.9f;
//Quality
int processer_quality = OVR::OV_PSQT_HIGH;
//use AR
bool useOvrvisionAR = false;

InputMgr *inputMgr = nullptr;
MyInput *input = nullptr;
float scaleAmount = 1.0f;
OVR::Vector3f translateAmount = OVR::Vector3f(0.0f, 0.0f, 0.0f);
/*
Number of rendered pixels per display pixel. Generally you want this set at 1.0, but you
can gain some performance by setting it lower in exchange for a more blurry result.
*/
const float PixelsPerDisplayPixel = 1.0f;
const int MultisampleCount = 1; // Set to 1 to disable multisampling

// Commonly used vectors.
const OVR::Vector3f RightVector(1.0f, 0.0f, 0.0f);
const OVR::Vector3f UpVector(0.0f, 1.0f, 0.0f);
const OVR::Vector3f ForwardVector(0.0f, 0.0f, -1.0f);


ID3D11ShaderResourceView *m_pTextureRV = nullptr;
ID3D11ShaderResourceView *m_pTextureRV2 = nullptr;

ID3D11SamplerState *m_pSamplerLinear = nullptr;

// Position and angle of the player's body. In a real project these are probably not constant,
// but we're keeping things simple here.
const OVR::Vector3f BodyPosition{ 0.5f, 0.5f, 0 };
const OVR::Anglef BodyYaw{ 0.9f };


ID3D11Buffer* SetupScene(ID3D11Device* d3dDevice, ID3D11DeviceContext* d3dContext);
void DestroyScene();

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
	case WM_CLOSE:
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	case WM_MOUSEMOVE: case WM_LBUTTONUP: case WM_LBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDOWN: case WM_MOUSEWHEEL: case WM_KEYDOWN: case WM_KEYUP:
		if (inputMgr)
			inputMgr->Run(msg, wParam, lParam);
		scaleAmount = input->getScale();
		translateAmount = input->translate;

		return 0;
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}

/*
INTERESTING PART BEGINS HERE
*/

int main() {
	ovrEyeRenderDesc vrEyeRenderDesc[2];
	ovrRecti vrEyeRenderViewport[2];
	ovrHmd vrHmd = nullptr;
	ovrFovPort vrEyeFov[2];
	ovrSizei renderTargetSize;
	ovrD3D11Texture vrEyeTexture[2];
	ovrD3D11Config vrRenderConfiguration;

	//Objects
	OVR::Ovrvision* g_pOvrvision;

	ID3D11Texture2D* d3dDepthStencilTexture = nullptr;
	ID3D11DepthStencilView* d3dDepthStencilView = nullptr;
	ID3D11RenderTargetView* d3dBackBufferRenderTargetView = nullptr;

	// Texture used for main rendering. LibOVR will use this as the source when rendering the final
	// distorted view to the HMD.
	ID3D11Texture2D* d3dEyeTexture = nullptr;
	ID3D11RenderTargetView* d3dEyeTextureRenderTargetView = nullptr;
	ID3D11ShaderResourceView* d3dEyeTextureShaderResourceView = nullptr;

	/*
	We need an additional intermediary rendertarget if (and only if) we use multisampling.
	Without multisampling the rendering process is like this:
	Geometry ----> Eye texture ----> Back buffer
	With multisampling we must add one step:
	Geometry ----> Eye texture ----> Intermediary ----> Back buffer
	All this may change in later
	*/
	ID3D11Texture2D* d3dIntermediaryTexture = nullptr;
	ID3D11RenderTargetView* d3dIntermediaryTextureRenderTargetView = nullptr;
	ID3D11ShaderResourceView* d3dIntermediaryTextureShaderResourceView = nullptr;

	/*
	This call prevents the window to get stretched on High-DPI systems. Alternatively you can
	do this by modifying the application manifest file. It is not terribly important though,
	especially if you disable mirroring.
	Further reading:
	http://msdn.microsoft.com/en-us/library/windows/desktop/dn469266%28v=vs.85%29.aspx
	*/
	SetProcessDPIAware();


	inputMgr = new InputMgr();
	input = new MyInput();
	inputMgr->AddListener(input);

	/*
	LibOVR Initialization part.
	Beware, LibOVR may be a bit picky about when you perform the various steps. ovr_Initialize
	must be called before you start initializing Direct3D.
	*/

	ovr_Initialize();

	vrHmd = ovrHmd_Create(0);
	if (vrHmd == nullptr) {
		// Forgetting to turn on the HMD is fairly common so we make an exception here and actually
		// add some error handling.
		MessageBox(nullptr, L"Failed initializing HMD, make sure it is connected and turned on.", L"LibOVR error", MB_OK);
		ovr_Shutdown();
		return EXIT_FAILURE;
	}

	// We'll request orientation and position tracking, but not require either. Adjust according to your needs.
	ovrHmd_ConfigureTracking(vrHmd, ovrTrackingCap_Orientation | ovrTrackingCap_Position, 0);

	// Fetch the texture sizes needed for the eye buffers.
	// We'll be using a single texture for both eyes, so we'll figure out how large that texture needs to be.
	auto ovrEyeDimsLeft = ovrHmd_GetFovTextureSize(vrHmd, ovrEye_Left, vrHmd->DefaultEyeFov[0], PixelsPerDisplayPixel);
	auto ovrEyeDimsRight = ovrHmd_GetFovTextureSize(vrHmd, ovrEye_Right, vrHmd->DefaultEyeFov[0], PixelsPerDisplayPixel);

	// We ARE making an assumption here that both eye buffers have the same width, as this is the case for DK2.
	renderTargetSize.w = ovrEyeDimsLeft.w + ovrEyeDimsRight.w;
	renderTargetSize.h = std::max(ovrEyeDimsLeft.h, ovrEyeDimsRight.h);

	// View ports for each eye. We'll be using a single a single render target and allocate half of it to each eye.
	vrEyeRenderViewport[0].Pos = { 0, 0 };
	vrEyeRenderViewport[0].Size = { renderTargetSize.w / 2, renderTargetSize.h };
	vrEyeRenderViewport[1].Pos = { (renderTargetSize.w + 1) / 2, 0 };
	vrEyeRenderViewport[1].Size = vrEyeRenderViewport[0].Size;


	// FOV for each eye.
	vrEyeFov[0] = vrHmd->DefaultEyeFov[0];
	vrEyeFov[1] = vrHmd->DefaultEyeFov[1];


	// Windows-specific initialization part.
	WNDCLASSEX wcx;
	ZeroMemory(&wcx, sizeof(wcx));
	wcx.cbSize = sizeof(wcx);
	wcx.lpszClassName = ClassName;
	wcx.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcx.hInstance = GetModuleHandle(nullptr); // Tip: GetModuleHandle(nullptr) works just as well.
	wcx.lpfnWndProc = WndProc;

	RegisterClassEx(&wcx);

	/*
	The dimensions of the window does not need to match the dimensions of the actual HMD
	output.
	*/
	auto hwnd = CreateWindowW(
		ClassName,
		L"SimpleOVR - D3D11",
		WS_OVERLAPPEDWINDOW | WS_VISIBLE,
		0, 0,
		1280, 720,
		nullptr,
		nullptr,
		nullptr,
		nullptr);

	//Create ovrvision object
	g_pOvrvision = new OVR::Ovrvision();
	if (vrHmd->Type == ovrHmd_DK2) {
		//Rift DK2
		g_pOvrvision->Open(0, OVR::OV_CAMVGA_FULL);  //Open
	}
	else {
		//Rift DK1
		g_pOvrvision->Open(0, OVR::OV_CAMVGA_FULL, OVR::OV_HMD_OCULUS_DK1);  //Open
	}

	/*
	D3D11 initialization.
	This example uses no fancy features, so we only require Direct3D 10.1 capable hardware.
	If you attempt to target anything less you might start getting crashes in LibOVR or various
	D3D-related errors.
	*/
	D3D_FEATURE_LEVEL requestedLevels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };
	D3D_FEATURE_LEVEL obtainedLevel;
	ID3D11Device* d3dDevice = nullptr;
	ID3D11DeviceContext* d3dContext = nullptr;

	DXGI_SWAP_CHAIN_DESC scd;
	ZeroMemory(&scd, sizeof(scd));
	scd.BufferCount = 1;
	scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	scd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	scd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;

	scd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	scd.OutputWindow = hwnd;
	scd.SampleDesc.Count = MultisampleCount;
	scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	scd.Windowed = false;

	// NOTE: LibOVR 0.4.3 requires that the width and height for the backbuffer is set even if
	// you use windowed mode, despite being optional according to the D3D11 documentation.
	scd.BufferDesc.Width = vrHmd->Resolution.w;
	scd.BufferDesc.Height = vrHmd->Resolution.h;
	scd.BufferDesc.RefreshRate.Numerator = 0;
	scd.BufferDesc.RefreshRate.Denominator = 1;

	UINT createFlags = 0;
#ifdef _DEBUG
	// This flag gives you some quite wonderful debug text. Not wonderful for performance, though!
	createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	IDXGISwapChain* d3dSwapChain = 0;

	D3D11CreateDeviceAndSwapChain(
		nullptr,
		D3D_DRIVER_TYPE_HARDWARE,
		nullptr,
		createFlags,
		requestedLevels,
		sizeof(requestedLevels) / sizeof(D3D_FEATURE_LEVEL),
		D3D11_SDK_VERSION,
		&scd,
		&d3dSwapChain,
		&d3dDevice,
		&obtainedLevel,
		&d3dContext);

	// Create a render target view for the backbuffer. This will be used during rendering when we
	// actually render the eye buffers to the HMD.
	ID3D11Texture2D* pBackBuffer = nullptr;
	d3dSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
	d3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &d3dBackBufferRenderTargetView);
	pBackBuffer->Release();

	// We don't get a depth buffer by default, and you'll probably want one of those.
	D3D11_TEXTURE2D_DESC descDepth;
	ZeroMemory(&descDepth, sizeof(descDepth));
	descDepth.Width = renderTargetSize.w;
	descDepth.Height = renderTargetSize.h;
	descDepth.MipLevels = 1;
	descDepth.ArraySize = 1;
	descDepth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	descDepth.SampleDesc.Count = MultisampleCount;
	d3dDevice->CreateTexture2D(&descDepth, nullptr, &d3dDepthStencilTexture);

	D3D11_DEPTH_STENCIL_VIEW_DESC descStencilView;
	ZeroMemory(&descStencilView, sizeof(descStencilView));
	descStencilView.Format = descDepth.Format;
	descStencilView.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS;

	d3dDevice->CreateDepthStencilView(d3dDepthStencilTexture, &descStencilView, &d3dDepthStencilView);

	// Get ovrvision image
	g_pOvrvision->PreStoreCamData();	//renderer
	unsigned char* p = g_pOvrvision->GetCamImage(OVR::OV_CAMEYE_LEFT, (OVR::OvPSQuality)processer_quality);
	unsigned char* p2 = g_pOvrvision->GetCamImage(OVR::OV_CAMEYE_RIGHT, (OVR::OvPSQuality)processer_quality);
	
	unsigned char *texArray = (unsigned char *)malloc(4 * 2364 * 1461 * sizeof(unsigned char));

	for (int i = 0; i < 4 * 2364 * 1461; i++) {
		
		if (i >= 4 * 640 * 480)
		{
			if (i % 4 == 3){
				texArray[i] = 0;
			}
			else
			{
				texArray[i] = (unsigned char)125;
			}
		}
		else
		{

			if (i % 4 == 3){
				texArray[i] = 0;
			}
			else
			{
				texArray[i] = p[i];
			}
		}
	}

	//int texArraysize = texArray.
	D3D11_TEXTURE2D_DESC desc;
	ZeroMemory(&desc, sizeof(desc));
	desc.Width = renderTargetSize.w;
	desc.Height = renderTargetSize.h;
	desc.MipLevels = desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	desc.MiscFlags = 0;

	ID3D11Texture2D *pTexture = NULL;

	D3D11_SUBRESOURCE_DATA TexInitData;
	ZeroMemory(&TexInitData, sizeof(D3D11_SUBRESOURCE_DATA));
	TexInitData.pSysMem = texArray;
	TexInitData.SysMemPitch = static_cast<UINT>(2364 * 3);
	TexInitData.SysMemSlicePitch = static_cast<UINT>(3 * 2364 * 1461 * sizeof(unsigned char));

	d3dDevice->CreateTexture2D(&desc, &TexInitData, &d3dEyeTexture);
	d3dDevice->CreateShaderResourceView(d3dEyeTexture, nullptr, &d3dEyeTextureShaderResourceView);
	d3dDevice->CreateRenderTargetView(d3dEyeTexture, nullptr, &d3dEyeTextureRenderTargetView);

	/*
	// Allocate a texture that will hold both (undistorted) eye views. Later we'll let LibOVR use this texture
	// to render the final distorted view to the HMD.
	D3D11_TEXTURE2D_DESC texdesc;
	ZeroMemory(&texdesc, sizeof(texdesc));
	texdesc.Width = renderTargetSize.w;
	texdesc.Height = renderTargetSize.h;
	texdesc.MipLevels = 1;
	texdesc.ArraySize = 1;
	texdesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	texdesc.SampleDesc.Count = MultisampleCount;
	texdesc.Usage = D3D11_USAGE_DEFAULT;
	texdesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	d3dDevice->CreateTexture2D(&texdesc, nullptr, &d3dEyeTexture);
	d3dDevice->CreateShaderResourceView(d3dEyeTexture, nullptr, &d3dEyeTextureShaderResourceView);
	d3dDevice->CreateRenderTargetView(d3dEyeTexture, nullptr, &d3dEyeTextureRenderTargetView);
	*/
	if (MultisampleCount > 1) {
		// This render target is ONLY used for multisampling. More comments up at the variable declarations.
		D3D11_TEXTURE2D_DESC texdesc;
		ZeroMemory(&texdesc, sizeof(texdesc));
		texdesc.Width = renderTargetSize.w;
		texdesc.Height = renderTargetSize.h;
		texdesc.MipLevels = 1;
		texdesc.ArraySize = 1;
		texdesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		texdesc.SampleDesc.Count = 1; // NOT multisampled. We resolve the multisampled rendertarget to this one.
		texdesc.SampleDesc.Quality = 0;
		texdesc.Usage = D3D11_USAGE_DEFAULT;
		texdesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		texdesc.CPUAccessFlags = 0;
		texdesc.MiscFlags = 0;
		d3dDevice->CreateTexture2D(&texdesc, nullptr, &d3dIntermediaryTexture);
		d3dDevice->CreateShaderResourceView(d3dIntermediaryTexture, nullptr, &d3dIntermediaryTextureShaderResourceView);
		d3dDevice->CreateRenderTargetView(d3dIntermediaryTexture, nullptr, &d3dIntermediaryTextureRenderTargetView);
	}

	// We'll let LibOVR take care of the distortion rendering for us, so we'll let it know where it
	// can find the undistorted eye buffers. We are sharing a single texture between both eyes.
	vrEyeTexture[0].D3D11.Header.API = ovrRenderAPI_D3D11;
	vrEyeTexture[0].D3D11.Header.TextureSize = renderTargetSize;
	vrEyeTexture[0].D3D11.Header.RenderViewport = vrEyeRenderViewport[0];

	// If we use multisampling we're actually rendering from the intermediary texture instead
	if (MultisampleCount > 1) {
		vrEyeTexture[0].D3D11.pSRView = d3dIntermediaryTextureShaderResourceView;
		vrEyeTexture[0].D3D11.pTexture = d3dIntermediaryTexture;
	}
	else {
		vrEyeTexture[0].D3D11.pSRView = d3dEyeTextureShaderResourceView;
		vrEyeTexture[0].D3D11.pTexture = d3dEyeTexture;
	}

	// Right eye uses the same texture, but different rendering viewport.
	vrEyeTexture[1] = vrEyeTexture[0];
	vrEyeTexture[1].D3D11.Header.RenderViewport = vrEyeRenderViewport[1];


	vrRenderConfiguration.D3D11.Header.API = ovrRenderAPI_D3D11;
	vrRenderConfiguration.D3D11.Header.RTSize = vrHmd->Resolution;
	vrRenderConfiguration.D3D11.pDevice = d3dDevice;
	vrRenderConfiguration.D3D11.pDeviceContext = d3dContext;
	vrRenderConfiguration.D3D11.pSwapChain = d3dSwapChain;
	vrRenderConfiguration.D3D11.pBackBufferRT = d3dBackBufferRenderTargetView;
	// NOTE: Header.Multisample does not seem to be used as of 0.4.3, so feel free to ignore it for now.
	vrRenderConfiguration.D3D11.Header.Multisample = MultisampleCount;

	ovrHmd_ConfigureRendering(vrHmd, &vrRenderConfiguration.Config, ovrDistortionCap_Chromatic | ovrDistortionCap_TimeWarp | ovrDistortionCap_Overdrive | ovrDistortionCap_Vignette, vrEyeFov, vrEyeRenderDesc);

	// This line can be skipped if the defaults are good enough for you.
	ovrHmd_SetEnabledCaps(vrHmd, ovrHmdCap_LowPersistence | ovrHmdCap_DynamicPrediction | ovrHmdCap_NoMirrorToWindow);

	// This is the magic part that enabled Direct HMD Access mode. Currently (0.4.3) it only works on Windows.
	ovrHmd_AttachToWindow(vrHmd, hwnd, nullptr, nullptr);

	ovrVector3f vrHmdToEyeViewOffset[2] = {
		vrEyeRenderDesc[0].HmdToEyeViewOffset,
		vrEyeRenderDesc[1].HmdToEyeViewOffset
	};

	// Finally we'll create everything we need for a very simple scene. This is probably not very
	// interesting so I have hidden it in a separate function.
	auto d3dConstantBuffer = SetupScene(d3dDevice, d3dContext);



	bool keepRunning = true;
	while (keepRunning) {
		MSG msg;
		while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) {
				keepRunning = false;
			}

			// Pressing a key will cause a recenter and attempt to dismiss the health warning.
			// Many other VR applications use F12 for recentering.
			if (msg.message == WM_KEYDOWN) {
				if (input->recenter == true)
					ovrHmd_RecenterPose(vrHmd);
				ovrHmd_DismissHSWDisplay(vrHmd);
			}

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		// Get ovrvision image
		g_pOvrvision->PreStoreCamData();	//renderer
		unsigned char* p = g_pOvrvision->GetCamImage(OVR::OV_CAMEYE_LEFT, (OVR::OvPSQuality)processer_quality);
		unsigned char* p2 = g_pOvrvision->GetCamImage(OVR::OV_CAMEYE_RIGHT, (OVR::OvPSQuality)processer_quality);


		// Rendering part
		ovrHmd_BeginFrame(vrHmd, 0);

		ovrPosef vrEyeRenderPose[2];
		ovrTrackingState hmdTrackingState;
		ovrHmd_GetEyePoses(vrHmd, 0, vrHmdToEyeViewOffset, vrEyeRenderPose, &hmdTrackingState);

		float f[] = { 0.22f, 0.23f, 0.29f, 1 };
		d3dContext->ClearRenderTargetView(d3dEyeTextureRenderTargetView, f);
		d3dContext->ClearDepthStencilView(d3dDepthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1, 0);

		// We use one single render target for both eyes.
		d3dContext->OMSetRenderTargets(1, &d3dEyeTextureRenderTargetView, d3dDepthStencilView);

		// We'll assume people have at most two eyes.
		for (int i = 0; i < 2; i++) {
			// The HMD might want us to render each eye in a specific order for best result.
			auto eye = vrHmd->EyeRenderOrder[i];

			// Use the viewport for the current eye
			D3D11_VIEWPORT vp;
			vp.Width = static_cast<float>(vrEyeRenderViewport[eye].Size.w);
			vp.Height = static_cast<float>(vrEyeRenderViewport[eye].Size.h);
			vp.TopLeftX = static_cast<float>(vrEyeRenderViewport[eye].Pos.x);
			vp.TopLeftY = static_cast<float>(vrEyeRenderViewport[eye].Pos.y);
			vp.MinDepth = 0.0f;
			vp.MaxDepth = 1.0f;
			d3dContext->RSSetViewports(1, &vp);

			// All left now is to render the scene. 
			/*
			OVR::Matrix4f scale = OVR::Matrix4f::Scaling(0.3f);
			OVR::Matrix4f translate = OVR::Matrix4f::Translation(0.0f, 0.0f, -5.0f);
			OVR::Matrix4f result = scale*translate;
			ovrMatrix4f transposedScaling = result.Transposed();
			D3D11_MAPPED_SUBRESOURCE d3dMappedStatus2;
			auto ms2 = d3dContext->Map(d3dConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &d3dMappedStatus2);
			std::memcpy(d3dMappedStatus2.pData, &transposedScaling, sizeof(float)* 16);
			d3dContext->Unmap(d3dConstantBuffer, 0);
			*/



			// Calculate projection and view for the current eye. You'll probably replace all of this in
			// your own project.
			OVR::Posef currentEyePose = vrEyeRenderPose[eye];
			OVR::Matrix4f projection = ovrMatrix4f_Projection(vrEyeRenderDesc[eye].Fov, 0.01f, 10000.0f, true);
			OVR::Quatf quatBodyRotation = OVR::Quatf(UpVector, BodyYaw.Get());
			auto worldPose = OVR::Posef(
				quatBodyRotation * currentEyePose.Rotation, // Final rotation (body AND head)
				BodyPosition + quatBodyRotation.Rotate(currentEyePose.Translation) // Final position (body AND eye)
				);

			OVR::Matrix4f scale = OVR::Matrix4f::Scaling(scaleAmount);
			OVR::Matrix4f translate = OVR::Matrix4f::Translation(translateAmount);
			OVR::Matrix4f rotate = OVR::Matrix4f::RotationY(0);
			OVR::Matrix4f cubeFinalTransform = translate*scale*rotate;

			d3dContext->PSSetShaderResources(0, 1, &m_pTextureRV);
			d3dContext->PSSetSamplers(0, 1, &m_pSamplerLinear);


			auto up = worldPose.Rotation.Rotate(UpVector);
			auto forward = worldPose.Rotation.Rotate(ForwardVector);

			OVR::Matrix4f view = OVR::Matrix4f::LookAtRH(worldPose.Translation, worldPose.Translation + forward, up);
			//view = OVR::Matrix4f::Identity();
			OVR::Matrix4f matrixViewProjection = projection  * cubeFinalTransform;
			//OVR::Matrix4f matrixViewProjection = projection * view * cubeFinalTransform;

			//ovrMatrix4f scale = 
			// Send the View-Projection matrix to the Vertex Shader.
			// The shader only expects the matrix so we're taking the quick and dirty approach.
			ovrMatrix4f transposedMvp = matrixViewProjection.Transposed();
			D3D11_MAPPED_SUBRESOURCE d3dMappedStatus;
			auto ms = d3dContext->Map(d3dConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &d3dMappedStatus);
			std::memcpy(d3dMappedStatus.pData, &transposedMvp, sizeof(float)* 16);
			d3dContext->Unmap(d3dConstantBuffer, 0);

			d3dContext->DrawIndexed(6, 0, 0);

			translate = OVR::Matrix4f::Translation(0.0f, 0.0f, -2.0f);
			scale = OVR::Matrix4f::Scaling(scaleAmount);
			rotate = OVR::Matrix4f::RotationY(0);

			d3dContext->PSSetShaderResources(0, 1, &m_pTextureRV2);
			d3dContext->PSSetSamplers(0, 1, &m_pSamplerLinear);

			cubeFinalTransform = translate*scale*rotate;


			matrixViewProjection = projection * view * cubeFinalTransform;
			transposedMvp = matrixViewProjection.Transposed();
			ms = d3dContext->Map(d3dConstantBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &d3dMappedStatus);
			std::memcpy(d3dMappedStatus.pData, &transposedMvp, sizeof(float)* 16);
			d3dContext->Unmap(d3dConstantBuffer, 0);

			d3dContext->DrawIndexed(6, 0, 0);
		}

		if (MultisampleCount > 1) {
			d3dContext->ResolveSubresource(d3dIntermediaryTexture, 0, d3dEyeTexture, 0, DXGI_FORMAT_R8G8B8A8_UNORM);
		}

		/*
		Finish the current frame and send it to the HMD. swapChain->Present is called
		automatically inside this function.
		*/
		ovrHmd_EndFrame(vrHmd, vrEyeRenderPose, &vrEyeTexture[0].Texture);
	}


	/*
	Cleanup part.
	*/
	DestroyScene();
	if (d3dIntermediaryTextureShaderResourceView != nullptr) {
		d3dIntermediaryTextureShaderResourceView->Release();
	}
	if (d3dIntermediaryTextureRenderTargetView != nullptr) {
		d3dIntermediaryTextureRenderTargetView->Release();
	}
	if (d3dIntermediaryTexture != nullptr) {
		d3dIntermediaryTexture->Release();
	}

	//Clean up Wizapply library
	delete g_pOvrvision;


	/*------------------------------------------------------------------*/

	d3dDepthStencilView->Release();
	d3dDepthStencilTexture->Release();
	d3dEyeTextureRenderTargetView->Release();
	d3dEyeTextureShaderResourceView->Release();
	d3dEyeTexture->Release();
	d3dBackBufferRenderTargetView->Release();
	d3dSwapChain->Release();
	d3dContext->Release();
	d3dDevice->Release();
	ovrHmd_Destroy(vrHmd);
	ovr_Shutdown();

	return EXIT_SUCCESS;
}

/*
Below is a bunch of code that prepares the scene. It has very little to do with the actual VR,
but this example would be fairly boring without anything to look at.
*/

const char* VertexShaderCode =
"cbuffer Constants {"
"   float4x4 mvp;"
"};"
"struct VS_OUTPUT"
"{"
"   float4 Pos : SV_POSITION; "
"   float4 Color : COLOR0;"
"};"
"VS_OUTPUT main(float4 Pos : POSITION, float4 Color : Color) {"
"   VS_OUTPUT output = (VS_OUTPUT)0;"
"   output.Pos = mul(mvp, Pos);"
"   output.Color = Color;"
"   return output;"
"}";

const char* PixelShaderCode =
"struct VS_OUTPUT"
"{"
"float4 Pos : SV_POSITION; "
"   float4 Color : COLOR0;"
"}; "
"float4 main(VS_OUTPUT input) : SV_Target"
"{"
"   return input.Color;"
"}";

/*
struct Vertex {
float Position[3];
};
*/
struct SimpleVertex
{
	XMFLOAT3 Pos;
	XMFLOAT2 Tex;
};

ID3D11InputLayout* d3dInputLayout = nullptr;
ID3D11VertexShader* d3dVertexShader = nullptr;
ID3D11PixelShader* d3dPixelShader = nullptr;
ID3D11Buffer* d3dConstantBuffer = nullptr;
ID3D11Buffer* d3dVertexBuffer = nullptr;



ID3D11Buffer* SetupScene(ID3D11Device* d3dDevice, ID3D11DeviceContext* d3dContext) {
	ID3D10Blob* d3dBlobVertexShader = nullptr;
	ID3D10Blob* d3dBlobPixelShader = nullptr;

	// The vertices of our scene. Perhaps not very exciting.
	/*std::vector<Vertex> vertices = {
	Vertex{ { -1, -1, -0.5 } },
	Vertex{ { -1, 1, -1.5 } },
	Vertex{ { 1, -1, -0.5 } },
	};


	SimpleVertex vertices[] =
	{
	{ XMFLOAT3(-1.0f, 1.0f, -1.0f), XMFLOAT2(0.0f, 0.0f) },
	{ XMFLOAT3(1.0f, 1.0f, -1.0f), XMFLOAT2(1.0f, 0.0f) },
	{ XMFLOAT3(1.0f, 1.0f, 1.0f), XMFLOAT2(1.0f, 1.0f) },
	{ XMFLOAT3(-1.0f, 1.0f, 1.0f), XMFLOAT2(0.0f, 1.0f) },

	{ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT2(0.0f, 0.0f) },
	{ XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT2(1.0f, 0.0f) },
	{ XMFLOAT3(1.0f, -1.0f, 1.0f), XMFLOAT2(1.0f, 1.0f) },
	{ XMFLOAT3(-1.0f, -1.0f, 1.0f), XMFLOAT2(0.0f, 1.0f) },

	{ XMFLOAT3(-1.0f, -1.0f, 1.0f), XMFLOAT2(0.0f, 0.0f) },
	{ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT2(1.0f, 0.0f) },
	{ XMFLOAT3(-1.0f, 1.0f, -1.0f), XMFLOAT2(1.0f, 1.0f) },
	{ XMFLOAT3(-1.0f, 1.0f, 1.0f), XMFLOAT2(0.0f, 1.0f) },

	{ XMFLOAT3(1.0f, -1.0f, 1.0f), XMFLOAT2(0.0f, 0.0f) },
	{ XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT2(1.0f, 0.0f) },
	{ XMFLOAT3(1.0f, 1.0f, -1.0f), XMFLOAT2(1.0f, 1.0f) },
	{ XMFLOAT3(1.0f, 1.0f, 1.0f), XMFLOAT2(0.0f, 1.0f) },

	{ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT2(0.0f, 0.0f) },
	{ XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT2(1.0f, 0.0f) },
	{ XMFLOAT3(1.0f, 1.0f, -1.0f), XMFLOAT2(1.0f, 1.0f) },
	{ XMFLOAT3(-1.0f, 1.0f, -1.0f), XMFLOAT2(0.0f, 1.0f) },

	{ XMFLOAT3(-1.0f, -1.0f, 1.0f), XMFLOAT2(0.0f, 0.0f) },
	{ XMFLOAT3(1.0f, -1.0f, 1.0f), XMFLOAT2(1.0f, 0.0f) },
	{ XMFLOAT3(1.0f, 1.0f, 1.0f), XMFLOAT2(1.0f, 1.0f) },
	{ XMFLOAT3(-1.0f, 1.0f, 1.0f), XMFLOAT2(0.0f, 1.0f) },
	};



	WORD indices[] =
	{
	3, 1, 0,
	2, 1, 3,

	0, 5, 4,
	1, 5, 0,

	3, 4, 7,
	0, 4, 3,

	1, 6, 5,
	2, 6, 1,

	2, 7, 6,
	3, 7, 2,

	6, 4, 5,
	7, 4, 6,
	};
	*/

	// Simple array of type SimpleVertex which stores information about vertices of each plane (in this case we have 2 planes) 
	SimpleVertex vertices[] =
	{
		{ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT2(0.0f, 1.0f) },
		{ XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT2(1.0f, 1.0f) },
		{ XMFLOAT3(1.0f, 1.0f, -1.0f), XMFLOAT2(1.0f, 0.0f) },
		{ XMFLOAT3(-1.0f, 1.0f, -1.0f), XMFLOAT2(0.0f, 0.0f) },

		{ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT2(0.0f, 1.0f) },
		{ XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT2(1.0f, 1.0f) },
		{ XMFLOAT3(1.0f, 1.0f, -1.0f), XMFLOAT2(1.0f, 0.0f) },
		{ XMFLOAT3(-1.0f, 1.0f, -1.0f), XMFLOAT2(0.0f, 0.0f) },
	};

	/*XMFLOAT2(0.0f, 1.0f)
	XMFLOAT2(1.0f, 1.0f)
	XMFLOAT2(1.0f, 0.0f)
	XMFLOAT2(0.0f, 0.0f)
	*/
	// Simple array of type WORD which hold the order of indices for each triangle that we will draw. (to draw a plane we need 2 triangles)
	WORD indices[] =
	{
		3, 1, 0,        //first triangle
		2, 1, 3,        //second triangle
	};

	HRESULT hr = S_OK;
	DWORD ShaderFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined( DEBUG ) || defined( _DEBUG )
	ShaderFlags |= D3DCOMPILE_DEBUG;
#endif

	ID3D10Blob* pErrorBlob = nullptr;
	hr = D3DX11CompileFromFile(L"shader.hlsl", NULL, NULL, "VS", "vs_4_0", ShaderFlags, 0, NULL, &d3dBlobVertexShader, &pErrorBlob, NULL);
	if (FAILED(hr) && pErrorBlob != NULL) {
		OutputDebugStringA((char*)pErrorBlob->GetBufferPointer());
		pErrorBlob->Release();
	}

	d3dDevice->CreateVertexShader(d3dBlobVertexShader->GetBufferPointer(), d3dBlobVertexShader->GetBufferSize(), nullptr, &d3dVertexShader);

	pErrorBlob = nullptr;
	hr = D3DX11CompileFromFile(L"shader.hlsl", NULL, NULL, "PS", "ps_4_0", ShaderFlags, 0, NULL, &d3dBlobPixelShader, &pErrorBlob, NULL);
	if (FAILED(hr) && pErrorBlob != NULL) {
		OutputDebugStringA((char*)pErrorBlob->GetBufferPointer());
		pErrorBlob->Release();
	}

	d3dDevice->CreatePixelShader(d3dBlobPixelShader->GetBufferPointer(), d3dBlobPixelShader->GetBufferSize(), nullptr, &d3dPixelShader);


	// This code tells Direct3D how to read the block of data in memory.
	//For this we create InputLayout. We are telling what the first entry will be vertex position and the second  Texture Coordinates. 
	D3D11_INPUT_ELEMENT_DESC inputElements[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	UINT numElements = ARRAYSIZE(inputElements);
	//HRESULT hr = S_OK;
	hr = d3dDevice->CreateInputLayout(inputElements, numElements, d3dBlobVertexShader->GetBufferPointer(), d3dBlobVertexShader->GetBufferSize(), &d3dInputLayout);
	if FAILED(hr)
		return false;
	d3dContext->IASetInputLayout(d3dInputLayout);
	D3D11_BUFFER_DESC vbDesc;
	ZeroMemory(&vbDesc, sizeof(vbDesc));
	vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	//vbDesc.ByteWidth = sizeof(Vertex)* vertices.size();
	vbDesc.ByteWidth = sizeof(SimpleVertex)* 8;
	vbDesc.Usage = D3D11_USAGE_DEFAULT;

	D3D11_SUBRESOURCE_DATA initialData;
	ZeroMemory(&initialData, sizeof(initialData));
	//initialData.pSysMem = vertices.data();
	initialData.pSysMem = vertices;
	//initialData.SysMemPitch = sizeof(Vertex);
	initialData.SysMemPitch = sizeof(SimpleVertex);

	d3dDevice->CreateBuffer(&vbDesc, &initialData, &d3dVertexBuffer);

	UINT stride = sizeof(SimpleVertex);
	UINT offset = 0;
	d3dContext->IASetVertexBuffers(0, 1, &d3dVertexBuffer, &stride, &offset);

	ID3D11Buffer *m_pIndexBuffer = nullptr;
	vbDesc.Usage = D3D11_USAGE_DEFAULT;
	vbDesc.ByteWidth = sizeof(WORD)* 36;
	vbDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	vbDesc.CPUAccessFlags = 0;
	initialData.pSysMem = indices;
	d3dDevice->CreateBuffer(&vbDesc, &initialData, &m_pIndexBuffer);

	d3dContext->IASetIndexBuffer(m_pIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
	d3dContext->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);


	D3D11_BUFFER_DESC cbDesc;
	ZeroMemory(&cbDesc, sizeof(cbDesc));
	cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	cbDesc.Usage = D3D11_USAGE_DYNAMIC;
	cbDesc.ByteWidth = sizeof(float)* 16;

	hr = D3DX11CreateShaderResourceViewFromFile(d3dDevice, L"D:\\texture_out.png", NULL, NULL, &m_pTextureRV, NULL);
	if (FAILED(hr))
		return false;
	hr = D3DX11CreateShaderResourceViewFromFile(d3dDevice, L"D:\\texture_in.jpg", NULL, NULL, &m_pTextureRV2, NULL);
	if (FAILED(hr))
		return false;

	D3D11_SAMPLER_DESC sampDesc;
	ZeroMemory(&sampDesc, sizeof(sampDesc));
	sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	sampDesc.MinLOD = 0;
	sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
	hr = d3dDevice->CreateSamplerState(&sampDesc, &m_pSamplerLinear);
	if (FAILED(hr))
		return false;

	hr = d3dDevice->CreateBuffer(&cbDesc, nullptr, &d3dConstantBuffer);
	if (FAILED(hr))
		return false;
	/*
	UINT stride = sizeof(Vertex);
	UINT offset = 0;
	d3dContext->IASetInputLayout(d3dInputLayout);
	d3dContext->IASetVertexBuffers(0, 1, &d3dVertexBuffer, &stride, &offset);
	d3dContext->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	*/

	d3dContext->VSSetShader(d3dVertexShader, nullptr, 0);
	d3dContext->PSSetShader(d3dPixelShader, nullptr, 0);



	d3dContext->VSSetConstantBuffers(0, 1, &d3dConstantBuffer);



	d3dBlobPixelShader->Release();
	d3dBlobVertexShader->Release();

	return d3dConstantBuffer;
}

void DestroyScene() {
	d3dConstantBuffer->Release();
	d3dVertexBuffer->Release();
	d3dInputLayout->Release();
	d3dVertexShader->Release();
	d3dPixelShader->Release();
	m_pSamplerLinear->Release();
	m_pTextureRV->Release();
}