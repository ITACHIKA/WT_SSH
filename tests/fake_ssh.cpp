#include <cstdlib>
#include <fstream>

int main(int argc, char** argv) {
    const char* capture = std::getenv("WTSSH_TEST_CAPTURE");
    if (!capture || !*capture) return 2;
    std::ofstream out(capture, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return 3;
    for (int i = 1; i < argc; ++i) out << "ARG=" << argv[i] << '\n';
    const char* names[] = {"DISPLAY", "SSH_ASKPASS", "SSH_ASKPASS_REQUIRE", "WTSSH_ASKPASS_MODE",
                           "WTSCP_ASKPASS_MODE", "WTSSH_CREDENTIAL_ID"};
    for (const char* name : names) {
        const char* value = std::getenv(name);
        out << "ENV_" << name << '=' << (value ? value : "<unset>") << '\n';
    }
    return 0;
}
