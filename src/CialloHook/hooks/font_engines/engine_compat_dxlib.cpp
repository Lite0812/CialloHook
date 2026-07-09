#include "engine_compat_file.h"

#include "../../../RuntimeCore/hook/Hook_API.h"

using namespace Rut::HookX;

namespace CialloHook
{
    namespace EngineCompat
    {
        void ApplyDxLibFileCompat(const AppSettings& settings, const EngineCompatState& state)
        {
            if (HasEngine(state, EngineDxLib) || HasEngine(state, EngineMED))
            {
                AddEngineDxLibFontCacheRule(ResolveReplacementFaceName(settings.font).c_str());
            }
        }
    }
}
