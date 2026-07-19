#include "gpu_color_converter.h"
#include "utils.h"
#include <d3dcompiler.h>
#include <sstream>

#pragma comment(lib, "d3dcompiler.lib")

static const char* TAG = "GPUColorConverter";

// Embedded HLSL shader code (same as color_conversion.hlsl)
static const char g_colorConversionShader[] = R"(
Texture2D<float4> InputTexture : register(t0);
RWTexture2D<float> OutputY : register(u0);
RWTexture2D<float2> OutputUV : register(u1);

cbuffer ConversionParams : register(b0) {
    uint width;
    uint height;
    uint2 pad;
};

static const float Kr = 0.299f;
static const float Kg = 0.587f;
static const float Kb = 0.114f;

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint x = DTid.x;
    uint y = DTid.y;

    if (x >= width || y >= height)
        return;

    float4 bgra = InputTexture[uint2(x, y)];
    float b = bgra.x;
    float g = bgra.y;
    float r = bgra.z;
    float a = bgra.w;

    float y_val = Kr * r + Kg * g + Kb * b;
    y_val = clamp(y_val, 0.0f, 1.0f);

    OutputY[uint2(x, y)] = y_val;

    if ((x & 1) == 0 && (y & 1) == 0)
    {
        float4 samples[4];
        samples[0] = InputTexture[uint2(x, y)];
        samples[1] = InputTexture[uint2(min(x + 1, width - 1), y)];
        samples[2] = InputTexture[uint2(x, min(y + 1, height - 1))];
        samples[3] = InputTexture[uint2(min(x + 1, width - 1), min(y + 1, height - 1))];

        float r_avg = 0.0f, g_avg = 0.0f, b_avg = 0.0f;
        for (int i = 0; i < 4; ++i)
        {
            b_avg += samples[i].x;
            g_avg += samples[i].y;
            r_avg += samples[i].z;
        }
        r_avg /= 4.0f;
        g_avg /= 4.0f;
        b_avg /= 4.0f;

        float v = -0.169f * r_avg - 0.331f * g_avg + 0.5f * b_avg + 0.5f;
        float u = 0.5f * r_avg - 0.419f * g_avg - 0.081f * b_avg + 0.5f;

        u = clamp(u, 0.0f, 1.0f);
        v = clamp(v, 0.0f, 1.0f);

        OutputUV[uint2(x >> 1, y >> 1)] = float2(u, v);
    }
}
)";

GPUColorConverter::GPUColorConverter()
{
}

GPUColorConverter::~GPUColorConverter()
{
}

bool GPUColorConverter::Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
{
    m_device = device;
    m_context = context;

    if (!CreateComputeShader()) {
        LOGE(TAG, "Failed to create compute shader");
        return false;
    }

    // Create constant buffer
    D3D11_BUFFER_DESC cbd;
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.ByteWidth = sizeof(uint32_t) * 4;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    cbd.MiscFlags = 0;
    cbd.StructureByteStride = 0;

    if (FAILED(m_device->CreateBuffer(&cbd, nullptr, &m_constantBuffer))) {
        LOGE(TAG, "Failed to create constant buffer");
        return false;
    }

    // Query Video interfaces (optional, only needed for VideoProcessor path)
    if (FAILED(m_device.As(&m_videoDevice))) {
        LOGW(TAG, "ID3D11VideoDevice not supported on this device");
    }
    if (FAILED(m_context.As(&m_videoContext))) {
        LOGW(TAG, "ID3D11VideoContext not supported on this context");
    }

    LOGI(TAG, "GPU Color Converter initialized successfully");
    return true;
}

bool GPUColorConverter::CreateComputeShader()
{
    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3DCompile(
        g_colorConversionShader,
        strlen(g_colorConversionShader),
        nullptr,
        nullptr,
        nullptr,
        "main",
        "cs_5_0",
        D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0,
        &blob,
        &errorBlob
    );

    if (FAILED(hr)) {
        if (errorBlob) {
            LOGE(TAG, "Shader compilation failed: " + std::string(static_cast<char*>(errorBlob->GetBufferPointer())));
        } else {
            LOGE(TAG, "Shader compilation failed: " + std::to_string(hr));
        }
        return false;
    }

    hr = m_device->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &m_colorConvertShader);
    if (FAILED(hr)) {
        LOGE(TAG, "Failed to create compute shader object: " + std::to_string(hr));
        return false;
    }

    LOGD(TAG, "Compute shader created successfully");
    return true;
}

bool GPUColorConverter::ConvertBGRAToNV12(
    ID3D11Texture2D* bgra_texture,
    ID3D11Texture2D* y_texture,
    ID3D11Texture2D* uv_texture,
    uint32_t width,
    uint32_t height
)
{
    if (!m_context || !m_colorConvertShader || !m_constantBuffer) {
        LOGE(TAG, "Converter not properly initialized");
        return false;
    }

    // Set constant buffer
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(m_context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        LOGE(TAG, "Failed to map constant buffer");
        return false;
    }

    uint32_t* params = static_cast<uint32_t*>(mapped.pData);
    params[0] = width;
    params[1] = height;
    params[2] = 0;
    params[3] = 0;

    m_context->Unmap(m_constantBuffer.Get(), 0);

    // Create SRV for input BGRA texture
    D3D11_TEXTURE2D_DESC texture_desc = {};
    bgra_texture->GetDesc(&texture_desc);
    
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texture_desc.Format;  // Use actual texture format
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    ComPtr<ID3D11ShaderResourceView> input_srv;
    if (FAILED(m_device->CreateShaderResourceView(bgra_texture, &srvDesc, &input_srv))) {
        LOGE(TAG, "Failed to create input SRV (format: " + std::to_string((int)texture_desc.Format) + ")");
        return false;
    }

    // Create UAVs for separate Y and UV textures
    D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
    uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
    uav_desc.Texture2D.MipSlice = 0;

    // Y plane UAV
    uav_desc.Format = DXGI_FORMAT_R8_UNORM;
    ComPtr<ID3D11UnorderedAccessView> output_y_uav;
    if (FAILED(m_device->CreateUnorderedAccessView(y_texture, &uav_desc, &output_y_uav))) {
        LOGE(TAG, "Failed to create Y plane UAV");
        return false;
    }

    // UV plane UAV
    uav_desc.Format = DXGI_FORMAT_R8G8_UNORM;
    ComPtr<ID3D11UnorderedAccessView> output_uv_uav;
    if (FAILED(m_device->CreateUnorderedAccessView(uv_texture, &uav_desc, &output_uv_uav))) {
        LOGE(TAG, "Failed to create UV plane UAV");
        return false;
    }

    // Bind resources
    m_context->CSSetShader(m_colorConvertShader.Get(), nullptr, 0);
    m_context->CSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    m_context->CSSetShaderResources(0, 1, input_srv.GetAddressOf());
    
    ID3D11UnorderedAccessView* uavs[] = { output_y_uav.Get(), output_uv_uav.Get() };
    m_context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);

    // Dispatch compute shader
    uint32_t group_x = (width + 7) / 8;
    uint32_t group_y = (height + 7) / 8;
    m_context->Dispatch(group_x, group_y, 1);

    // Unbind resources
    ID3D11UnorderedAccessView* null_uavs[] = { nullptr, nullptr };
    m_context->CSSetUnorderedAccessViews(0, 2, null_uavs, nullptr);
    
    ID3D11ShaderResourceView* null_srv = nullptr;
    m_context->CSSetShaderResources(0, 1, &null_srv);

    m_context->CSSetShader(nullptr, nullptr, 0);

    // Explicit flush to ensure GPU completes work before encoder reads texture
    m_context->Flush();

    LOGD(TAG, "Color conversion completed: " + std::to_string(width) + "x" + std::to_string(height) + 
         " into single NV12 texture (FFmpeg-managed)");
    return true;
}

bool GPUColorConverter::ConvertBGRAToNV12Texture(
    ID3D11Texture2D* bgra_texture,
    ID3D11UnorderedAccessView* y_uav,
    ID3D11UnorderedAccessView* uv_uav,
    uint32_t width,
    uint32_t height
)
{
    if (!m_context || !m_colorConvertShader || !m_constantBuffer) {
        LOGE(TAG, "Converter not properly initialized");
        return false;
    }

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(m_context->Map(m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        LOGE(TAG, "Failed to map constant buffer");
        return false;
    }

    uint32_t* params = static_cast<uint32_t*>(mapped.pData);
    params[0] = width;
    params[1] = height;
    params[2] = 0;
    params[3] = 0;
    m_context->Unmap(m_constantBuffer.Get(), 0);

    D3D11_TEXTURE2D_DESC texture_desc = {};
    bgra_texture->GetDesc(&texture_desc);
    
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texture_desc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    ComPtr<ID3D11ShaderResourceView> input_srv;
    if (FAILED(m_device->CreateShaderResourceView(bgra_texture, &srvDesc, &input_srv))) {
        LOGE(TAG, "Failed to create input SRV");
        return false;
    }

    m_context->CSSetShader(m_colorConvertShader.Get(), nullptr, 0);
    m_context->CSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());
    m_context->CSSetShaderResources(0, 1, input_srv.GetAddressOf());
    
    ID3D11UnorderedAccessView* uavs[] = { y_uav, uv_uav };
    m_context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);

    uint32_t group_x = (width + 7) / 8;
    uint32_t group_y = (height + 7) / 8;
    m_context->Dispatch(group_x, group_y, 1);

    ID3D11UnorderedAccessView* null_uavs[] = { nullptr, nullptr };
    m_context->CSSetUnorderedAccessViews(0, 2, null_uavs, nullptr);
    ID3D11ShaderResourceView* null_srv = nullptr;
    m_context->CSSetShaderResources(0, 1, &null_srv);
    m_context->CSSetShader(nullptr, nullptr, 0);

    return true;
}

bool GPUColorConverter::ConvertBGRAToNV12VideoProcessor(
    ID3D11Texture2D* bgra_texture,
    ID3D11Texture2D* nv12_texture,
    uint32_t inputWidth,
    uint32_t inputHeight,
    uint32_t outputWidth,
    uint32_t outputHeight
)
{
    if (!m_videoDevice || !m_videoContext) {
        return false;
    }

    if (!m_videoProcessor || inputWidth != m_vpWidth || inputHeight != m_vpHeight
                          || outputWidth != m_vpOutputWidth || outputHeight != m_vpOutputHeight) {
        m_videoProcessor.Reset();
        m_videoProcessorEnumerator.Reset();
        m_vpWidth        = inputWidth;
        m_vpHeight       = inputHeight;
        m_vpOutputWidth  = outputWidth;
        m_vpOutputHeight = outputHeight;

        D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDesc = {};
        contentDesc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        contentDesc.InputFrameRate.Numerator = 60;
        contentDesc.InputFrameRate.Denominator = 1;
        contentDesc.InputWidth  = inputWidth;
        contentDesc.InputHeight = inputHeight;
        contentDesc.OutputWidth  = outputWidth;
        contentDesc.OutputHeight = outputHeight;
        contentDesc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

        if (FAILED(m_videoDevice->CreateVideoProcessorEnumerator(&contentDesc, &m_videoProcessorEnumerator))) {
            LOGE(TAG, "Failed to create VideoProcessorEnumerator");
            return false;
        }

        if (FAILED(m_videoDevice->CreateVideoProcessor(m_videoProcessorEnumerator.Get(), 0, &m_videoProcessor))) {
            LOGE(TAG, "Failed to create VideoProcessor");
            return false;
        }
    }

    // Create Views
    // Note: To optimize this further, views should be cached with the textures in the pipeline
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputDesc = {};
    inputDesc.FourCC = 0;
    inputDesc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
    inputDesc.Texture2D.MipSlice = 0;
    inputDesc.Texture2D.ArraySlice = 0;

    ComPtr<ID3D11VideoProcessorInputView> inputView;
    HRESULT hrInputView = m_videoDevice->CreateVideoProcessorInputView(bgra_texture, m_videoProcessorEnumerator.Get(), &inputDesc, &inputView);
    if (FAILED(hrInputView)) {
        std::stringstream ss; ss << "Failed to create VideoProcessorInputView, hr=0x" << std::hex << hrInputView;
        LOGE(TAG, ss.str());
        return false;
    }

    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputDesc = {};
    outputDesc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    outputDesc.Texture2D.MipSlice = 0;

    ComPtr<ID3D11VideoProcessorOutputView> outputView;
    // Note: nv12_texture must be created with D3D11_BIND_RENDER_TARGET for this to succeed
    HRESULT hrOutputView = m_videoDevice->CreateVideoProcessorOutputView(nv12_texture, m_videoProcessorEnumerator.Get(), &outputDesc, &outputView);
    if (FAILED(hrOutputView)) {
        std::stringstream ss; ss << "Failed to create VideoProcessorOutputView, hr=0x" << std::hex << hrOutputView;
        LOGE(TAG, ss.str());
        return false;
    }

    D3D11_VIDEO_PROCESSOR_STREAM stream = {};
    stream.Enable = TRUE;
    stream.pInputSurface = inputView.Get();

    HRESULT hr = m_videoContext->VideoProcessorBlt(m_videoProcessor.Get(), outputView.Get(), 0, 1, &stream);
    if (FAILED(hr)) {
        std::stringstream ss;
        ss << "VideoProcessorBlt failed, hr=0x" << std::hex << hr;
        LOGE(TAG, ss.str());
    }
    return SUCCEEDED(hr);
}
