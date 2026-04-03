#include "renderer/Material.h"

namespace Horo {

void Material::Apply() const {
  if (!shader || !shader->IsValid())
    return;
  shader->Bind();
  shader->SetVec4("u_color", color);
  shader->SetFloat("u_roughness", roughness);
  shader->SetFloat("u_metallic", metallic);

  if (albedoMap && albedoMap->IsValid())
    albedoMap->Bind(0);
  shader->SetInt("u_albedoMap", 0);
  shader->SetInt("u_hasTexture", (albedoMap && albedoMap->IsValid()) ? 1 : 0);
  shader->SetFloat("u_uvScale", uvScale);
}

}  // namespace Horo
