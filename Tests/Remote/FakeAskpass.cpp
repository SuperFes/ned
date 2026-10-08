//
// Stands in for ned as SSH_ASKPASS in the SshMaster tests: the same relay
// ned's main runs, without the editor around it.
//

#include <cstdlib>

#include <unistd.h>

#include "Editor/Remote/Askpass.h"

int main(int argc, char** argv) {
    const char* socket = std::getenv(ned::editor::remote::kAskpassSocketVariable);
    const char* hint   = std::getenv("SSH_ASKPASS_PROMPT");
    if (socket == nullptr || argc != 2) {
        return 1;
    }
    return ned::editor::remote::RunAskpassRelay(socket, argv[1], hint != nullptr ? hint : "", STDOUT_FILENO);
}
