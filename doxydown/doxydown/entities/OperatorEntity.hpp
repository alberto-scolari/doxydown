#pragma once

#include "ContainerBases.hpp"
#include "TemplateCodeEntity.hpp"

class OperatorEntity : public TemplateCodeEntity<false>, public TypeContainer, public FunctionContainer, public VariableContainer, public EnumContainer, public TypedefContainer {
public:
  using TemplateCodeEntity<false>::TemplateCodeEntity;
};
