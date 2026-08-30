#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class SecretBuffer {
public:
    SecretBuffer() = default;
    SecretBuffer(const SecretBuffer&) = delete;
    SecretBuffer& operator=(const SecretBuffer&) = delete;
    SecretBuffer(SecretBuffer&& other) noexcept;
    SecretBuffer& operator=(SecretBuffer&& other) noexcept;
    ~SecretBuffer();

    static SecretBuffer from_utf8_literal(const char* value);
    void assign(const void* data, size_t size);
    void push_back(unsigned char value);
    void pop_back();
    bool empty() const;
    size_t size() const;
    const unsigned char* data() const;
    bool equals(const SecretBuffer& other) const;
    void wipe();

private:
    std::vector<unsigned char> value_;
};

class ICredentialBackend;

class CredentialStore {
public:
    CredentialStore();
    ~CredentialStore();
    CredentialStore(const CredentialStore&) = delete;
    CredentialStore& operator=(const CredentialStore&) = delete;

    const char* backend_name() const;
    bool available() const;
    bool exists(const std::string& id) const;
    bool write(const std::string& id, const std::string& username, const SecretBuffer& password,
               std::string& error_message) const;
    std::optional<SecretBuffer> read(const std::string& id, std::string& error_message) const;
    bool remove(const std::string& id, std::string& error_message) const;

private:
    std::unique_ptr<ICredentialBackend> backend_;
};
