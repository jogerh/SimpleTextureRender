#include <iostream>
#include <iostream>
#include <Windows.h>
#include <D3D11_1.h>
#include <wrl.h>
#include <array>
#include <vector>
#include <d3dcompiler.h>
#include <d3dcommon.h>
#include <mutex>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

namespace {

    void Check(HRESULT hr)
    {
        if (hr == S_OK)
            return;

        throw std::system_error(hr, std::system_category());
    }

    ComPtr<ID3D11Device1> CreateDeviceOnAdapter(int adapterIndex)
    {
        ComPtr<IDXGIFactory1> factory;
        Check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));

        ComPtr<IDXGIAdapter1> adapter;
        Check(factory->EnumAdapters1(adapterIndex, adapter.GetAddressOf()));

        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        Check(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, device.GetAddressOf(), {}, context.GetAddressOf()));

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

            std::wcerr << "Error compiling shader: " << errorString << std::endl;

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
        : m_displayTexture{ CreateTexture(device) }
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC yDesc{};
        yDesc.Format = DXGI_FORMAT_R8_UNORM;
        yDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        yDesc.Texture2D.MostDetailedMip = 0;
        yDesc.Texture2D.MipLevels = 1;

        Check(device->CreateShaderResourceView(m_displayTexture.Get(), &yDesc, m_yView.ReleaseAndGetAddressOf()));

        D3D11_SHADER_RESOURCE_VIEW_DESC uvDesc{};
        uvDesc.Format = DXGI_FORMAT_R8G8_UNORM;
        uvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        uvDesc.Texture2D.MostDetailedMip = 0;
        uvDesc.Texture2D.MipLevels = 1;

        Check(device->CreateShaderResourceView(m_displayTexture.Get(), &uvDesc, m_uvView.ReleaseAndGetAddressOf()));
    }

    void Apply(const ComPtr<ID3D11DeviceContext1>& context)
    {
        ID3D11ShaderResourceView* textureViews[] = { m_yView.Get(), m_uvView.Get() };
        context->PSSetShaderResources(0, 2, textureViews);
    }

private:

    std::vector<unsigned char> CreateYUV420SampleImage(int width, int height)
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
                *UV++ = static_cast<unsigned char>(j * 255 / uwHeight);
                *UV++ = static_cast<unsigned char>(i * 255 / uvWidth);
            }
        }
        return bitmap;
    }

    ComPtr<ID3D11Texture2D> CreateTexture(const ComPtr<ID3D11Device1>& device)
    {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = 1200;
        desc.Height = 1000;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_NV12;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.MipLevels = 1;
        desc.SampleDesc = { 1, 0 };
        desc.Usage = D3D11_USAGE_IMMUTABLE;

        const auto bitmap = CreateYUV420SampleImage(desc.Width, desc.Height);

        D3D11_SUBRESOURCE_DATA data{};
        data.pSysMem = bitmap.data();
        data.SysMemPitch = desc.Width;

        ComPtr<ID3D11Texture2D> texture;
        Check(device->CreateTexture2D(&desc, &data, texture.GetAddressOf()));

        return texture;
    }

    ComPtr<ID3D11Texture2D> m_displayTexture;
    ComPtr<ID3D11ShaderResourceView> m_yView;
    ComPtr<ID3D11ShaderResourceView> m_uvView;
};

class Quad
{
public:
    Quad(const ComPtr<ID3D11Device1>& device)
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

    void Apply(const ComPtr<ID3D11DeviceContext>& context) const
    {
        ID3D11Buffer* buffers[] = { m_vertexBuffer.Get() };
        constexpr UINT offset = 0;
        context->IASetVertexBuffers(0, 1, buffers, &m_stride, &offset);
        context->Draw(m_vertexCount, 0);
    }

private:
    ComPtr<ID3D11Buffer> m_vertexBuffer;
    UINT m_vertexCount;
    UINT m_stride;
};

class VideoRenderer
{
public:
    VideoRenderer(const ComPtr<ID3D11Device1>& device)
        : m_vertexShader{ device }
        , m_pixelShader{ device }
        , m_sampler{ device }
        , m_texture{ device }
        , m_quad{ device }
    {
    }

    void Draw(const ComPtr<ID3D11DeviceContext1>& context)
    {
        m_vertexShader.Apply(context);
        m_pixelShader.Apply(context);
        m_sampler.Apply(context);
        m_texture.Apply(context);
        m_quad.Apply(context);
    }

private:
    VertexShader m_vertexShader;
    PixelShader m_pixelShader;
    SamplerState m_sampler;
    Texture m_texture;
    Quad m_quad;
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

struct VideoWindow
{
    explicit VideoWindow(int adapterIndex)
        : m_device{ CreateDeviceOnAdapter(adapterIndex) }
        , m_swapChain{ m_device, m_wnd }
        , m_quad{ m_device }
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

        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }

private:
    std::vector<std::thread> m_threads;
    const HWND m_wnd = CreateWindowExW(0, L"MyWindowClass", L"YUV 4:2:0",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        nullptr, nullptr, GetModuleHandle(nullptr), this);

    const ComPtr<ID3D11Device1> m_device;
    SwapChain m_swapChain;
    VideoRenderer m_quad;
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

        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }

    const wchar_t* m_className = L"MyWindowClass";
};


int main()
{
    constexpr int adapterIndex = 0;

    try {
        WindowClass windowClass{};
        VideoWindow window{ adapterIndex };

        window.Show();

        windowClass.Run();
    }
    catch (const std::exception& e)
    {
        std::wcerr << e.what() << std::endl;
        return 1;
    }
    return 0;
}
