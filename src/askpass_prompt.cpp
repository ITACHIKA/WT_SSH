#include "askpass_prompt.h"

#include <algorithm>
#include <cctype>
#include <string>

AskpassPromptKind classify_askpass_prompt(const char* environment_hint, const char* prompt) {
    if (environment_hint && std::string(environment_hint) == "confirm")
        return AskpassPromptKind::HintedConfirmation;

    std::string normalized = prompt ? prompt : "";
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    const bool host_confirmation =
        normalized.find("are you sure you want to continue connecting") != std::string::npos ||
        normalized.find("authenticity of host") != std::string::npos ||
        normalized.find("please type 'yes' or 'no'") != std::string::npos ||
        normalized.find("please type \"yes\" or \"no\"") != std::string::npos ||
        normalized.find("yes/no") != std::string::npos;
    return host_confirmation ? AskpassPromptKind::TextConfirmation : AskpassPromptKind::Secret;
}
