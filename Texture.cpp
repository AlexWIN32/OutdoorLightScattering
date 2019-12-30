#include "Texture.h"
#include "stdafx.h"

HRESULT CreateTexture(CComPtr<ID3D11ShaderResourceView> &SRV, CComPtr<ID3D11RenderTargetView> &RTV, ID3D11Device* pd3dDevice, UINT Width, UINT Height, DXGI_FORMAT Format)
{
    D3D11_TEXTURE2D_DESC CoordinateTexDesc =
    {
        Width,                                                 //UINT Width;
        Height,                                                //UINT Height;
        1,                                                     //UINT MipLevels;
        1,                                                     //UINT ArraySize;
        Format,                                                //DXGI_FORMAT Format;
        {1,0},                                                 //DXGI_SAMPLE_DESC SampleDesc;
        D3D11_USAGE_DEFAULT,                                   //D3D11_USAGE Usage;
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, //UINT BindFlags;
        0,                                                     //UINT MiscFlags;
    };

    SRV.Release();
    RTV.Release();

    HRESULT hr;
    CComPtr<ID3D11Texture2D> tex;
    V_RETURN(pd3dDevice->CreateTexture2D(&CoordinateTexDesc, NULL, &tex));
    V_RETURN(pd3dDevice->CreateShaderResourceView(tex, NULL, &SRV));
    V_RETURN(pd3dDevice->CreateRenderTargetView(tex, NULL, &RTV));

    return S_OK;
}

HRESULT CreateTexture(CComPtr<ID3D11ShaderResourceView> &SRV, CComPtr<ID3D11UnorderedAccessView> &UAV, ID3D11Device* pd3dDevice, UINT Width, UINT Height, DXGI_FORMAT Format)
{
    D3D11_TEXTURE2D_DESC CoordinateTexDesc =
    {
        Width,                                                    //UINT Width;
        Height,                                                   //UINT Height;
        1,                                                        //UINT MipLevels;
        1,                                                        //UINT ArraySize;
        Format,                                                   //DXGI_FORMAT Format;
        {1,0},                                                    //DXGI_SAMPLE_DESC SampleDesc;
        D3D11_USAGE_DEFAULT,                                      //D3D11_USAGE Usage;
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS, //UINT BindFlags;
        0,                                                        //UINT CPUAccessFlags;
        0,                                                        //UINT MiscFlags;
    };
    SRV.Release();
    UAV.Release();

    HRESULT hr;
    CComPtr<ID3D11Texture2D> tex;
    V_RETURN(pd3dDevice->CreateTexture2D(&CoordinateTexDesc, NULL, &tex));
    V_RETURN(pd3dDevice->CreateShaderResourceView(tex, NULL, &SRV));
    V_RETURN(pd3dDevice->CreateUnorderedAccessView(tex, NULL, &UAV));

    return S_OK;
}

HRESULT CreateTexture(CComPtr<ID3D11DepthStencilView> &DSV, ID3D11Device* pd3dDevice, UINT Width, UINT Height, DXGI_FORMAT Format)
{
    D3D11_TEXTURE2D_DESC CoordinateTexDesc =
    {
        Width,                                                    //UINT Width;
        Height,                                                    //UINT Height;
        1,                                                        //UINT MipLevels;
        1,                                                        //UINT ArraySize;
        Format,                                                   //DXGI_FORMAT Format;
        {1,0},                                                    //DXGI_SAMPLE_DESC SampleDesc;
        D3D11_USAGE_DEFAULT,                                      //D3D11_USAGE Usage;
        D3D11_BIND_DEPTH_STENCIL,                                 //UINT BindFlags;
        0,                                                        //UINT CPUAccessFlags;
        0,                                                        //UINT MiscFlags;
    };

    DSV.Release();

    HRESULT hr;
    CComPtr<ID3D11Texture2D> tex;
    V_RETURN(pd3dDevice->CreateTexture2D(&CoordinateTexDesc, NULL, &tex));

    V_RETURN(pd3dDevice->CreateDepthStencilView(tex, NULL, &DSV));

    return S_OK;
}