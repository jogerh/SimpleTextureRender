#include <iostream>
#include <iostream>
#include <Windows.h>
#include <D3D11_1.h>
#include <wrl.h>
#include <winrt/base.h>
#include <d3dcompiler.h>
#include <d3dcommon.h>
#include <mutex>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

namespace {
    void Check(HRESULT hr)
    {
        if (hr == S_OK)
            return;

        const winrt::hresult_error err{hr};
        std::wcerr << err.message().c_str() << std::endl;
        winrt::check_hresult(hr);
    }

    ComPtr<ID3D11Device1> CreateDevice()
    {
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), {}, context.GetAddressOf()));

        ComPtr<ID3D11Device1> result;
        Check(device.As(&result));
        return result;
    }

    ComPtr<ID3DBlob> CompileShader(const std::string& shader, const std::string& entryPoint, const std::string& target)
    {
        ComPtr<ID3DBlob> shaderCompileErrorsBlob;
        ComPtr<ID3DBlob> shaderCode;
        const HRESULT hResult = D3DCompile(shader.data(), shader.size(), nullptr, nullptr, nullptr, entryPoint.c_str(), target.c_str(), 0, 0, shaderCode.GetAddressOf(), shaderCompileErrorsBlob.GetAddressOf());
        if (FAILED(hResult))
        {
            const char* errorString = nullptr;
            if (shaderCompileErrorsBlob)
                errorString = static_cast<const char*>(shaderCompileErrorsBlob->GetBufferPointer());

            std::wcerr << errorString << std::endl;
            Check(hResult);
        }

        return shaderCode;
    }
}

class VertexShader
{
public:
    VertexShader(const ComPtr<ID3D11Device1>& device)
    {
        const ComPtr<ID3DBlob> vsBlob = CompileShader(s_vertexShader, "vs_main", "vs_5_0");
        Check(device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_vertexShader.GetAddressOf()));

        const std::array<D3D11_INPUT_ELEMENT_DESC, 2> desc =
        {
            D3D11_INPUT_ELEMENT_DESC{ "POS", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            D3D11_INPUT_ELEMENT_DESC{ "TEX", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 }
        };

        Check(device->CreateInputLayout(desc.data(), static_cast<UINT>(desc.size()), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_inputLayout.GetAddressOf()));

    }

    void Apply(const ComPtr<ID3D11DeviceContext>& context) const
    {
        context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->IASetInputLayout(m_inputLayout.Get());
    }

private:
    ComPtr<ID3D11VertexShader> m_vertexShader;
    ComPtr<ID3D11InputLayout> m_inputLayout;
    static constexpr char s_vertexShader[] = R"(
        struct VS_Input {
            float2 pos : POS;
            float2 uv : TEX;
        };

        struct VS_Output {
            float4 pos : SV_POSITION;
            float2 uv : TEXCOORD;
        };

        VS_Output vs_main(VS_Input input)
        {
            VS_Output output;
            output.pos = float4(input.pos, 0.0f, 1.0f);
            output.uv = input.uv;
            return output;
        }
    )";
};

class PixelShader
{
public:
    PixelShader(const ComPtr<ID3D11Device1>& device)
    {
        const ComPtr<ID3DBlob> psBlob = CompileShader(s_pixelShader, "ps_main", "ps_5_0");
        Check(device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_pixelShader.GetAddressOf()));
    }

    void Apply(const ComPtr<ID3D11DeviceContext>& context) const
    {
        context->PSSetShader(m_pixelShader.Get(), nullptr, 0);
    }

private:
    ComPtr<ID3D11PixelShader> m_pixelShader;
    static constexpr char s_pixelShader[] = R"(
        Texture2D    YTex : register(t0);
        Texture2D    UVTex : register(t1);
        SamplerState mysampler : register(s0);

        struct VS_Output {
            float4 pos : SV_POSITION;
            float2 uv : TEXCOORD;
        };

        float4 ps_main(VS_Output input) : SV_Target
        {
            float y = YTex.Sample(mysampler, input.uv).x;
            float2 UV = UVTex.Sample(mysampler, input.uv).xy;
            float u = UV.x - 0.5;
            float v = UV.y - 0.5;

            float4 rgb;
            rgb.r = y + (1.403 * v);
            rgb.g = y - (0.344 * u) - (0.714 * v);
            rgb.b = y + (1.770 * u);

            return rgb;
        }
    )";

};

class SamplerState
{
public:
    SamplerState(const ComPtr<ID3D11Device1>& device)
    {
        D3D11_SAMPLER_DESC samplerDesc = {};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
        samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
        samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
        samplerDesc.BorderColor[0] = 1.0f;
        samplerDesc.BorderColor[1] = 1.0f;
        samplerDesc.BorderColor[2] = 1.0f;
        samplerDesc.BorderColor[3] = 1.0f;
        samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;

        Check(device->CreateSamplerState(&samplerDesc, m_samplerState.GetAddressOf()));
    }

    void Apply(const ComPtr<ID3D11DeviceContext>& context) const
    {
        ID3D11SamplerState* samplers[] = { m_samplerState.Get() };
        context->PSSetSamplers(0, 1, samplers);
    }

private:
    ComPtr<ID3D11SamplerState> m_samplerState;
};

class Texture
{
public:
    Texture(const ComPtr<ID3D11Device1>& device)
        : m_device{ device }
    {
    }

    void Apply(const ComPtr<ID3D11DeviceContext1>& context)
    {
        if (!m_sharedTexture)
            return;

        ComPtr<IDXGIResource1> dxgiResource;
        Check(m_sharedTexture.As(&dxgiResource));

        HANDLE textureHandle{};
        Check(dxgiResource->GetSharedHandle(&textureHandle));

        ComPtr<ID3D11Texture2D> sharedTex;
        Check(m_device->OpenSharedResource(textureHandle, IID_PPV_ARGS(&sharedTex)));

        D3D11_TEXTURE2D_DESC sharedDesc;
        sharedTex->GetDesc(&sharedDesc);

        ComPtr<ID3D11DeviceContext> ctx;
        m_device->GetImmediateContext(ctx.GetAddressOf());
        ctx->CopySubresourceRegion(m_displayTexture.Get(), 0, 0, 0, 0, sharedTex.Get(), m_displayLayer % sharedDesc.ArraySize, nullptr);

        ID3D11ShaderResourceView* textureViews[] = { m_yView.Get(), m_uvView.Get() };
        context->PSSetShaderResources(0, 2, textureViews);

        ++m_displayLayer;
    }

    void SetTexture(const ComPtr<ID3D11Texture2D>& texture)
    {
        m_sharedTexture = texture;

        D3D11_TEXTURE2D_DESC sharedDesc;
        m_sharedTexture->GetDesc(&sharedDesc);

        D3D11_TEXTURE2D_DESC displayTexDesc = sharedDesc;
        displayTexDesc.ArraySize = 1;
        displayTexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        displayTexDesc.MiscFlags = 0;

        Check(m_device->CreateTexture2D(&displayTexDesc, nullptr, m_displayTexture.ReleaseAndGetAddressOf()));

        D3D11_SHADER_RESOURCE_VIEW_DESC yDesc{};
        yDesc.Format = DXGI_FORMAT_R8_UNORM;
        yDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        yDesc.Texture2D.MostDetailedMip = 0;
        yDesc.Texture2D.MipLevels = 1;

        Check(m_device->CreateShaderResourceView(m_displayTexture.Get(), &yDesc, m_yView.ReleaseAndGetAddressOf()));

        D3D11_SHADER_RESOURCE_VIEW_DESC uvDesc{};
        uvDesc.Format = DXGI_FORMAT_R8G8_UNORM;
        uvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        uvDesc.Texture2D.MostDetailedMip = 0;
        uvDesc.Texture2D.MipLevels = 1;

        Check(m_device->CreateShaderResourceView(m_displayTexture.Get(), &uvDesc, m_uvView.ReleaseAndGetAddressOf()));
    }

private:
    ComPtr<ID3D11Device1> m_device;
    ComPtr<ID3D11Texture2D> m_displayTexture;
    ComPtr<ID3D11ShaderResourceView> m_yView;
    ComPtr<ID3D11ShaderResourceView> m_uvView;
    ComPtr<ID3D11Texture2D> m_sharedTexture;
    unsigned int m_displayLayer = 0;
};

class Quad
{
public:
    Quad(const ComPtr<ID3D11Device1>& device)
        : m_vertexShader{ device }
        , m_pixelShader{ device }
        , m_sampler{ device }
        , m_texture{ device }
    {
        using Vertex = std::array<float, 4>;
        std::array<Vertex, 6> vertexData = {
            -0.5f,  0.5f, 0.f, 0.f,
            0.5f, -0.5f, 1.f, 1.f,
            -0.5f, -0.5f, 0.f, 1.f,
            -0.5f,  0.5f, 0.f, 0.f,
            0.5f,  0.5f, 1.f, 0.f,
            0.5f, -0.5f, 1.f, 1.f
        };

        m_stride = static_cast<UINT>(vertexData.front().size()) * sizeof(float);
        m_vertexCount = static_cast<UINT>(vertexData.size());

        D3D11_BUFFER_DESC vertexBufferDesc = {};
        vertexBufferDesc.ByteWidth = sizeof(vertexData);
        vertexBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
        vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

        const D3D11_SUBRESOURCE_DATA data = { vertexData.data() };
        Check(device->CreateBuffer(&vertexBufferDesc, &data, m_vertexBuffer.GetAddressOf()));
    }

    void Draw(const ComPtr<ID3D11DeviceContext1>& context)
    {
        m_vertexShader.Apply(context);
        m_pixelShader.Apply(context);
        m_sampler.Apply(context);
        m_texture.Apply(context);

        ID3D11Buffer* vertBuffers[] = { m_vertexBuffer.Get() };
        constexpr UINT offset = 0;
        context->IASetVertexBuffers(0, 1, vertBuffers, &m_stride, &offset);
        context->Draw(m_vertexCount, 0);
    }

    void SetTexture(const ComPtr<ID3D11Texture2D>& texture)
    {
        m_texture.SetTexture(texture);
    }

private:
    VertexShader m_vertexShader;
    PixelShader m_pixelShader;
    SamplerState m_sampler;
    Texture m_texture;
    ComPtr<ID3D11Buffer> m_vertexBuffer;
    UINT m_vertexCount;
    UINT m_stride;
};

class SwapChain
{
public:
    SwapChain(const ComPtr<ID3D11Device1>& device, HWND wnd)
    {
        DXGI_SWAP_CHAIN_DESC1 swapDesc{};
        swapDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        swapDesc.BufferCount = 2;
        swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapDesc.SampleDesc = { 1, 0 };
        swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

        const auto factory = GetFactory(device);
        Check(factory->CreateSwapChainForHwnd(device.Get(), wnd, &swapDesc, nullptr, nullptr, m_swapChain.GetAddressOf()));
    }

    void Apply(const ComPtr<ID3D11DeviceContext>& context) const
    {
        constexpr float bgColor[] = { 0.1f, 0.2f, 0.6f, 1.0f };
        context->ClearRenderTargetView(m_frameBufferView.Get(), bgColor);

        ID3D11RenderTargetView* renderTargets[] = { m_frameBufferView.Get() };
        context->OMSetRenderTargets(1, renderTargets, nullptr);
    }

    void Resize(const ComPtr<ID3D11Device1>& device, SIZE size)
    {
        m_frameBufferView = nullptr;

        Check(m_swapChain->ResizeBuffers(0, size.cx, size.cy, DXGI_FORMAT_UNKNOWN, 0));

        ComPtr<ID3D11Texture2D> frameBuffer;
        Check(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&frameBuffer)));

        Check(device->CreateRenderTargetView(frameBuffer.Get(), nullptr, m_frameBufferView.GetAddressOf()));
    }

    void Present() const
    {
        constexpr DXGI_PRESENT_PARAMETERS presentParams{};
        const HRESULT hr = m_swapChain->Present1(0, 0, &presentParams);

        if (hr == DXGI_STATUS_OCCLUDED)
            return;

        Check(hr);
    }

private:
    static ComPtr<IDXGIFactory2> GetFactory(const ComPtr<ID3D11Device1>& device)
    {
        ComPtr<IDXGIDevice> dxgiDevice;
        Check(device.As(&dxgiDevice));

        ComPtr<IDXGIAdapter1> dxgiAdapter;
        Check(dxgiDevice->GetParent(IID_PPV_ARGS(&dxgiAdapter)));

        ComPtr<IDXGIFactory2> factory;
        Check(dxgiAdapter->GetParent(IID_PPV_ARGS(&factory)));

        return factory;
    }

    ComPtr<IDXGISwapChain1> m_swapChain;
    ComPtr<ID3D11RenderTargetView> m_frameBufferView;
};

class VideoProducer
{
public:
    VideoProducer(int width, int height, int slices)
    {
        m_device->GetImmediateContext1(m_context.GetAddressOf());

        D3D11_TEXTURE2D_DESC esc = {};
        esc.Width = width;
        esc.Height = height;
        esc.MipLevels = 1;
        esc.ArraySize = slices;
        esc.Format = DXGI_FORMAT_NV12;
        esc.SampleDesc = { 1, 0 };
        esc.Usage = D3D11_USAGE_DEFAULT;
        esc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_DECODER;
        esc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

        Check(m_device->CreateTexture2D(&esc, nullptr, m_texture.GetAddressOf()));

        Produce();

    }

    ~VideoProducer()
    {
        m_stopped.store(true, std::memory_order_relaxed);
        if (m_thread.joinable())
            m_thread.join();
    }

    ComPtr<ID3D11Texture2D> GetTexture()
    {
        return m_texture;
    }

    void Start()
    {
        m_thread = std::thread([this]
            {
                while (!m_stopped.load(std::memory_order_relaxed))
                    Produce();
            });
    }

private:

    std::vector<unsigned char>  CreateYUV420SampleImage(int chromaRotation, int width, int height)
    {
        const size_t rowPitch = width;
        std::vector<unsigned char> bitmap(rowPitch * height * 3 / 2, 0);

        // Populate the Y plane
        for (size_t j = 0; j < height; ++j)
            for (size_t i = 0; i < width; ++i)
                bitmap[i + j * rowPitch] = static_cast<unsigned char>((j * 255) / height);


        // Populate UV plane (downsampled by 2 in both directions)
        const size_t nLuma = rowPitch * height;
        auto* UV = bitmap.data() + nLuma;

        const size_t uvWidth = width / 2;
        const size_t uwHeight = height / 2;
        for (size_t j = 0; j < uwHeight; ++j) {
            for (size_t i = 0; i < uvWidth; ++i) {
                *UV++ = static_cast<unsigned char>(j * 255 / uwHeight + chromaRotation);
                *UV++ = static_cast<unsigned char>(i * 255 / uvWidth);
            }
        }
        return bitmap;
    }

    /** Loops over texture array and updates each slice with a new YUV image */
    void Produce()
    {
        D3D11_TEXTURE2D_DESC desc = {};
        m_texture->GetDesc(&desc);

        for (UINT sliceIdx = 0; sliceIdx < desc.ArraySize; ++sliceIdx)
        {
            const auto sliceImage = CreateYUV420SampleImage(m_time + sliceIdx, desc.Width, desc.Height);
            D3D11_SUBRESOURCE_DATA data{};
            data.pSysMem = sliceImage.data();
            data.SysMemPitch = desc.Width;

            D3D11_TEXTURE2D_DESC sliceDesc = desc;
            sliceDesc.ArraySize = 1;
            sliceDesc.Usage = D3D11_USAGE_IMMUTABLE;

            ComPtr<ID3D11Texture2D> slice;
            Check(m_device->CreateTexture2D(&sliceDesc, &data, slice.GetAddressOf()));

            m_context->CopySubresourceRegion1(m_texture.Get(), sliceIdx, 0, 0, 0, slice.Get(), 0, nullptr, D3D11_COPY_DISCARD);
        }

        ++m_time;
    }

    std::thread m_thread;

    std::atomic_bool m_stopped = false;
    ComPtr<ID3D11Device1> m_device = CreateDevice();
    ComPtr<ID3D11DeviceContext1> m_context;

    ComPtr<ID3D11Texture2D> m_texture;
    int m_time = 0;
};

struct VideoWindow
{
    explicit VideoWindow()
    {
    }

    ~VideoWindow()
    {
        DestroyWindow(m_wnd);
    }

    void Show() const
    {
        ShowWindow(m_wnd, SW_NORMAL);
    }

    virtual LRESULT WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
    {
        if (uMsg == WM_CLOSE)
            PostQuitMessage(0);

        if (uMsg == WM_PAINT)
        {
            ComPtr<ID3D11DeviceContext1> context;
            m_device->GetImmediateContext1(context.GetAddressOf());

            const D3D11_VIEWPORT viewPorts[] = { {0.0f, 0.0f, static_cast<FLOAT>(m_size.cx), static_cast<FLOAT>(m_size.cy), 0.0f, 1.0f} };
            context->RSSetViewports(1, viewPorts);

            m_swapChain.Apply(context);
            m_quad.Draw(context);
            m_swapChain.Present();

            return 0;
        }

        if (uMsg == WM_SIZE)
        {
            m_size.cx = LOWORD(lParam);
            m_size.cy = HIWORD(lParam);
            m_swapChain.Resize(m_device, m_size);
            return 0;
        }

        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    void SetTexture(const ComPtr<ID3D11Texture2D>& texture)
    {
        m_quad.SetTexture(texture);
    }

private:
    std::vector<std::thread> m_threads;
    const HWND m_wnd = CreateWindowExW(0, L"MyWindowClass", L"MyTestWindow",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        nullptr, nullptr, GetModuleHandle(nullptr), this);

    const ComPtr<ID3D11Device1> m_device = CreateDevice();
    SwapChain m_swapChain{ m_device, m_wnd };
    Quad m_quad{ m_device };
    SIZE m_size{};
};

struct WindowClass
{
    WindowClass()
    {
        WNDCLASSW unicodeWndCls{};

        unicodeWndCls.style = 0;
        unicodeWndCls.lpfnWndProc = static_cast<WNDPROC>(WndProc);
        unicodeWndCls.cbClsExtra = 0;
        unicodeWndCls.cbWndExtra = 0;
        unicodeWndCls.hInstance = GetModuleHandle(nullptr);
        unicodeWndCls.hIcon = nullptr;
        unicodeWndCls.hCursor = LoadCursor(nullptr, IDC_IBEAM);
        unicodeWndCls.hbrBackground = nullptr;
        unicodeWndCls.lpszMenuName = nullptr;
        unicodeWndCls.lpszClassName = m_className;

        RegisterClassW(&unicodeWndCls);
    }

    virtual ~WindowClass()
    {
        UnregisterClassW(m_className, nullptr);
    }

    const wchar_t* ClassName() const
    {
        return m_className;
    }

    static void Run()
    {
        MSG msg{};
        while (GetMessage(&msg, nullptr, 0, 0) > 0)
        {
            if (msg.message == WM_QUIT)
                break;

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
        VideoWindow* window = nullptr;
        if (uMsg == WM_NCCREATE) {
            const auto lpcs = reinterpret_cast<LPCREATESTRUCT>(lParam);
            window = static_cast<VideoWindow*>(lpcs->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
        }
        else {
            window = reinterpret_cast<VideoWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        }

        if (window) {
            return window->WndProc(hwnd, uMsg, wParam, lParam);
        }

        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    const wchar_t* m_className = L"MyWindowClass";
};

int main()
{
    WindowClass windowClass{};
    VideoProducer producer{1200, 1024, 64};

    VideoWindow window;
    window.SetTexture(producer.GetTexture());
    producer.Start();
    window.Show();

    windowClass.Run();

    return 0;
}
