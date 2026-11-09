#pragma once

#include "common/Types.h"

namespace pvf::model {

// Generates a version 4 UUID without braces.
Uuid newUuid();

// Accepts only the canonical 8-4-4-4-12 hexadecimal form (no braces).
bool isValidUuid(const Uuid& uuid);

} // namespace pvf::model
