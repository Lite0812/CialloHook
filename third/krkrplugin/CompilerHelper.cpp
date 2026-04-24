#include "CompilerHelper.h"
#include "Pe.h"

namespace
{
    struct MsvcTypeDescriptor
    {
        const void* pVFTable;
        void* spare;
        char name[1];
    };

    struct MsvcRttiCompleteObjectLocator
    {
        uint32_t signature;
        uint32_t offset;
        uint32_t cdOffset;
#if defined(_M_X64)
        int32_t pTypeDescriptor;
        int32_t pClassDescriptor;
        int32_t pSelf;

        const MsvcTypeDescriptor* GetTypeDescriptor() const
        {
            const auto* imageBase = reinterpret_cast<const BYTE*>(this) - pSelf;
            return reinterpret_cast<const MsvcTypeDescriptor*>(imageBase + pTypeDescriptor);
        }
#else
        const MsvcTypeDescriptor* pTypeDescriptor;
        const void* pClassDescriptor;

        const MsvcTypeDescriptor* GetTypeDescriptor() const
        {
            return pTypeDescriptor;
        }
#endif
    };

    static std::string BuildMsvcRttiClassName(const std::string& className)
    {
        std::string mangled;
        size_t segmentEnd = className.size();
        while (segmentEnd > 0)
        {
            size_t separator = className.rfind("::", segmentEnd - 1);
            size_t segmentStart = separator == std::string::npos ? 0 : separator + 2;
            if (!mangled.empty())
            {
                mangled.push_back('@');
            }
            mangled.append(className, segmentStart, segmentEnd - segmentStart);
            if (separator == std::string::npos)
            {
                break;
            }
            segmentEnd = separator;
        }
        return mangled;
    }
}

void CompilerHelper::Analyze()
{
#if defined(_M_IX86)
    if (static constexpr auto PATTERN_BORLAND = "Borland"; Pe::FindData(PATTERN_BORLAND, strlen(PATTERN_BORLAND), TRUE))
        CompilerType = CompilerType::Borland;
    else
        CompilerType = CompilerType::Msvc;
#else
    CompilerType = CompilerType::Msvc;
#endif
}

void** CompilerHelper::FindVTable(const std::string& className)
{
    return FindVTable(GetModuleHandleW(nullptr), className);
}

void** CompilerHelper::FindVTable(HMODULE hModule, const std::string& className)
{
    if (!hModule || className.empty() || CompilerType != CompilerType::Msvc)
    {
        return nullptr;
    }

    const BYTE* moduleStart = reinterpret_cast<const BYTE*>(hModule);
    const BYTE* moduleEnd = moduleStart + Pe::GetOptionalHeader(hModule)->SizeOfImage;
    const std::string mangledClassName = BuildMsvcRttiClassName(className);

    const auto* pNtHeaders = Pe::GetNtHeaders(hModule);
    const auto* pSectionHeaders = IMAGE_FIRST_SECTION(pNtHeaders);

    auto isCodePtr = [&](const void* pFunc) -> bool
    {
        const BYTE* func = reinterpret_cast<const BYTE*>(pFunc);
        if (func < moduleStart || func >= moduleEnd)
        {
            return false;
        }

        for (WORD i = 0; i < pNtHeaders->FileHeader.NumberOfSections; ++i)
        {
            const IMAGE_SECTION_HEADER& section = pSectionHeaders[i];
            if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) == 0)
            {
                continue;
            }

            const BYTE* sectionStart = moduleStart + section.VirtualAddress;
            const BYTE* sectionEnd = sectionStart + std::max<DWORD>(section.Misc.VirtualSize, section.SizeOfRawData);
            if (func >= sectionStart && func < sectionEnd)
            {
                return true;
            }
        }

        return false;
    };

    for (WORD i = 0; i < pNtHeaders->FileHeader.NumberOfSections; ++i)
    {
        const IMAGE_SECTION_HEADER& section = pSectionHeaders[i];
        if ((section.Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0)
        {
            continue;
        }

        BYTE* sectionStart = const_cast<BYTE*>(moduleStart + section.VirtualAddress);
        size_t sectionSize = std::max<DWORD>(section.Misc.VirtualSize, section.SizeOfRawData);
        if (sectionSize < sizeof(void*) * 4)
        {
            continue;
        }

        for (size_t offset = sizeof(void*) * 3; offset + sizeof(void*) <= sectionSize; offset += sizeof(void*))
        {
            void** pVTable = reinterpret_cast<void**>(sectionStart + offset);
            if (!isCodePtr(*pVTable))
            {
                continue;
            }

            if (HasMsvcTypeDescriptor(pVTable, mangledClassName, moduleStart, moduleEnd))
            {
                return pVTable;
            }
        }
    }

    return nullptr;
}

bool CompilerHelper::HasMsvcTypeDescriptor(void** pVTable, const std::string& className, const BYTE* pModuleStart, const BYTE* pModuleEnd)
{
    const auto* pLocator = reinterpret_cast<const MsvcRttiCompleteObjectLocator*>(pVTable[-1]);
    if (reinterpret_cast<const BYTE*>(pLocator) < pModuleStart || reinterpret_cast<const BYTE*>(pLocator + 1) > pModuleEnd)
    {
        return false;
    }
    if (pLocator->signature != 0)
    {
        return false;
    }

    const MsvcTypeDescriptor* pTypeDescriptor = pLocator->GetTypeDescriptor();
    if (reinterpret_cast<const BYTE*>(pTypeDescriptor) < pModuleStart || reinterpret_cast<const BYTE*>(pTypeDescriptor + 1) > pModuleEnd)
    {
        return false;
    }

    const char* pRttiClassName = pTypeDescriptor->name;
    size_t requiredSize = 4 + className.size() + 3;
    if (reinterpret_cast<const BYTE*>(pRttiClassName) < pModuleStart ||
        reinterpret_cast<const BYTE*>(pRttiClassName + requiredSize) > pModuleEnd)
    {
        return false;
    }

    return memcmp(pRttiClassName, ".?A", 3) == 0 &&
        (pRttiClassName[3] == 'V' || pRttiClassName[3] == 'U') &&
        memcmp(pRttiClassName + 4, className.c_str(), className.size()) == 0 &&
        memcmp(pRttiClassName + 4 + className.size(), "@@\0", 3) == 0;
}
