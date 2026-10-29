// WinRT (Windows.Graphics.Capture) backend with D3D11/CUDA interop.
#ifndef WINRT_CAPTURE_H
#define WINRT_CAPTURE_H

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <cuda_runtime.h>
#include <cuda_d3d11_interop.h>
#include <memory>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <opencv2/core.hpp>
#include <opencv2/core/cuda.hpp>
#include "capture/capture.h"

class WinRTScreenCapture : public IScreenCapture
{
public:
    WinRTScreenCapture(int monitorIndex, int desiredWidth, int desiredHeight);
    ~WinRTScreenCapture() override;

    cv::cuda::GpuMat GetNextFrameGpu() override;
    cv::Mat          GetNextFrameCpu() override;
    bool             IsInitialized() const { return m_initialized; }

private:
    bool initD3D(int monitorIndex);
    bool initCapture(int monitorIndex);
    bool createSharedTextureGPU();
    bool createStagingTextureCPU();

    ID3D11Device*           d3dDevice          = nullptr;
    ID3D11DeviceContext*    d3dContext          = nullptr;

    ID3D11Texture2D*        sharedTexture       = nullptr;
    cudaGraphicsResource*   cudaResource        = nullptr;
    cudaStream_t            cudaStream          = nullptr;

    ID3D11Texture2D*        stagingTextureCPU   = nullptr;
    cv::Mat                 m_cachedCpuFrame;

    int screenWidth  = 0;
    int screenHeight = 0;
    int regionWidth  = 0;
    int regionHeight = 0;
    bool m_initialized = false;
    bool m_hasValidFrame = false;

    winrt::Windows::Graphics::Capture::GraphicsCaptureItem        m_item{ nullptr };
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool m_framePool{ nullptr };
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession    m_session{ nullptr };
};

#endif // WINRT_CAPTURE_H
