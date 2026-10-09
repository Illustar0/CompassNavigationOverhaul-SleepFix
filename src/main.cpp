#include <SKSE/SKSE.h>

#include <array>
#include <filesystem>
#include <format>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <Windows.h>
#include <bcrypt.h>

namespace
{
    constexpr std::wstring_view targetName = L"CompassNavigationOverhaul.dll";
    constexpr std::string_view targetHash =
        "4449723b381fa111fa8f21bfde4e899dce53001308f0cc1920c55033bf0f51ba";
    constexpr std::uintptr_t sleepImmediateRva = 0x6458D;

    using FileHandle = std::unique_ptr<void, decltype(&CloseHandle)>;
    using ModuleHandle = std::unique_ptr<std::remove_pointer_t<HMODULE>, decltype(&FreeLibrary)>;

    HMODULE preloadedCno = nullptr;

    /** Report a failure to resolve the running game's executable path. */
    class GamePathResolutionError final : public std::runtime_error
    {
    public:
        /** Describe the path resolution operation that failed. */
        GamePathResolutionError() : std::runtime_error("Cannot determine the game executable path") {}
    };

    /** Locate CNO through the game's Data path so MO2 can resolve its virtual file. */
    std::filesystem::path GetTargetPath()
    {
        std::array<wchar_t, 32768> buffer{};
        const auto length = GetModuleFileNameW(nullptr, buffer.data(), buffer.size());
        if (length == 0 || length >= buffer.size()) {
            throw GamePathResolutionError();
        }
        return std::filesystem::path(std::wstring_view(buffer.data(), length)).parent_path() /
               L"Data" / L"SKSE" / L"Plugins" / targetName;
    }

    /** Accept only the SHA-256 of the original CNO binary identified in the startup trace. */
    bool MatchesTargetHash(HANDLE file)
    {
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
            static_cast<unsigned long long>(size.QuadPart) > (std::numeric_limits<ULONG>::max)()) {
            SKSE::log::error("Cannot read the target file size for SHA-256");
            return false;
        }

        std::vector<BYTE> data(static_cast<std::size_t>(size.QuadPart));
        DWORD bytesRead = 0;
        if (!ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &bytesRead, nullptr) ||
            bytesRead != data.size()) {
            SKSE::log::error("Cannot read the complete CNO file; SHA-256 check aborted");
            return false;
        }

        std::array<BYTE, 32> hash{};
        const auto status = BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
            data.data(), static_cast<ULONG>(data.size()), hash.data(), hash.size());
        if (status < 0) {
            SKSE::log::error("SHA-256 calculation failed: NTSTATUS {:#x}", static_cast<ULONG>(status));
            return false;
        }

        std::string digest;
        digest.reserve(hash.size() * 2);
        for (const auto byte : hash) {
            digest += std::format("{:02x}", byte);
        }
        if (digest != targetHash) {
            SKSE::log::warn("CNO SHA-256 mismatch; patch skipped. Actual: {}", digest);
            return false;
        }
        return true;
    }

    /** Change the low byte of the known binary's sleep immediate from six to zero. */
    bool WriteZeroDuration(HMODULE cnoLibrary)
    {
        auto* immediate = reinterpret_cast<BYTE*>(cnoLibrary) + sleepImmediateRva;
        DWORD oldProtection = 0;
        if (!VirtualProtect(immediate, 1, PAGE_EXECUTE_READWRITE, &oldProtection)) {
            SKSE::log::error("Cannot make the CNO sleep immediate writable: {}", GetLastError());
            return false;
        }

        *immediate = 0;
        const auto flushed = FlushInstructionCache(GetCurrentProcess(), immediate, 1);
        DWORD unusedProtection = 0;
        const auto restored = VirtualProtect(immediate, 1, oldProtection, &unusedProtection);
        if (!flushed || !restored) {
            SKSE::log::error("Cannot finalize the CNO patch: cache flushed={}, protection restored={}",
                flushed != FALSE, restored != FALSE);
            return false;
        }
        return true;
    }

    /** Preload and patch CNO before SKSE invokes its entry point, retaining the mapping until PostLoad. */
    void ApplySleepFix()
    {
        if (GetModuleHandleW(targetName.data())) {
            SKSE::log::warn("CNO is already mapped; patch skipped because its entry point may have run");
            return;
        }

        const auto path = GetTargetPath();
        // Keep the verified file open without write/delete sharing until loading and patching finish.
        const auto rawFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (rawFile == INVALID_HANDLE_VALUE) {
            SKSE::log::warn("Cannot open CNO; patch skipped. Win32 error: {}", GetLastError());
            return;
        }
        const FileHandle file(rawFile, &CloseHandle);
        if (!MatchesTargetHash(file.get())) {
            return;
        }

        ModuleHandle cnoLibrary(LoadLibraryW(path.c_str()), &FreeLibrary);
        if (!cnoLibrary) {
            SKSE::log::error("Cannot preload CNO: {}", GetLastError());
            return;
        }
        if (!WriteZeroDuration(cnoLibrary.get())) {
            return;
        }

        preloadedCno = cnoLibrary.release();
        SKSE::log::info("CNO SHA-256 matched; sleep immediate at RVA {:#x} changed from 6 to 0",
            sleepImmediateRva);
    }

    /** Release our preload reference after SKSE has acquired its own reference and initialized plugins. */
    // SKSE's EventCallback requires Message*, so the pointee cannot be made const in this signature.
    // ReSharper disable once CppParameterMayBeConstPtrOrRef
    void OnSkseMessage(SKSE::MessagingInterface::Message* const message)
    {
        if (message->type == SKSE::MessagingInterface::kPostLoad && preloadedCno) {
            if (!FreeLibrary(std::exchange(preloadedCno, nullptr))) {
                SKSE::log::warn("Cannot release the CNO preload reference: {}", GetLastError());
            }
        }
    }
}

/** Initialize the plugin and apply the CNO fix; unsupported targets remain untouched. */
SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* skse)
{
    SKSE::Init(skse);
    if (!SKSE::GetMessagingInterface()->RegisterListener("SKSE", OnSkseMessage)) {
        SKSE::log::error("Cannot register the preload cleanup listener; patch skipped");
        return false;
    }

    try {
        ApplySleepFix();
    } catch (const std::exception& error) {
        SKSE::log::error("CNO patch skipped: {}", error.what());
    }
    return true;
}
