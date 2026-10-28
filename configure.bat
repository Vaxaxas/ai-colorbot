@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion

rem ==============================================================================
rem ai_colorbot - Project Configuration Script
rem ==============================================================================

rem [1] Compiler & Generator settings (VS 2022 v170 for CUDA 13.1 compatibility)
set "GENERATOR=Visual Studio 17 2022"
set "ARCH=x64"

rem [2] Dependency path pointers
rem ------------------------------------------------------------------------------
rem TensorRT (must contain include\NvInfer.h and lib\nvinfer.lib)
if "%TRT_PATH%"=="" (
    if exist "C:\TensorRT" (
        set "TRT_PATH=C:\TensorRT"
    ) else (
        set "TRT_PATH=C:\path\to\TensorRT"
    )
)

rem cuDNN (must contain include\cudnn.h and lib\cudnn.lib)
if "%CUDNN_PATH%"=="" (
    set "CUDNN_PATH=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.1"
)

rem OpenCV with CUDA (must contain OpenCVConfig.cmake)
if "%OpenCV_DIR%"=="" (
    if exist "C:\opencv\build" (
        set "OpenCV_DIR=C:\opencv\build"
    ) else (
        set "OpenCV_DIR=C:\path\to\opencv\build"
    )
)

rem Serial (must contain include\serial\serial.h and lib\serial.lib)
if "%SERIAL_ROOT%"=="" (
    if exist "%~dp0third_party\serial" (
        set "SERIAL_ROOT=%~dp0third_party\serial"
    ) else (
        set "SERIAL_ROOT=C:\path\to\serial"
    )
)

echo ==============================================================================
echo [ai_colorbot] Configuration Pointers:
echo ==============================================================================
echo  - Generator   : %GENERATOR% (%ARCH%)
echo  - CUDA_PATH   : %CUDA_PATH%
echo  - TRT_PATH    : %TRT_PATH%
echo  - CUDNN_PATH  : %CUDNN_PATH%
echo  - OpenCV_DIR  : %OpenCV_DIR%
echo  - SERIAL_ROOT : %SERIAL_ROOT%
echo ==============================================================================

cmake -B build -G "%GENERATOR%" -A %ARCH% ^
    -DOpenCV_DIR="%OpenCV_DIR%" ^
    -DSERIAL_ROOT="%SERIAL_ROOT%"

if %ERRORLEVEL% equ 0 (
    echo.
    echo [SUCCESS] Project configuration completed!
    echo Build with: cmake --build build --config Release
) else (
    echo.
    echo [INFO] CMake configuration paused.
    echo Please verify the paths above point to your extracted SDK folders.
)

endlocal
