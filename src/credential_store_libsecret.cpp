#include "credential_store_backend.h"

#include <libsecret/secret.h>

#include <cstring>

namespace {
const SecretSchema kSchema = {
    "org.wtssh.Password", SECRET_SCHEMA_NONE,
    {{"id", SECRET_SCHEMA_ATTRIBUTE_STRING}, {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING}}
};

std::string glib_error(GError* error) {
    if (!error) return "unknown libsecret error";
    std::string message = error->message ? error->message : "unknown libsecret error";
    g_error_free(error);
    return message;
}

void wipe_chars(char* data, size_t size) {
    volatile char* p = data;
    while (size--) *p++ = 0;
}

class LibSecretBackend final : public ICredentialBackend {
public:
    const char* name() const override { return "linux-libsecret"; }
    bool available() const override { return true; }

    bool exists(const std::string& id) const override {
        GError* error = nullptr;
        char* password = secret_password_lookup_sync(&kSchema, nullptr, &error, "id", id.c_str(), nullptr);
        if (error) { g_error_free(error); return false; }
        if (!password) return false;
        secret_password_free(password);
        return true;
    }

    bool write(const std::string& id, const std::string& username, const SecretBuffer& password,
               std::string& error_message) const override {
        std::vector<char> value(password.size() + 1, '\0');
        std::memcpy(value.data(), password.data(), password.size());
        const std::string label = username.empty() ? ("WT SSH Manager: " + id)
                                                   : ("WT SSH Manager: " + username);
        GError* error = nullptr;
        const gboolean ok = secret_password_store_sync(&kSchema, SECRET_COLLECTION_DEFAULT,
                                                        label.c_str(), value.data(), nullptr, &error,
                                                        "id", id.c_str(), nullptr);
        wipe_chars(value.data(), value.size());
        if (!ok) {
            error_message = "Unable to save password with libsecret: " + glib_error(error);
            return false;
        }
        return true;
    }

    std::optional<SecretBuffer> read(const std::string& id, std::string& error_message) const override {
        GError* error = nullptr;
        char* value = secret_password_lookup_sync(&kSchema, nullptr, &error, "id", id.c_str(), nullptr);
        if (!value) {
            error_message = "Unable to read password with libsecret: " +
                            (error ? glib_error(error) : "credential not found");
            return std::nullopt;
        }
        SecretBuffer password;
        password.assign(value, std::strlen(value));
        secret_password_free(value);
        return std::optional<SecretBuffer>(std::move(password));
    }

    bool remove(const std::string& id, std::string& error_message) const override {
        GError* error = nullptr;
        const gboolean ok = secret_password_clear_sync(&kSchema, nullptr, &error, "id", id.c_str(), nullptr);
        if (!ok) {
            error_message = "Unable to delete password with libsecret: " + glib_error(error);
            return false;
        }
        return true;
    }
};
}  // namespace

std::unique_ptr<ICredentialBackend> make_platform_credential_backend() {
    return std::make_unique<LibSecretBackend>();
}
