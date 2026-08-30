#pragma once

#include "credential_store.h"

class ICredentialBackend {
public:
    virtual ~ICredentialBackend() = default;
    virtual const char* name() const = 0;
    virtual bool available() const = 0;
    virtual bool exists(const std::string& id) const = 0;
    virtual bool write(const std::string& id, const std::string& username, const SecretBuffer& password,
                       std::string& error_message) const = 0;
    virtual std::optional<SecretBuffer> read(const std::string& id, std::string& error_message) const = 0;
    virtual bool remove(const std::string& id, std::string& error_message) const = 0;
};

std::unique_ptr<ICredentialBackend> make_platform_credential_backend();
