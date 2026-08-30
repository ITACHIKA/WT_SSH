#include "credential_store_backend.h"

#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {
void secure_wipe(void* data, size_t size) {
    if (!data || size == 0) return;
#ifdef _WIN32
    SecureZeroMemory(data, size);
#else
    volatile unsigned char* p = static_cast<volatile unsigned char*>(data);
    while (size--) *p++ = 0;
#endif
}
}  // namespace

SecretBuffer::SecretBuffer(SecretBuffer&& other) noexcept : value_(std::move(other.value_)) {
    other.wipe();
}

SecretBuffer& SecretBuffer::operator=(SecretBuffer&& other) noexcept {
    if (this != &other) {
        wipe();
        value_ = std::move(other.value_);
        other.wipe();
    }
    return *this;
}

SecretBuffer::~SecretBuffer() { wipe(); }

SecretBuffer SecretBuffer::from_utf8_literal(const char* value) {
    SecretBuffer result;
    if (value) result.assign(value, std::strlen(value));
    return result;
}

void SecretBuffer::assign(const void* data, size_t size) {
    wipe();
    if (!data || size == 0) return;
    const auto* bytes = static_cast<const unsigned char*>(data);
    value_.assign(bytes, bytes + size);
}

void SecretBuffer::push_back(unsigned char value) { value_.push_back(value); }
void SecretBuffer::pop_back() {
    if (!value_.empty()) {
        value_.back() = 0;
        value_.pop_back();
    }
}
bool SecretBuffer::empty() const { return value_.empty(); }
size_t SecretBuffer::size() const { return value_.size(); }
const unsigned char* SecretBuffer::data() const { return value_.data(); }
bool SecretBuffer::equals(const SecretBuffer& other) const { return value_ == other.value_; }

void SecretBuffer::wipe() {
    secure_wipe(value_.data(), value_.size());
    value_.clear();
}

CredentialStore::CredentialStore() : backend_(make_platform_credential_backend()) {}
CredentialStore::~CredentialStore() = default;
const char* CredentialStore::backend_name() const { return backend_->name(); }
bool CredentialStore::available() const { return backend_->available(); }
bool CredentialStore::exists(const std::string& id) const { return backend_->exists(id); }
bool CredentialStore::write(const std::string& id, const std::string& username, const SecretBuffer& password,
                            std::string& error_message) const {
    return backend_->write(id, username, password, error_message);
}
std::optional<SecretBuffer> CredentialStore::read(const std::string& id, std::string& error_message) const {
    return backend_->read(id, error_message);
}
bool CredentialStore::remove(const std::string& id, std::string& error_message) const {
    return backend_->remove(id, error_message);
}
