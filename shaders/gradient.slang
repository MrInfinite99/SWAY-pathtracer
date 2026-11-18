// Throwaway. This exists only so the image path - allocate, write, blit,
// present - is proven before ray tracing is added. The raygen shader writes
// to this same image at Phase 4 and this file is deleted.

// Binding first, set second: the reverse of GLSL's layout(set, binding).
// The explicit format drops the StorageImageWriteWithoutFormat capability,
// which would otherwise need a device feature enabled.
[[vk::image_format("rgba32f")]]
[[vk::binding(0, 0)]]
RWTexture2D<float4> image;

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 threadId : SV_DispatchThreadID)
{
    uint width;
    uint height;
    image.GetDimensions(width, height);

    // The dispatch is rounded up to whole 8x8 groups, so the last row and
    // column of groups run past the image.
    if (threadId.x >= width || threadId.y >= height)
    {
        return;
    }

    float2 uv = float2(threadId.xy) / float2(width, height);
    image[threadId.xy] = float4(uv, 0.25, 1.0);
}
