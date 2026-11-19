#pragma once

#include <doxydown/Doc.hpp>
#include <doxydown/TextLineCursor.hpp>

class DocParser {
public:
  virtual Doc parse(const TextLineCursor &comment) = 0;

  virtual ~DocParser() = default;
};
