#pragma once

#include <cstdint>

#include "dxso_common.h"
#include "dxso_decoder.h"
#include "../d3d9/d3d9_caps.h"

namespace dxvk {

  enum class DxsoBindingType : uint32_t {
    ConstantBuffer,
    Image,
  };

  enum DxsoConstantBuffers : uint32_t {
    VSConstantBuffer = 0,
    VSClipPlanes     = 1,
    VSFixedFunction  = 2,
    VSVertexBlendData = 3,
    VSCount,

    PSConstantBuffer = 0,
    PSFixedFunction  = 1,
    PSShared         = 2,
    PSCount
  };

  // D3D9 samplers are untyped. MoltenVK cannot represent the several image
  // types DXVK aliases to a single binding, so reserve five slots per sampler:
  // 2D, 3D, cube, 2D shadow, and cube shadow. The full range is reserved even
  // when de-aliasing is disabled so resource slot numbering stays stable.
  constexpr uint32_t SamplerVariantCount = 5;
  constexpr uint32_t SamplerVariantDepth2D = 3;
  constexpr uint32_t SamplerVariantDepthCube = 4;

  constexpr uint32_t samplerTypeVariant(uint32_t samplerType, bool depth) {
    return !depth ? samplerType
      : (samplerType == 2u /* SamplerTypeTextureCube */
          ? SamplerVariantDepthCube : SamplerVariantDepth2D);
  }

  constexpr uint32_t computeResourceSlotId(
        DxsoProgramType shaderStage,
        DxsoBindingType bindingType,
        uint32_t        bindingIndex) {
    const uint32_t stageOffset = (DxsoConstantBuffers::VSCount
      + caps::MaxSamplers * SamplerVariantCount) * uint32_t(shaderStage);

    if (bindingType == DxsoBindingType::ConstantBuffer)
      return bindingIndex + stageOffset;
    else // if (bindingType == DxsoBindingType::Image)
      return bindingIndex * SamplerVariantCount + stageOffset
        + (shaderStage == DxsoProgramType::PixelShader ? PSCount : VSCount);
  }

  // TODO: Intergrate into compute resource slot ID/refactor all of this?
  constexpr uint32_t getSWVPBufferSlot() {
    return DxsoConstantBuffers::VSCount + caps::MaxSamplers * SamplerVariantCount
      + DxsoConstantBuffers::PSCount + caps::MaxSamplers * SamplerVariantCount + 1;
  }

  uint32_t RegisterLinkerSlot(DxsoSemantic semantic);

}
