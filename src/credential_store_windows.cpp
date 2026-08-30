#include "credential_store_backend.h"

#include <windows.h>
#include <wincred.h>

#include <array>
#include <cstring>

namespace {
constexpr std::array<unsigned char, 7> kUtf8Prefix{'W', 'T', 'S', 'S', 'H', '1', 0};

std::wstring utf8_to_wide(const std::string& input) {
    if (input.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(),
                                         static_cast<int>(input.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring output(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()),
                        output.data(), size);
    return output;
}

std::string wide_to_utf8(const wchar_t* input, size_t length) {
    if (!input || length == 0) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, input, static_cast<int>(length), nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 0) return {};
    std::string output(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, input, static_cast<int>(length), output.data(), size, nullptr, nullptr);
    return output;
}

std::string windows_error(DWORD code = GetLastError()) {
    wchar_t* text = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                            FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr, code, 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    std::string result = length ? wide_to_utf8(text, length) : ("Windows error " + std::to_string(code));
    if (text) LocalFree(text);
    while (!result.empty() && (result.back() == '\r' || result.back() == '\n' || result.back() == ' '))
        result.pop_back();
    return result;
}

std::wstring target_for(const std::string& id) { return utf8_to_wide("wtssh/" + id); }

void wipe_and_free(PCREDENTIALW credential) {
    if (!credential) return;
    if (credential->CredentialBlob && credential->CredentialBlobSize)
        SecureZeroMemory(credential->CredentialBlob, credential->CredentialBlobSize);
    CredFree(credential);
}

class WindowsCredentialBackend final : public ICredentialBackend {
public:
    const char* name() const override { return "windows-credential-manager"; }
    bool available() const override { return true; }

    bool exists(const std::string& id) const override {
        PCREDENTIALW credential = nullptr;
        const auto target = target_for(id);
        if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential)) return false;
        wipe_and_free(credential);
        return true;
    }

    bool write(const std::string& id, const std::string& username, const SecretBuffer& password,
               std::string& error_message) const override {
        auto target = target_for(id);
        auto user = utf8_to_wide(username);
        std::vector<unsigned char> blob;
        blob.reserve(kUtf8Prefix.size() + password.size());
        blob.insert(blob.end(), kUtf8Prefix.begin(), kUtf8Prefix.end());
        if (!password.empty()) blob.insert(blob.end(), password.data(), password.data() + password.size());
        if (blob.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
            SecureZeroMemory(blob.data(), blob.size());
            error_message = "Password is too large for Windows Credential Manager.";
            return false;
        }
        CREDENTIALW credential{};
        credential.Type = CRED_TYPE_GENERIC;
        credential.TargetName = target.data();
        credential.CredentialBlobSize = static_cast<DWORD>(blob.size());
        credential.CredentialBlob = blob.data();
        credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
        credential.UserName = user.empty() ? nullptr : user.data();
        const bool ok = CredWriteW(&credential, 0) != FALSE;
        SecureZeroMemory(blob.data(), blob.size());
        if (!ok) error_message = "Unable to save password: " + windows_error();
        return ok;
    }

    std::optional<SecretBuffer> read(const std::string& id, std::string& error_message) const override {
        PCREDENTIALW credential = nullptr;
        const auto target = target_for(id);
        if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
            error_message = "Unable to read saved password: " + windows_error();
            return std::nullopt;
        }
        SecretBuffer password;
        const auto* blob = credential->CredentialBlob;
        const size_t size = credential->CredentialBlobSize;
        if (size >= kUtf8Prefix.size() &&
            std::memcmp(blob, kUtf8Prefix.data(), kUtf8Prefix.size()) == 0) {
            password.assign(blob + kUtf8Prefix.size(), size - kUtf8Prefix.size());
        } else if (size % sizeof(wchar_t) == 0) {
            auto utf8 = wide_to_utf8(reinterpret_cast<const wchar_t*>(blob), size / sizeof(wchar_t));
            password.assign(utf8.data(), utf8.size());
            if (!utf8.empty()) SecureZeroMemory(utf8.data(), utf8.size());
        } else {
            wipe_and_free(credential);
            error_message = "Saved password has an unknown format.";
            return std::nullopt;
        }
        wipe_and_free(credential);
        return std::optional<SecretBuffer>(std::move(password));
    }

    bool remove(const std::string& id, std::string& error_message) const override {
        const auto target = target_for(id);
        if (CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0)) return true;
        const DWORD code = GetLastError();
        if (code == ERROR_NOT_FOUND) return true;
        error_message = "Unable to delete saved password: " + windows_error(code);
        return false;
    }
};
}  // namespace

std::unique_ptr<ICredentialBackend> make_platform_credential_backend() {
    return std::make_unique<WindowsCredentialBackend>();
}
