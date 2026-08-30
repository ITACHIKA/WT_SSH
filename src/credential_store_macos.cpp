#include "credential_store_backend.h"

#include <Security/Security.h>

#include <cstring>

namespace {
CFStringRef make_string(const std::string& value) {
    return CFStringCreateWithBytes(kCFAllocatorDefault,
                                   reinterpret_cast<const UInt8*>(value.data()), value.size(),
                                   kCFStringEncodingUTF8, false);
}

std::string status_error(OSStatus status) {
    CFStringRef message = SecCopyErrorMessageString(status, nullptr);
    if (!message) return "OSStatus " + std::to_string(status);
    char buffer[1024]{};
    const bool ok = CFStringGetCString(message, buffer, sizeof(buffer), kCFStringEncodingUTF8);
    CFRelease(message);
    return ok ? std::string(buffer) : ("OSStatus " + std::to_string(status));
}

CFMutableDictionaryRef make_query(const std::string& id) {
    auto query = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                           &kCFTypeDictionaryKeyCallBacks,
                                           &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query, kSecAttrService, CFSTR("wtssh"));
    CFStringRef account = make_string(id);
    if (account) {
        CFDictionarySetValue(query, kSecAttrAccount, account);
        CFRelease(account);
    }
    return query;
}

class MacOSKeychainBackend final : public ICredentialBackend {
public:
    const char* name() const override { return "macos-keychain"; }
    bool available() const override { return true; }

    bool exists(const std::string& id) const override {
        auto query = make_query(id);
        CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
        CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);
        CFTypeRef result = nullptr;
        const OSStatus status = SecItemCopyMatching(query, &result);
        CFRelease(query);
        if (result) CFRelease(result);
        return status == errSecSuccess;
    }

    bool write(const std::string& id, const std::string& username, const SecretBuffer& password,
               std::string& error_message) const override {
        auto query = make_query(id);
        auto attributes = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                                     &kCFTypeDictionaryKeyCallBacks,
                                                     &kCFTypeDictionaryValueCallBacks);
        CFDataRef data = CFDataCreate(kCFAllocatorDefault, password.data(), password.size());
        CFDictionarySetValue(attributes, kSecValueData, data);
        CFStringRef label = make_string(username.empty() ? ("WT SSH Manager: " + id)
                                                         : ("WT SSH Manager: " + username));
        if (label) CFDictionarySetValue(attributes, kSecAttrLabel, label);

        OSStatus status = SecItemUpdate(query, attributes);
        if (status == errSecItemNotFound) {
            CFDictionarySetValue(query, kSecValueData, data);
            if (label) CFDictionarySetValue(query, kSecAttrLabel, label);
            status = SecItemAdd(query, nullptr);
        }
        if (label) CFRelease(label);
        CFRelease(data);
        CFRelease(attributes);
        CFRelease(query);
        if (status != errSecSuccess) {
            error_message = "Unable to save password in macOS Keychain: " + status_error(status);
            return false;
        }
        return true;
    }

    std::optional<SecretBuffer> read(const std::string& id, std::string& error_message) const override {
        auto query = make_query(id);
        CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
        CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);
        CFTypeRef result = nullptr;
        const OSStatus status = SecItemCopyMatching(query, &result);
        CFRelease(query);
        if (status != errSecSuccess) {
            error_message = "Unable to read password from macOS Keychain: " + status_error(status);
            return std::nullopt;
        }
        SecretBuffer password;
        auto data = static_cast<CFDataRef>(result);
        password.assign(CFDataGetBytePtr(data), static_cast<size_t>(CFDataGetLength(data)));
        CFRelease(result);
        return std::optional<SecretBuffer>(std::move(password));
    }

    bool remove(const std::string& id, std::string& error_message) const override {
        auto query = make_query(id);
        const OSStatus status = SecItemDelete(query);
        CFRelease(query);
        if (status == errSecSuccess || status == errSecItemNotFound) return true;
        error_message = "Unable to delete password from macOS Keychain: " + status_error(status);
        return false;
    }
};
}  // namespace

std::unique_ptr<ICredentialBackend> make_platform_credential_backend() {
    return std::make_unique<MacOSKeychainBackend>();
}
