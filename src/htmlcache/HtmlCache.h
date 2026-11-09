#pragma once

#include "htmlcache/DiskCache.h"
#include "htmlcache/FrameCache.h"
#include "htmlcache/FrameKey.h"
#include "htmlcache/MemoryCache.h"
#include "htmlcache/TieredCache.h"

// Process-local HTML frame cache. No external service is required: MemoryCache
// is volatile and empty after restart, while DiskCache persists WebP frames on
// local disk. Typical setup: TieredCache(memory, disk).
