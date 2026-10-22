#pragma once
#include <d3d11.h>

namespace resorep::proxy {

// Redirects CreateTexture2D on this device's vtable to the replacement logic. Idempotent.
void InstallTextureHook(ID3D11Device* device);

}  // namespace resorep::proxy
