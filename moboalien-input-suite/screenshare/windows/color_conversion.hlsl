// Color conversion compute shader: BGRA to NV12
// This shader performs the BGRA to NV12 color space conversion entirely on the GPU
// NV12 format: Y plane (full resolution) + UV plane (half resolution, interleaved)

Texture2D<float4> InputTexture : register(t0);
RWTexture2D<float> OutputY : register(u0);
RWTexture2D<float2> OutputUV : register(u1);

cbuffer ConversionParams : register(b0) {
    uint width;
    uint height;
    uint2 pad;
};

// BT.601 color space conversion constants
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

    // Load BGRA pixel (note: D3D11 typically uses RGBA, but we handle BGRA case)
    float4 bgra = InputTexture[uint2(x, y)];
    float b = bgra.x;
    float g = bgra.y;
    float r = bgra.z;
    float a = bgra.w;

    // Convert BGRA to Y (luma)
    // Y = 0.299*R + 0.587*G + 0.114*B
    float y_val = Kr * r + Kg * g + Kb * b;
    y_val = clamp(y_val, 0.0f, 1.0f);

    // Write Y plane (full resolution)
    OutputY[uint2(x, y)] = y_val;

    // Process UV for every 2x2 block (NV12 has quarter resolution for UV)
    if ((x & 1) == 0 && (y & 1) == 0)
    {
        // Average 2x2 block for chroma downsampling
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

        // Convert to U and V (chroma)
        // U = -0.169*R - 0.331*G + 0.5*B + 0.5
        // V = 0.5*R - 0.419*G - 0.081*B + 0.5
        float u = -0.169f * r_avg - 0.331f * g_avg + 0.5f * b_avg + 0.5f;
        float v = 0.5f * r_avg - 0.419f * g_avg - 0.081f * b_avg + 0.5f;

        u = clamp(u, 0.0f, 1.0f);
        v = clamp(v, 0.0f, 1.0f);

        // Write UV plane (quarter resolution, interleaved)
        OutputUV[uint2(x >> 1, y >> 1)] = float2(u, v);
    }
}
