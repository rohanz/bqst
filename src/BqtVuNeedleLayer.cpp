// Builds without the native needle (other platforms, or BQST_VU_NATIVE_NEEDLE off): the layer is
// never available and the needle is always painted. The macOS layer is in BqtVuNeedleLayer_mac.mm.

#include "BqtVuNeedleLayer.h"

#if ! BQST_VU_NATIVE_NEEDLE

namespace bqst::vu
{
struct NativeNeedle::Impl {};

NativeNeedle::NativeNeedle() = default;
NativeNeedle::~NativeNeedle() = default;
bool NativeNeedle::present(void*, const NeedlePicture&, double) { return false; }
void NativeNeedle::hide() {}
void NativeNeedle::detach() {}
NativeNeedle::Probe NativeNeedle::probe() const { return {}; }
} // namespace bqst::vu

#endif
