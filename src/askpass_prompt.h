#pragma once

enum class AskpassPromptKind {
    Secret,
    HintedConfirmation,
    TextConfirmation
};

AskpassPromptKind classify_askpass_prompt(const char* environment_hint, const char* prompt);
