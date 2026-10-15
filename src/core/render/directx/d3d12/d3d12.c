/* Windows SDK COM interfaces stay in this C99 adapter; public APIs remain C90. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include "../../render_internal.h"
#include <stdlib.h>
#include <string.h>

typedef HRESULT (WINAPI *maage_dxgi_create_factory)(REFIID, void **);
typedef struct maage_d3d12_renderer {
    HMODULE d3d12_library, dxgi_library;
    ID3D12Device *device;
    ID3D12CommandQueue *queue;
    IDXGISwapChain3 *swapchain;
    ID3D12DescriptorHeap *heap;
    ID3D12CommandAllocator *allocator;
    ID3D12GraphicsCommandList *commands;
    ID3D12Fence *fence;
    ID3D12Resource *buffers[2];
    D3D12_CPU_DESCRIPTOR_HANDLE handles[2];
    HANDLE event;
    UINT64 serial;
    int width, height;
} maage_d3d12_renderer;

static maage_render_result maage_d3d12_result(HRESULT result)
{
    if (SUCCEEDED(result)) return MAAGE_RENDER_OK;
    if (result == E_OUTOFMEMORY) return MAAGE_RENDER_OUT_OF_MEMORY;
    if (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET) return MAAGE_RENDER_DEVICE_LOST;
    if (result == DXGI_ERROR_UNSUPPORTED) return MAAGE_RENDER_UNSUPPORTED;
    return MAAGE_RENDER_PLATFORM_ERROR;
}
static maage_render_result maage_d3d12_wait(maage_d3d12_renderer *native)
{
    HRESULT result;
    if (!native->queue || !native->fence || !native->event) return MAAGE_RENDER_OK;
    result = ID3D12CommandQueue_Signal(native->queue, native->fence, ++native->serial);
    if (FAILED(result)) return maage_d3d12_result(result);
    if (ID3D12Fence_GetCompletedValue(native->fence) < native->serial) {
        result = ID3D12Fence_SetEventOnCompletion(native->fence, native->serial, native->event);
        if (FAILED(result)) return maage_d3d12_result(result);
        if (WaitForSingleObject(native->event, INFINITE) != WAIT_OBJECT_0) return MAAGE_RENDER_PLATFORM_ERROR;
    }
    return MAAGE_RENDER_OK;
}
static maage_render_result maage_d3d12_buffers(maage_d3d12_renderer *native)
{
    UINT index, increment = ID3D12Device_GetDescriptorHandleIncrementSize(native->device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    HRESULT result;
    native->heap->lpVtbl->GetCPUDescriptorHandleForHeapStart(native->heap, &native->handles[0]);
    native->handles[1].ptr = native->handles[0].ptr + increment;
    for (index = 0; index < 2; ++index) {
        result = IDXGISwapChain3_GetBuffer(native->swapchain, index, &IID_ID3D12Resource, (void **)&native->buffers[index]);
        if (FAILED(result)) return maage_d3d12_result(result);
        ID3D12Device_CreateRenderTargetView(native->device, native->buffers[index], NULL, native->handles[index]);
    }
    return MAAGE_RENDER_OK;
}
static maage_render_result maage_d3d12_create(maage_renderer *renderer)
{
    maage_d3d12_renderer *native;
    IDXGIFactory4 *factory = NULL;
    IDXGISwapChain1 *swapchain = NULL;
    D3D12_COMMAND_QUEUE_DESC queue;
    D3D12_DESCRIPTOR_HEAP_DESC heap;
    DXGI_SWAP_CHAIN_DESC1 description;
    D3D12_FEATURE_DATA_FORMAT_SUPPORT format;
    maage_window_state state;
    HRESULT result;
    PFN_D3D12_CREATE_DEVICE create_device;
    maage_dxgi_create_factory create_factory;
    if (renderer->native.backend != MAAGE_WINDOW_WIN32) return MAAGE_RENDER_UNSUPPORTED;
    native = calloc(1, sizeof(*native));
    if (!native) return MAAGE_RENDER_OUT_OF_MEMORY;
    renderer->platform = native;
    native->d3d12_library = LoadLibraryExA("d3d12.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    native->dxgi_library = LoadLibraryExA("dxgi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!native->d3d12_library || !native->dxgi_library) return MAAGE_RENDER_UNAVAILABLE;
    create_device = (PFN_D3D12_CREATE_DEVICE)GetProcAddress(native->d3d12_library, "D3D12CreateDevice");
    create_factory = (maage_dxgi_create_factory)GetProcAddress(native->dxgi_library, "CreateDXGIFactory1");
    if (!create_device || !create_factory) return MAAGE_RENDER_UNAVAILABLE;
    result = create_device(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&native->device);
    if (FAILED(result)) return MAAGE_RENDER_UNAVAILABLE;
    memset(&queue, 0, sizeof(queue));
    queue.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    result = ID3D12Device_CreateCommandQueue(native->device, &queue, &IID_ID3D12CommandQueue, (void **)&native->queue);
    if (FAILED(result)) return maage_d3d12_result(result);
    result = create_factory(&IID_IDXGIFactory4, (void **)&factory);
    if (FAILED(result)) return maage_d3d12_result(result);
    if (maage_window_get_state(renderer->window, &state) != MAAGE_WINDOW_OK) {
        IDXGIFactory4_Release(factory);
        return MAAGE_RENDER_WINDOW_ERROR;
    }
    native->width = state.framebuffer_width > 0 ? state.framebuffer_width : 1;
    native->height = state.framebuffer_height > 0 ? state.framebuffer_height : 1;
    memset(&description, 0, sizeof(description));
    description.Width = (UINT)native->width; description.Height = (UINT)native->height;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 2;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    result = IDXGIFactory4_CreateSwapChainForHwnd(factory, (IUnknown *)native->queue,
        renderer->native.handles.win32.window, &description, NULL, NULL, &swapchain);
    if (SUCCEEDED(result))
        result = IDXGISwapChain1_QueryInterface(swapchain, &IID_IDXGISwapChain3, (void **)&native->swapchain);
    if (swapchain) IDXGISwapChain1_Release(swapchain);
    IDXGIFactory4_Release(factory);
    if (FAILED(result)) return maage_d3d12_result(result);
    memset(&heap, 0, sizeof(heap));
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap.NumDescriptors = 2;
    result = ID3D12Device_CreateDescriptorHeap(native->device, &heap, &IID_ID3D12DescriptorHeap, (void **)&native->heap);
    if (FAILED(result)) return maage_d3d12_result(result);
    result = ID3D12Device_CreateCommandAllocator(native->device, D3D12_COMMAND_LIST_TYPE_DIRECT,
        &IID_ID3D12CommandAllocator, (void **)&native->allocator);
    if (FAILED(result)) return maage_d3d12_result(result);
    result = ID3D12Device_CreateCommandList(native->device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, native->allocator,
        NULL, &IID_ID3D12GraphicsCommandList, (void **)&native->commands);
    if (FAILED(result)) return maage_d3d12_result(result);
    result = ID3D12GraphicsCommandList_Close(native->commands);
    if (FAILED(result)) return maage_d3d12_result(result);
    result = ID3D12Device_CreateFence(native->device, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&native->fence);
    if (FAILED(result)) return maage_d3d12_result(result);
    native->event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!native->event) return MAAGE_RENDER_PLATFORM_ERROR;
    renderer->capabilities.features |= MAAGE_RENDER_CAP_SHADERS | MAAGE_RENDER_CAP_VERTEX_BUFFERS |
        MAAGE_RENDER_CAP_INDEX_BUFFERS | MAAGE_RENDER_CAP_RENDER_TARGETS | MAAGE_RENDER_CAP_INSTANCING | MAAGE_RENDER_CAP_COMPUTE;
    renderer->capabilities.synchronization = MAAGE_RENDER_SYNC_CPU_WAIT | MAAGE_RENDER_SYNC_FENCE;
    renderer->capabilities.max_texture_dimension_2d = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    memset(&format, 0, sizeof(format));
    format.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    if (SUCCEEDED(ID3D12Device_CheckFeatureSupport(native->device, D3D12_FEATURE_FORMAT_SUPPORT, &format, sizeof(format))) &&
        (format.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE)) renderer->capabilities.texture_formats |= MAAGE_RENDER_FORMAT_RGBA8;
    format.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    if (SUCCEEDED(ID3D12Device_CheckFeatureSupport(native->device, D3D12_FEATURE_FORMAT_SUPPORT, &format, sizeof(format))) &&
        (format.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE)) renderer->capabilities.texture_formats |= MAAGE_RENDER_FORMAT_BGRA8;
    format.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    if (SUCCEEDED(ID3D12Device_CheckFeatureSupport(native->device, D3D12_FEATURE_FORMAT_SUPPORT, &format, sizeof(format))) &&
        (format.Support1 & D3D12_FORMAT_SUPPORT1_DEPTH_STENCIL))
        renderer->capabilities.features |= MAAGE_RENDER_CAP_DEPTH | MAAGE_RENDER_CAP_STENCIL;
    return maage_d3d12_buffers(native);
}
static void maage_d3d12_destroy(maage_renderer *renderer)
{
    maage_d3d12_renderer *native = renderer->platform;
    UINT index;
    if (!native) return;
    maage_d3d12_wait(native);
    for (index = 0; index < 2; ++index) if (native->buffers[index]) ID3D12Resource_Release(native->buffers[index]);
    if (native->commands) ID3D12GraphicsCommandList_Release(native->commands);
    if (native->allocator) ID3D12CommandAllocator_Release(native->allocator);
    if (native->heap) ID3D12DescriptorHeap_Release(native->heap);
    if (native->swapchain) IDXGISwapChain3_Release(native->swapchain);
    if (native->fence) ID3D12Fence_Release(native->fence);
    if (native->queue) ID3D12CommandQueue_Release(native->queue);
    if (native->device) ID3D12Device_Release(native->device);
    if (native->event) CloseHandle(native->event);
    if (native->dxgi_library) FreeLibrary(native->dxgi_library);
    if (native->d3d12_library) FreeLibrary(native->d3d12_library);
    free(native);
}
static maage_render_result maage_d3d12_present(maage_renderer *renderer, const maage_window_state *state)
{
    maage_d3d12_renderer *native = renderer->platform;
    D3D12_RESOURCE_BARRIER barrier;
    ID3D12CommandList *list;
    HRESULT result, presented;
    maage_render_result waited;
    UINT index;
    waited = maage_d3d12_wait(native);
    if (waited != MAAGE_RENDER_OK) return waited;
    if (native->width != state->framebuffer_width || native->height != state->framebuffer_height) {
        for (index = 0; index < 2; ++index) { ID3D12Resource_Release(native->buffers[index]); native->buffers[index] = NULL; }
        result = IDXGISwapChain3_ResizeBuffers(native->swapchain, 2, (UINT)state->framebuffer_width,
            (UINT)state->framebuffer_height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
        if (FAILED(result)) return maage_d3d12_result(result);
        native->width = state->framebuffer_width; native->height = state->framebuffer_height;
        waited = maage_d3d12_buffers(native);
        if (waited != MAAGE_RENDER_OK) return waited;
    }
    result = ID3D12CommandAllocator_Reset(native->allocator);
    if (FAILED(result)) return maage_d3d12_result(result);
    result = ID3D12GraphicsCommandList_Reset(native->commands, native->allocator, NULL);
    if (FAILED(result)) return maage_d3d12_result(result);
    index = IDXGISwapChain3_GetCurrentBackBufferIndex(native->swapchain);
    memset(&barrier, 0, sizeof(barrier));
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = native->buffers[index];
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    ID3D12GraphicsCommandList_ResourceBarrier(native->commands, 1, &barrier);
    ID3D12GraphicsCommandList_ClearRenderTargetView(native->commands, native->handles[index], renderer->clear, 0, NULL);
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    ID3D12GraphicsCommandList_ResourceBarrier(native->commands, 1, &barrier);
    result = ID3D12GraphicsCommandList_Close(native->commands);
    if (FAILED(result)) return maage_d3d12_result(result);
    list = (ID3D12CommandList *)native->commands;
    ID3D12CommandQueue_ExecuteCommandLists(native->queue, 1, &list);
    presented = IDXGISwapChain3_Present(native->swapchain, 1, 0);
    waited = maage_d3d12_wait(native);
    if (waited != MAAGE_RENDER_OK) return waited;
    renderer->refresh = 0;
    return presented == DXGI_STATUS_OCCLUDED ? MAAGE_RENDER_SKIPPED : maage_d3d12_result(presented);
}
const maage_render_backend_api maage_render_d3d12_api = {
    MAAGE_RENDER_D3D12, maage_d3d12_create, maage_d3d12_destroy, maage_d3d12_present,
    MAAGE_RENDER_CAPABILITIES_VERSION
};
