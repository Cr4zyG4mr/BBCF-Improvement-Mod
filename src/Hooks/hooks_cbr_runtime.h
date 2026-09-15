#pragma once

// Installs the legacy CBR input hooks using a compatibility wrapper that preserves
// BBCF's register/flag state and keeps the CBR logic dormant until match data is ready.
bool EnsureCbrRuntimeHooksInstalled();
bool IsCbrRuntimeHooksInstalled();
