#include <catch2/catch_test_macros.hpp>

#include "Editor/SanitizerOutputParser.h"

using ned::editor::ParseSanitizerOutput;
using ned::editor::SanitizerFinding;

TEST_CASE("Ordinary non-sanitizer output yields no findings", "[SanitizerOutputParser]") {
    const std::string output = "All tests passed.\n[tests: 12 passed, 0 failed, 0 skipped]\n";
    REQUIRE(ParseSanitizerOutput(output).empty());
}

TEST_CASE("Empty input yields no findings", "[SanitizerOutputParser]") {
    REQUIRE(ParseSanitizerOutput("").empty());
}

TEST_CASE("AddressSanitizer heap-buffer-overflow report", "[SanitizerOutputParser]") {
    const std::string output =
        "==12345==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x602000000010 at pc 0x0000004a5b2c bp "
        "0x7ffd744f8c20 sp 0x7ffd744f8c18\n"
        "READ of size 4 at 0x602000000010 thread T0\n"
        "    #0 0x4a5b2b in main /home/user/ned/Source/Text/Rope.cpp:120:10\n"
        "    #1 0x7f8a12 in __libc_start_main (/lib/libc.so.6+0x21b97)\n"
        "\n"
        "SUMMARY: AddressSanitizer: heap-buffer-overflow /home/user/ned/Source/Text/Rope.cpp:120:10 in main\n"
        "==12345==ABORTING\n";

    const std::vector<SanitizerFinding> findings = ParseSanitizerOutput(output);
    REQUIRE(findings.size() == 1);
    CHECK(findings[0].tool == "AddressSanitizer");
    CHECK(findings[0].message == "heap-buffer-overflow");
    CHECK(findings[0].symbol == "main");
    CHECK(findings[0].file == "/home/user/ned/Source/Text/Rope.cpp");
    CHECK(findings[0].line == 120);
    CHECK(findings[0].column == 10);
}

TEST_CASE("UndefinedBehaviorSanitizer report prefers the paired runtime-error message", "[SanitizerOutputParser]") {
    const std::string output =
        "/home/user/ned/Source/Editor/Command.cpp:88:23: runtime error: signed integer overflow: 2147483647 + 1 cannot be "
        "represented in type 'int'\n"
        "SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior /home/user/ned/Source/Editor/Command.cpp:88:23 in "
        "ned::editor::Foo()\n";

    const std::vector<SanitizerFinding> findings = ParseSanitizerOutput(output);
    REQUIRE(findings.size() == 1);
    CHECK(findings[0].tool == "UndefinedBehaviorSanitizer");
    CHECK(findings[0].message == "signed integer overflow: 2147483647 + 1 cannot be represented in type 'int'");
    CHECK(findings[0].symbol == "ned::editor::Foo()");
    CHECK(findings[0].file == "/home/user/ned/Source/Editor/Command.cpp");
    CHECK(findings[0].line == 88);
    CHECK(findings[0].column == 23);
}

TEST_CASE("A runtime-error line at a different location than the SUMMARY is not used", "[SanitizerOutputParser]") {
    // Two independent UBSan findings back to back -- each SUMMARY must only
    // borrow the message from ITS OWN immediately preceding line, not the
    // other report's.
    const std::string output =
        "/a/b.cpp:10:1: runtime error: first message\n"
        "SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior /a/b.cpp:10:1 in f\n"
        "/a/b.cpp:20:2: runtime error: second message\n"
        "SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior /a/b.cpp:20:2 in g\n";

    const std::vector<SanitizerFinding> findings = ParseSanitizerOutput(output);
    REQUIRE(findings.size() == 2);
    CHECK(findings[0].message == "first message");
    CHECK(findings[0].line == 10);
    CHECK(findings[1].message == "second message");
    CHECK(findings[1].line == 20);
}

TEST_CASE("ThreadSanitizer data race report", "[SanitizerOutputParser]") {
    const std::string output =
        "==================\n"
        "WARNING: ThreadSanitizer: data race (pid=12345)\n"
        "  Write of size 4 at 0x7b0400000000 by thread T1:\n"
        "    #0 foo() /a/file.cpp:20:5\n"
        "\n"
        "  Previous read of size 4 at 0x7b0400000000 by thread T2:\n"
        "    #0 bar() /a/file.cpp:30:5\n"
        "\n"
        "SUMMARY: ThreadSanitizer: data race /a/file.cpp:20:5 in foo()\n"
        "==================\n";

    const std::vector<SanitizerFinding> findings = ParseSanitizerOutput(output);
    REQUIRE(findings.size() == 1);
    CHECK(findings[0].tool == "ThreadSanitizer");
    CHECK(findings[0].message == "data race");
    CHECK(findings[0].file == "/a/file.cpp");
    CHECK(findings[0].line == 20);
}

TEST_CASE("LeakSanitizer byte-count summary has no location but is still a finding", "[SanitizerOutputParser]") {
    const std::string output =
        "=================================================================\n"
        "==12345==ERROR: LeakSanitizer: detected memory leaks\n"
        "\n"
        "Direct leak of 24 byte(s) in 1 object(s) allocated from:\n"
        "    #0 0x4b1234 in operator new(unsigned long)\n"
        "    #1 0x4a5abc in main /a/file.cpp:40:15\n"
        "\n"
        "SUMMARY: AddressSanitizer: 24 byte(s) leaked in 1 allocation(s).\n";

    const std::vector<SanitizerFinding> findings = ParseSanitizerOutput(output);
    REQUIRE(findings.size() == 1);
    CHECK(findings[0].tool == "AddressSanitizer");
    CHECK(findings[0].message == "24 byte(s) leaked in 1 allocation(s).");
    CHECK(findings[0].file.empty());
    CHECK(findings[0].line == 0);
    CHECK(findings[0].symbol.empty());
}

TEST_CASE("Multiple independent reports in one blob each produce a finding", "[SanitizerOutputParser]") {
    const std::string output = "SUMMARY: AddressSanitizer: heap-use-after-free /a.cpp:1:1 in f\n"
                                "some unrelated output in between\n"
                                "SUMMARY: AddressSanitizer: stack-buffer-overflow /b.cpp:2:2 in g\n";

    const std::vector<SanitizerFinding> findings = ParseSanitizerOutput(output);
    REQUIRE(findings.size() == 2);
    CHECK(findings[0].file == "/a.cpp");
    CHECK(findings[1].file == "/b.cpp");
}

TEST_CASE("A file:line location without a column is parsed", "[SanitizerOutputParser]") {
    const std::string output = "SUMMARY: AddressSanitizer: heap-buffer-overflow /a/file.cpp:42 in main\n";
    const std::vector<SanitizerFinding> findings = ParseSanitizerOutput(output);
    REQUIRE(findings.size() == 1);
    CHECK(findings[0].file == "/a/file.cpp");
    CHECK(findings[0].line == 42);
    CHECK(findings[0].column == 0);
}
