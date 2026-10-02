#include "askpass_prompt.h"

#include <iostream>

namespace {
bool expect(AskpassPromptKind actual, AskpassPromptKind expected, const char* message) {
    if (actual == expected) return true;
    std::cerr << message << '\n';
    return false;
}
}  // namespace

int main() {
    if (!expect(classify_askpass_prompt("confirm", "Allow operation?"),
                AskpassPromptKind::HintedConfirmation, "The OpenSSH confirmation hint was ignored.")) return 1;
    if (!expect(classify_askpass_prompt(nullptr,
                    "Are you sure you want to continue connecting (yes/no/[fingerprint])?"),
                AskpassPromptKind::TextConfirmation, "A first-connection host-key prompt was not detected.")) return 1;
    if (!expect(classify_askpass_prompt(nullptr, "Please type 'yes' or 'no': "),
                AskpassPromptKind::TextConfirmation, "A repeated host-key confirmation was not detected.")) return 1;
    if (!expect(classify_askpass_prompt(nullptr, "alice@example password: "),
                AskpassPromptKind::Secret, "A password prompt was misclassified as confirmation.")) return 1;
    if (!expect(classify_askpass_prompt(nullptr, "Enter passphrase for key 'id_ed25519': "),
                AskpassPromptKind::Secret, "A passphrase prompt was misclassified as confirmation.")) return 1;
    return 0;
}
