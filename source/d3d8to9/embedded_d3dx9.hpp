#pragma once

bool InitEmbeddedD3DX();

// HLSL through the embedded d3dcompiler_47 (D3DCompile), for the post-process presets. Loads the
// embedded DLLs on first use. ID3DXBuffer and ID3DBlob share their layout, as in the assembler path.
HRESULT EmbeddedD3DCompile(const char *pSource, size_t nLength, const char *szName, const char *szEntryPoint,
	const char *szProfile, struct ID3DXBuffer **ppCode, struct ID3DXBuffer **ppErrors);
