#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
using Microsoft::WRL::ComPtr;

class GPUColorConverter {
public:
    GPUColorConverter();
    ~GPUColorConverter();

    // Initialize with D3D11 device
    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context);

    // Convert BGRA texture to separate Y and UV textures
    bool ConvertBGRAToNV12(
        ID3D11Texture2D* bgra_texture,
        ID3D11Texture2D* y_texture,
        ID3D11Texture2D* uv_texture,
        uint32_t width,
        uint32_t height
    );

    // Convert BGRA to NV12 texture using UAVs
    bool ConvertBGRAToNV12Texture(
        ID3D11Texture2D* bgra_texture,
        ID3D11UnorderedAccessView* y_uav,
        ID3D11UnorderedAccessView* uv_uav,
        uint32_t width,
        uint32_t height
    );

    // Convert BGRA to NV12 texture using D3D11 Video Processor
    bool ConvertBGRAToNV12VideoProcessor(
        ID3D11Texture2D* bgra_texture,
        ID3D11Texture2D* nv12_texture,
        uint32_t inputWidth,
        uint32_t inputHeight,
        uint32_t outputWidth,
        uint32_t outputHeight
    );

private:
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<ID3D11ComputeShader> m_colorConvertShader;
    ComPtr<ID3D11Buffer> m_constantBuffer;

    // Video Processor interfaces
    ComPtr<ID3D11VideoDevice> m_videoDevice;
    ComPtr<ID3D11VideoContext> m_videoContext;
    ComPtr<ID3D11VideoProcessor> m_videoProcessor;
    ComPtr<ID3D11VideoProcessorEnumerator> m_videoProcessorEnumerator;
    uint32_t m_vpWidth = 0;
    uint32_t m_vpHeight = 0;
    uint32_t m_vpOutputWidth = 0;
    uint32_t m_vpOutputHeight = 0;

    bool CreateComputeShader();
};
