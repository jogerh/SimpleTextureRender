#include <iostream>
#include <iostream>
#include <Windows.h>
#include <D3D11_1.h>
#include <wrl.h>
#include <winrt/base.h>
#include <d3dcompiler.h>
#include <d3dcommon.h>

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
        Texture2D    mytexture : register(t0);
        SamplerState mysampler : register(s0);

        struct VS_Output {
            float4 pos : SV_POSITION;
            float2 uv : TEXCOORD;
        };

        float4 ps_main(VS_Output input) : SV_Target
        {
            return mytexture.Sample(mysampler, input.uv);   
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
    {
        constexpr int texWidth = 200;
        constexpr int texHeight = 100;
        const std::vector<std::array<unsigned char, 4>> testTextureBytes(texWidth * texHeight, { 255, 0, 0, 255 });
        constexpr int texBytesPerRow = 4 * texWidth;

        // Create Texture
        D3D11_TEXTURE2D_DESC textureDesc = {};
        textureDesc.Width = texWidth;
        textureDesc.Height = texHeight;
        textureDesc.MipLevels = 1;
        textureDesc.ArraySize = 1;
        textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.Usage = D3D11_USAGE_IMMUTABLE;
        textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA data = {};
        data.pSysMem = testTextureBytes.data();
        data.SysMemPitch = texBytesPerRow;

        Check(device->CreateTexture2D(&textureDesc, &data, m_texture.GetAddressOf()));
        Check(device->CreateShaderResourceView(m_texture.Get(), nullptr, m_textureView.GetAddressOf()));
    }

    void Apply(const ComPtr<ID3D11DeviceContext1>& context) const
    {
        ID3D11ShaderResourceView* textures[] = { m_textureView.Get() };
        context->PSSetShaderResources(0, 1, textures);
    }

private:
    ComPtr<ID3D11Texture2D> m_texture;
    ComPtr<ID3D11ShaderResourceView> m_textureView;
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

    void Draw(const ComPtr<ID3D11DeviceContext1>& context) const
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
        swapDesc.SampleDesc.Count = 1;
        swapDesc.SampleDesc.Quality = 0;
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

struct Window
{
    explicit Window()
    {
    }

    ~Window()
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


            const D3D11_VIEWPORT viewPorts [] = {{0.0f, 0.0f, static_cast<FLOAT>(m_size.cx), static_cast<FLOAT>(m_size.cy), 0.0f, 1.0f}};
            context->RSSetViewports(1, viewPorts);

            m_swapChain.Apply(context);

            m_quad.Draw(context);
            m_swapChain.Present();
        }
        else if (uMsg == WM_SIZE)
        {
            m_size.cx = LOWORD(lParam);
            m_size.cy = HIWORD(lParam);
            m_swapChain.Resize(m_device, m_size);
        }

        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

private:

    const HWND m_wnd = CreateWindowExW(0, L"MyWindowClass", L"MyTestWindow",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        nullptr, nullptr, GetModuleHandle(nullptr), this);

    const ComPtr<ID3D11Device1> m_device = CreateDevice();
    SwapChain m_swapChain{ m_device, m_wnd };
    const Quad m_quad{ m_device };
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
        Window* window = nullptr;
        if (uMsg == WM_NCCREATE) {
            const auto lpcs = reinterpret_cast<LPCREATESTRUCT>(lParam);
            window = static_cast<Window*>(lpcs->lpCreateParams);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
        }
        else {
            window = reinterpret_cast<Window*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        }

        if (window) {
            return window->WndProc(hwnd, uMsg, wParam, lParam);
        }

        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    const wchar_t* m_className = L"MyWindowClass";
};



//
//
//ComPtr<ID3D11Texture2D> CreateTexture(const ComPtr<ID3D11Device1>& dev)
//{
//    D3D11_TEXTURE2D_DESC desc{};
//    desc.Format = DXGI_FORMAT_NV12;
//    desc.Width = 1280;
//    desc.Height = 720;
//    desc.MipLevels = 1;
//    desc.ArraySize = 33;
//    desc.SampleDesc = { 1, 0 };
//    desc.Usage = D3D11_USAGE_DEFAULT;
//    desc.BindFlags = 520;
//    desc.MiscFlags = 2;
//
//    ComPtr<ID3D11Texture2D> tex;
//    Check(dev->CreateTexture2D(&desc, nullptr, tex.GetAddressOf()));
//
//    return tex;
//}



int main()
{
    WindowClass c{};
    Window w;

    w.Show();
    c.Run();

    return 0;
}
