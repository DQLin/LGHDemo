#include "LGHDemo.h"

int wmain(int argc, wchar_t** argv)
{
#if _DEBUG
	CComPtr<ID3D12Debug> debugInterface;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugInterface))))
	{
		debugInterface->EnableDebugLayer();
	}
#endif

	UUID experimentalShadersFeatures[] = { D3D12ExperimentalShaderModels };
	struct Experiment { UUID *Experiments; UINT numFeatures; };
	Experiment experiments[] = {
		{ experimentalShadersFeatures, ARRAYSIZE(experimentalShadersFeatures) },
		{ nullptr, 0 },
	};

	CComPtr<ID3D12Device> pDevice;
	CComPtr<IDXGIAdapter1> pAdapter;
	CComPtr<IDXGIFactory2> pFactory;
	CreateDXGIFactory2(0, IID_PPV_ARGS(&pFactory));
	bool validDeviceFound = false;
	for (auto &experiment : experiments)
	{
		if (SUCCEEDED(D3D12EnableExperimentalFeatures(experiment.numFeatures, experiment.Experiments, nullptr, nullptr)))
		{
			for (uint32_t Idx = 0; !validDeviceFound && DXGI_ERROR_NOT_FOUND != pFactory->EnumAdapters1(Idx, &pAdapter); ++Idx)
			{
				DXGI_ADAPTER_DESC1 desc;
				pAdapter->GetDesc1(&desc);
				if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0)
				{
					validDeviceFound = SUCCEEDED(D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&pDevice)));
				}
				pAdapter = nullptr;
			}
			if (validDeviceFound) break;
		}
	}

	s_EnableVSync.Decrement();

	// parse scene file

	if (argc > 1)
	{
		int imgWidth;
		int imgHeight;

		std::wstring ws(argv[1]);
		size_t xpos = ws.find('x');

		auto isNonnegInteger = [](std::wstring s)
		{
			for (int i = 0; i < s.length(); i++)
				if (!isdigit(s[i]))
					return false;
			return true;
		};

		// any [Integer]x[Integer] string will be treated as resolution argument
		int matchCount = 0;
		if (xpos != std::string::npos)
		{
			std::wstring width = ws.substr(0, xpos);
			if (isNonnegInteger(width))
			{
				imgWidth = std::min(7680, std::max(1,std::stoi(width)));
				matchCount++;
			}
			std::wstring height = ws.substr(xpos+1);
			if (isNonnegInteger(height))
			{
				imgHeight = std::min(7680, std::max(1,std::stoi(height)));
				matchCount++;
			}
		}

		if (matchCount == 2)
		{
			g_CustomResolutionX = imgWidth;
			g_CustomResolutionY = imgHeight;
			g_DisplayWidth = imgWidth;
			g_DisplayHeight = imgHeight;
			GameCore::RunApplication(LGHDemo(), L"DXLGH");
		}
		else
		{
			std::string modelPath;
			Camera camera;
			bool isCameraInitialized;
			float sunIntensity;
			float sunOrietation;
			float sunInclination;
			std::string str(ws.begin(), ws.end());
			if (!ParseSceneFile(str, modelPath, camera, isCameraInitialized,
				sunIntensity, sunOrietation, sunInclination, imgWidth, imgHeight)) return 1;

			g_CustomResolutionX = imgWidth;
			g_CustomResolutionY = imgHeight;
			g_DisplayWidth = imgWidth;
			g_DisplayHeight = imgHeight;
			GameCore::RunApplication(LGHDemo(modelPath, isCameraInitialized, camera, sunIntensity, sunOrietation, sunInclination), L"DXLGH");
		}
	}
	else // load demo scene (crytek sponza)
	{
		TargetResolution = k720p;
		g_DisplayWidth = 1280;
		g_DisplayHeight = 720;
		GameCore::RunApplication(LGHDemo(), L"DXLGH");
	}
	return 0;
}
