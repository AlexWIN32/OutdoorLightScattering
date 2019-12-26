#pragma once

#include <windows.h>
#include <atlcomcli.h>
#include <D3DX11.h>

HRESULT CreateTexture(CComPtr<ID3D11ShaderResourceView> &SRV, CComPtr<ID3D11RenderTargetView> &RTV, ID3D11Device* pd3dDevice, UINT Width, UINT Height, DXGI_FORMAT Format);

HRESULT CreateTexture(CComPtr<ID3D11ShaderResourceView> &SRV, CComPtr<ID3D11UnorderedAccessView> &UAV, ID3D11Device* pd3dDevice, UINT Width, UINT Height, DXGI_FORMAT Format);

HRESULT CreateTexture(CComPtr<ID3D11DepthStencilView> &DSV, ID3D11Device* pd3dDevice, UINT Width, UINT Height, DXGI_FORMAT Format);
