#include "credential_store_backend.h"

namespace {
class UnavailableCredentialBackend final : public ICredentialBackend {
public:
    const char* name() const override { return "none"; }
    bool available() const override { return false; }
    bool exists(const std::string&) const override { return false; }
    bool write(const std::string&, const std::string&, const SecretBuffer&, std::string& error) const override {
        error = "No credential backend was built. Reconfigure CMake with a supported backend.";
        return false;
    }
    std::optional<SecretBuffer> read(const std::string&, std::string& error) const override {
        error = "No credential backend was built.";
        return std::nullopt;
    }
    bool remove(const std::string&, std::string&) const override { return true; }
};
}  // namespace

std::unique_ptr<ICredentialBackend> make_platform_credential_backend() {
    return std::make_unique<UnavailableCredentialBackend>();
}
