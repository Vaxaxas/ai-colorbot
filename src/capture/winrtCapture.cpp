// WinRT (Windows.Graphics.Capture) implementation with D3D11/CUDA interop.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <iostream>
#include <chrono>
#include <thread>

#include "capture/winrtCapture.h"
#include "util/otherTools.h"
#include "config/config.h"

template <typename T>
static inline void SafeRelease(T** ppInterface)
{
    if (*ppInterface)
    {
        (*ppInterface)->Release();
        *ppInterface = nullptr;
    }
}

WinRTScreenCapture::WinRTScreenCapture(int monitorIndex, int desiredWidth, int desiredHeight)
    : regionWidth(desiredWidth),
      regionHeight(desiredHeight)
{
    try
    {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    }
    catch (...)
    {
        // Apartment may already be initialized on this thread
    }

    if (!initD3D())
    {
        std::cerr << "[WinRT] Failed to initialize Direct3D 11 device." << std::endl;
        return;
    }

    if (!initCapture(monitorIndex))
    {
        std::cerr << "[WinRT] Failed to initialize Windows.Graphics.Capture." << std::endl;
        return;
    }

    createSharedTextureGPU();
    createStagingTextureCPU();
    m_cachedCpuFrame = cv::Mat::zeros(regionHeight, regionWidth, CV_8UC4);

    m_initialized = true;
    std::cout << "[WinRT] Capture initialized successfully ("
              << screenWidth << "x" << screenHeight << " -> "
              << regionWidth << "x" << regionHeight << ")" << std::endl;
}

WinRTScreenCapture::~WinRTScreenCapture()
{
    if (m_session)
    {
        try { m_session.Close(); } catch (...) {}
        m_session = nullptr;
    }
    if (m_framePool)
    {
        try { m_framePool.Close(); } catch (...) {}
        m_framePool = nullptr;
    }
    m_item = nullptr;

    SafeRelease(&stagingTextureCPU);
    SafeRelease(&sharedTexture);

    if (cudaResource)
    {
        cudaGraphicsUnregisterResource(cudaResource);
        cudaResource = nullptr;
    }
    if (cudaStream)
    {
        cudaStreamDestroy(cudaStream);
        cudaStream = nullptr;
    }

    SafeRelease(&d3dContext);
    SafeRelease(&d3dDevice);
}

bool WinRTScreenCapture::initD3D()
{
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &d3dDevice,
        &featureLevel,
        &d3dContext
    );

    return SUCCEEDED(hr);
}

bool WinRTScreenCapture::initCapture(int monitorIndex)
{
    HMONITOR hMonitor = getMonitorHandleByIndex(monitorIndex);
    if (!hMonitor)
    {
        hMonitor = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
    }
    if (!hMonitor)
    {
        std::cerr << "[WinRT] Could not get monitor handle for index " << monitorIndex << std::endl;
        return false;
    }

    MONITORINFO mi = { sizeof(mi) };
    if (GetMonitorInfo(hMonitor, &mi))
    {
        screenWidth  = mi.rcMonitor.right  - mi.rcMonitor.left;
        screenHeight = mi.rcMonitor.bottom - mi.rcMonitor.top;
    }

    try
    {
        auto factory = winrt::get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem>();
        auto interop = factory.as<IGraphicsCaptureItemInterop>();

        HRESULT hr = interop->CreateForMonitor(
            hMonitor,
            winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
            winrt::put_abi(m_item)
        );

        if (FAILED(hr) || !m_item)
        {
            std::cerr << "[WinRT] IGraphicsCaptureItemInterop::CreateForMonitor failed hr=0x"
                      << std::hex << hr << std::endl;
            return false;
        }

        auto itemSize = m_item.Size();
        if (itemSize.Width > 0 && itemSize.Height > 0)
        {
            screenWidth  = itemSize.Width;
            screenHeight = itemSize.Height;
        }

        winrt::com_ptr<IDXGIDevice> dxgiDevice;
        d3dDevice->QueryInterface(IID_PPV_ARGS(&dxgiDevice));

        winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice winrtDevice{ nullptr };
        hr = CreateDirect3D11DeviceFromDXGIDevice(
            dxgiDevice.get(),
            reinterpret_cast<IInspectable**>(winrt::put_abi(winrtDevice))
        );

        if (FAILED(hr))
        {
            std::cerr << "[WinRT] CreateDirect3D11DeviceFromDXGIDevice failed hr=0x"
                      << std::hex << hr << std::endl;
            return false;
        }

        m_framePool = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
            winrtDevice,
            winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
            2,
            m_item.Size()
        );

        m_session = m_framePool.CreateCaptureSession(m_item);

        try { m_session.IsCursorCaptureEnabled(false); } catch (...) {}
        try { m_session.IsBorderRequired(false); } catch (...) {}

        m_session.StartCapture();
        return true;
    }
    catch (const winrt::hresult_error& ex)
    {
        std::cerr << "[WinRT] Exception: " << winrt::to_string(ex.message())
                  << " (0x" << std::hex << ex.code() << ")" << std::endl;
        return false;
    }
    catch (const std::exception& e)
    {
        std::cerr << "[WinRT] Standard Exception: " << e.what() << std::endl;
        return false;
    }
}

bool WinRTScreenCapture::createSharedTextureGPU()
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width              = regionWidth;
    desc.Height             = regionHeight;
    desc.MipLevels          = 1;
    desc.ArraySize          = 1;
    desc.Format             = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count   = 1;
    desc.Usage              = D3D11_USAGE_DEFAULT;
    desc.BindFlags          = D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags          = D3D11_RESOURCE_MISC_SHARED;

    HRESULT hr = d3dDevice->CreateTexture2D(&desc, nullptr, &sharedTexture);
    if (FAILED(hr)) return false;

    cudaError_t err = cudaGraphicsD3D11RegisterResource(
        &cudaResource, sharedTexture, cudaGraphicsRegisterFlagsNone);
    if (err != cudaSuccess) return false;

    cudaStreamCreate(&cudaStream);
    return true;
}

bool WinRTScreenCapture::createStagingTextureCPU()
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width              = regionWidth;
    desc.Height             = regionHeight;
    desc.MipLevels          = 1;
    desc.ArraySize          = 1;
    desc.Format             = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count   = 1;
    desc.Usage              = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags     = D3D11_CPU_ACCESS_READ;

    HRESULT hr = d3dDevice->CreateTexture2D(&desc, nullptr, &stagingTextureCPU);
    return SUCCEEDED(hr);
}

cv::cuda::GpuMat WinRTScreenCapture::GetNextFrameGpu()
{
    if (!m_initialized || !sharedTexture || !cudaResource || !m_framePool)
        return cv::cuda::GpuMat();

    auto frame = m_framePool.TryGetNextFrame();
    if (frame)
    {
        auto surface = frame.Surface();
        if (surface)
        {
            try
            {
                auto access = surface.as<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
                winrt::com_ptr<ID3D11Texture2D> surfaceTex;
                if (SUCCEEDED(access->GetInterface(IID_PPV_ARGS(&surfaceTex))) && surfaceTex)
                {
                    D3D11_BOX box{
                        static_cast<UINT>((screenWidth  - regionWidth)  / 2),
                        static_cast<UINT>((screenHeight - regionHeight) / 2),
                        0u,
                        static_cast<UINT>((screenWidth  + regionWidth)  / 2),
                        static_cast<UINT>((screenHeight + regionHeight) / 2),
                        1u
                    };
                    d3dContext->CopySubresourceRegion(sharedTexture, 0, 0, 0, 0, surfaceTex.get(), 0, &box);
                    m_hasValidFrame = true;
                }
            }
            catch (...) {}
        }
        frame.Close();
    }

    if (!m_hasValidFrame)
        return cv::cuda::GpuMat();

    cv::cuda::GpuMat gpu(regionHeight, regionWidth, CV_8UC4);
    cudaGraphicsMapResources(1, &cudaResource, cudaStream);
    cudaArray_t arr;
    cudaGraphicsSubResourceGetMappedArray(&arr, cudaResource, 0, 0);
    cudaMemcpy2DFromArrayAsync(
        gpu.data, gpu.step,
        arr, 0, 0,
        static_cast<size_t>(regionWidth) * 4,
        static_cast<size_t>(regionHeight),
        cudaMemcpyDeviceToDevice,
        cudaStream
    );
    cudaGraphicsUnmapResources(1, &cudaResource, cudaStream);
    cudaStreamSynchronize(cudaStream);

    return gpu;
}

cv::Mat WinRTScreenCapture::GetNextFrameCpu()
{
    if (!m_initialized || !stagingTextureCPU || !m_framePool)
        return cv::Mat();

    auto frame = m_framePool.TryGetNextFrame();
    if (frame)
    {
        auto surface = frame.Surface();
        if (surface)
        {
            try
            {
                auto access = surface.as<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
                winrt::com_ptr<ID3D11Texture2D> surfaceTex;
                if (SUCCEEDED(access->GetInterface(IID_PPV_ARGS(&surfaceTex))) && surfaceTex)
                {
                    D3D11_BOX box{
                        static_cast<UINT>((screenWidth  - regionWidth)  / 2),
                        static_cast<UINT>((screenHeight - regionHeight) / 2),
                        0u,
                        static_cast<UINT>((screenWidth  + regionWidth)  / 2),
                        static_cast<UINT>((screenHeight + regionHeight) / 2),
                        1u
                    };
                    d3dContext->CopySubresourceRegion(stagingTextureCPU, 0, 0, 0, 0, surfaceTex.get(), 0, &box);

                    D3D11_MAPPED_SUBRESOURCE mapped;
                    if (SUCCEEDED(d3dContext->Map(stagingTextureCPU, 0, D3D11_MAP_READ, 0, &mapped)))
                    {
                        for (int y = 0; y < regionHeight; ++y)
                        {
                            memcpy(m_cachedCpuFrame.ptr(y),
                                   static_cast<BYTE*>(mapped.pData) + y * mapped.RowPitch,
                                   static_cast<size_t>(regionWidth) * 4);
                        }
                        d3dContext->Unmap(stagingTextureCPU, 0);
                        m_hasValidFrame = true;
                    }
                }
            }
            catch (...) {}
        }
        frame.Close();
    }

    if (!m_hasValidFrame)
        return cv::Mat();

    return m_cachedCpuFrame.clone();
}
