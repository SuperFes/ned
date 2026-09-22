#!/usr/bin/env python3
"""Ask real language servers which LSP capabilities they actually advertise.

    Tools/lsp-capability-probe.py [--root DIR] [--require CAP] [SERVER ...]

Answers the recurring "is this request worth implementing?" question that
ROADMAP's own unimplemented-request inventory keeps raising, with a
measurement instead of a guess. Twice now the answer changed the plan: a
2026-09-21 pass argued `textDocument/selectionRange` was worth building
natively rather than over LSP, and a 2026-09-22 pass deferred
`textDocument/inlineValue` outright after no installed server advertised
`inlineValueProvider`.

The client capabilities sent in `initialize` are deliberately generous --
every optional feature ned might ever ask about is announced as supported,
because a server is entitled to withhold a capability the client never
claimed. So a `false`/missing provider here means the server does not
implement it, not that it was never offered the chance.

SERVER names come from the table below; with none given, every one of them
that is actually installed is probed. --require exits non-zero unless every
probed server advertises that capability, so this can gate a build step or
answer a yes/no question from a script.

What this CANNOT tell you: what a server actually returns. A capability is
a promise to answer, not a description of the answer -- for a request whose
result shape drives the design (inlineValue's three variants, say), a
capability probe settles only whether to keep looking.
"""

import argparse
import json
import os
import select
import shutil
import subprocess
import sys
import time

# The servers worth asking, by the argv that starts each one on stdio.
# Extend freely: this is a convenience list, not a registry -- ned's own
# server configuration lives in each language's language.janet.
SERVERS = {
    "clangd": ["clangd"],
    "gopls": ["gopls"],
    "pylsp": ["pylsp"],
    "pyright": ["pyright-langserver", "--stdio"],
    "typescript-language-server": ["typescript-language-server", "--stdio"],
    "rust-analyzer": ["rust-analyzer"],
    "lua-language-server": ["lua-language-server"],
    "jdtls": ["jdtls"],
    "kotlin-language-server": ["kotlin-language-server"],
    "omnisharp": ["omnisharp", "-lsp"],
    "harper-ls": ["harper-ls", "--stdio"],
    "texlab": ["texlab"],
    "zls": ["zls"],
}

# Announced so nothing is withheld for not having been asked for. Only the
# optional, provider-gated corners need to appear here; a server advertises
# the core regardless.
CLIENT_CAPABILITIES = {
    "general": {"positionEncodings": ["utf-16", "utf-8"]},
    "textDocument": {
        "callHierarchy": {"dynamicRegistration": True},
        "codeAction": {"dynamicRegistration": True, "resolveSupport": {"properties": ["edit"]}},
        "codeLens": {"dynamicRegistration": True},
        "colorProvider": {"dynamicRegistration": True},
        "completion": {"completionItem": {"resolveSupport": {"properties": ["documentation", "additionalTextEdits"]}}},
        "declaration": {"dynamicRegistration": True},
        "diagnostic": {"dynamicRegistration": True},
        "documentLink": {"dynamicRegistration": True},
        "foldingRange": {"dynamicRegistration": True},
        "implementation": {"dynamicRegistration": True},
        "inlayHint": {"dynamicRegistration": True, "resolveSupport": {"properties": ["tooltip"]}},
        "inlineValue": {"dynamicRegistration": True},
        "linkedEditingRange": {"dynamicRegistration": True},
        "moniker": {"dynamicRegistration": True},
        "selectionRange": {"dynamicRegistration": True},
        "semanticTokens": {
            "dynamicRegistration": True,
            "requests": {"full": {"delta": True}, "range": True},
            "formats": ["relative"],
            "tokenTypes": [],
            "tokenModifiers": [],
        },
        "typeDefinition": {"dynamicRegistration": True},
    },
    "workspace": {
        "applyEdit": True,
        "codeLens": {"refreshSupport": True},
        "diagnostics": {"refreshSupport": True},
        "fileOperations": {
            "didCreate": True, "willCreate": True,
            "didRename": True, "willRename": True,
            "didDelete": True, "willDelete": True,
        },
        "inlayHint": {"refreshSupport": True},
        "inlineValue": {"refreshSupport": True},
        "semanticTokens": {"refreshSupport": True},
        "symbol": {"resolveSupport": {"properties": ["location.range"]}},
        "workspaceFolders": True,
    },
    "window": {"showDocument": {"support": True}, "showMessage": {}, "workDoneProgress": True},
}


def probe(argv, root, timeout):
    """The server's advertised capabilities, or (None, why-not)."""
    if shutil.which(argv[0]) is None:
        return None, "not installed"
    # stderr is captured rather than discarded because a server that
    # refuses to start says so there, and discarding it turns a one-line
    # answer into an indistinguishable timeout. Measured cost of not doing
    # this: `rust-analyzer` on a rustup-proxy install reported "no
    # initialize result in 120s" when it had in fact printed "Unknown
    # binary 'rust-analyzer' in official toolchain" and exited at once.
    try:
        process = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE)
    except OSError as error:
        return None, f"could not start: {error}"

    def died():
        """Why the process is gone, from its own stderr where it said so."""
        if process.poll() is None:
            return None
        try:
            complaint = process.stderr.read().decode(errors="replace").strip()
        except OSError:
            complaint = ""
        first = complaint.splitlines()[0] if complaint else ""
        return f"exited {process.returncode}" + (f": {first}" if first else " without a word")

    request = {
        "jsonrpc": "2.0", "id": 1, "method": "initialize",
        "params": {
            "processId": os.getpid(),
            "rootUri": "file://" + root,
            "capabilities": CLIENT_CAPABILITIES,
            "workspaceFolders": [{"uri": "file://" + root, "name": os.path.basename(root) or "probe"}],
        },
    }
    body = json.dumps(request).encode()
    try:
        process.stdin.write(b"Content-Length: %d\r\n\r\n" % len(body) + body)
        process.stdin.flush()
    except OSError as error:
        return None, died() or f"died before initialize: {error}"

    # Framed exactly as Editor/Lsp/Transport.cpp reads it, minus the
    # robustness -- a probe that mis-frames a reply would report a
    # capability gap that is really its own bug.
    deadline = time.time() + timeout
    buffered = b""
    while time.time() < deadline:
        ready, _, _ = select.select([process.stdout], [], [], max(0.0, deadline - time.time()))
        if not ready:
            break
        chunk = process.stdout.read1(65536)
        if not chunk:
            # EOF on stdout: the server is gone, and the useful half of
            # why is on the stderr just captured.
            process.wait(timeout=1)
            return None, died() or "closed stdout without answering"
        buffered += chunk
        while b"\r\n\r\n" in buffered:
            head, rest = buffered.split(b"\r\n\r\n", 1)
            lengths = [line for line in head.split(b"\r\n") if line.lower().startswith(b"content-length")]
            if not lengths:
                process.kill()
                return None, "reply had no Content-Length"
            length = int(lengths[0].split(b":")[1])
            if len(rest) < length:
                break
            message = json.loads(rest[:length])
            buffered = rest[length:]
            if message.get("id") == 1 and "result" in message:
                process.kill()
                return message["result"].get("capabilities", {}), None
            if message.get("id") == 1 and "error" in message:
                process.kill()
                return None, f"initialize failed: {message['error'].get('message', '?')}"
    process.kill()
    return None, f"no initialize result in {timeout:g}s"


def advertised(capabilities, name):
    """Whether `name` is really offered -- `false` and `{}` differ."""
    value = capabilities.get(name)
    if value is None or value is False:
        return False
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("servers", nargs="*", metavar="SERVER", help="names from the built-in table; default: all installed")
    parser.add_argument("--root", default=os.getcwd(), help="project root to initialize against (default: cwd)")
    parser.add_argument("--require", metavar="CAP", help="exit non-zero unless every probed server advertises CAP")
    parser.add_argument("--timeout", type=float, default=20.0, help="seconds to wait for initialize (default: 20)")
    parser.add_argument("--list", action="store_true", help="list the known server names and exit")
    arguments = parser.parse_args()

    if arguments.list:
        for name in sorted(SERVERS):
            print(name)
        return 0

    unknown = [name for name in arguments.servers if name not in SERVERS]
    if unknown:
        parser.error(f"unknown server(s): {', '.join(unknown)} (see --list)")

    wanted = arguments.servers or sorted(SERVERS)
    root = os.path.abspath(arguments.root)
    width = max(len(name) for name in wanted)
    missing = []
    probed = 0

    for name in wanted:
        capabilities, why = probe(SERVERS[name], root, arguments.timeout)
        if capabilities is None:
            # An uninstalled server is not a finding; anything else is.
            if why != "not installed" or arguments.servers:
                print(f"{name:{width}}  -- {why}")
            continue
        probed += 1
        if arguments.require:
            has = advertised(capabilities, arguments.require)
            print(f"{name:{width}}  {arguments.require} = {json.dumps(capabilities.get(arguments.require))}")
            if not has:
                missing.append(name)
        else:
            providers = sorted(key for key in capabilities if key.endswith("Provider") and advertised(capabilities, key))
            print(f"{name:{width}}  {', '.join(providers) if providers else '(no providers advertised)'}")

    sys.stdout.flush()
    if not probed:
        print(f"nothing probed -- none of the {len(wanted)} named server(s) answered", file=sys.stderr)
        return 2
    if arguments.require and missing:
        print(f"\n{arguments.require}: not advertised by {', '.join(missing)}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
