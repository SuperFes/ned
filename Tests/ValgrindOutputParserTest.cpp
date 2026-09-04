#include <catch2/catch_test_macros.hpp>

#include "Editor/ValgrindOutputParser.h"

using ned::editor::ParseValgrindXml;
using ned::editor::ValgrindFinding;

TEST_CASE("Ordinary non-Valgrind output yields no findings", "[ValgrindOutputParser]") {
    const std::string output = "All tests passed.\n[tests: 12 passed, 0 failed, 0 skipped]\n";
    REQUIRE(ParseValgrindXml(output).empty());
}

TEST_CASE("Empty input yields no findings", "[ValgrindOutputParser]") {
    REQUIRE(ParseValgrindXml("").empty());
}

TEST_CASE("A clean valgrind XML run with zero errors yields no findings", "[ValgrindOutputParser]") {
    const std::string output = "<?xml version=\"1.0\"?>\n"
                                "<valgrindoutput>\n"
                                "<protocolversion>4</protocolversion>\n"
                                "<protocoltool>memcheck</protocoltool>\n"
                                "<pid>1234</pid>\n"
                                "<tool>memcheck</tool>\n"
                                "<status>\n<state>RUNNING</state>\n</status>\n"
                                "<status>\n<state>FINISHED</state>\n</status>\n"
                                "<errorcounts>\n</errorcounts>\n"
                                "<suppcounts>\n</suppcounts>\n"
                                "</valgrindoutput>\n";
    REQUIRE(ParseValgrindXml(output).empty());
}

TEST_CASE("Memcheck invalid-read error with a single debuggable frame", "[ValgrindOutputParser]") {
    const std::string output = "<valgrindoutput>\n"
                                "<tool>memcheck</tool>\n"
                                "<error>\n"
                                "  <unique>0x0</unique>\n"
                                "  <tid>1</tid>\n"
                                "  <kind>InvalidRead</kind>\n"
                                "  <what>Invalid read of size 4</what>\n"
                                "  <stack>\n"
                                "    <frame>\n"
                                "      <ip>0x109180</ip>\n"
                                "      <obj>/home/user/a.out</obj>\n"
                                "      <fn>main</fn>\n"
                                "      <dir>/home/user/project</dir>\n"
                                "      <file>main.cpp</file>\n"
                                "      <line>10</line>\n"
                                "    </frame>\n"
                                "  </stack>\n"
                                "  <auxwhat>Address 0x4d6b080 is 0 bytes after a block of size 40 alloc'd</auxwhat>\n"
                                "  <stack>\n"
                                "    <frame>\n"
                                "      <ip>0x4849f2c</ip>\n"
                                "      <obj>/usr/lib/valgrind/vgpreload_memcheck-amd64-linux.so</obj>\n"
                                "      <fn>operator new(unsigned long)</fn>\n"
                                "    </frame>\n"
                                "  </stack>\n"
                                "</error>\n"
                                "<errorcounts>\n  <pair>\n    <count>1</count>\n    <unique>0x0</unique>\n  </pair>\n</errorcounts>\n"
                                "</valgrindoutput>\n";

    const std::vector<ValgrindFinding> findings = ParseValgrindXml(output);
    REQUIRE(findings.size() == 1);
    CHECK(findings[0].tool == "memcheck");
    CHECK(findings[0].kind == "InvalidRead");
    CHECK(findings[0].message == "Invalid read of size 4");
    CHECK(findings[0].file == "/home/user/project/main.cpp");
    CHECK(findings[0].line == 10);
}

TEST_CASE("A leading frame with no debug info is skipped in favor of the next one", "[ValgrindOutputParser]") {
    const std::string output = "<valgrindoutput>\n"
                                "<tool>memcheck</tool>\n"
                                "<error>\n"
                                "  <kind>Leak_DefinitelyLost</kind>\n"
                                "  <xwhat>\n"
                                "    <text>40 bytes in 1 blocks are definitely lost in loss record 1 of 1</text>\n"
                                "    <leakedbytes>40</leakedbytes>\n"
                                "    <leakedblocks>1</leakedblocks>\n"
                                "  </xwhat>\n"
                                "  <stack>\n"
                                "    <frame>\n"
                                "      <ip>0x4849f2c</ip>\n"
                                "      <obj>/usr/lib/valgrind/vgpreload_memcheck-amd64-linux.so</obj>\n"
                                "      <fn>operator new(unsigned long)</fn>\n"
                                "    </frame>\n"
                                "    <frame>\n"
                                "      <ip>0x109150</ip>\n"
                                "      <obj>/home/user/a.out</obj>\n"
                                "      <fn>main</fn>\n"
                                "      <dir>/home/user/project</dir>\n"
                                "      <file>main.cpp</file>\n"
                                "      <line>8</line>\n"
                                "    </frame>\n"
                                "  </stack>\n"
                                "</error>\n"
                                "</valgrindoutput>\n";

    const std::vector<ValgrindFinding> findings = ParseValgrindXml(output);
    REQUIRE(findings.size() == 1);
    CHECK(findings[0].kind == "Leak_DefinitelyLost");
    CHECK(findings[0].message == "40 bytes in 1 blocks are definitely lost in loss record 1 of 1");
    CHECK(findings[0].file == "/home/user/project/main.cpp");
    CHECK(findings[0].line == 8);
}

TEST_CASE("Helgrind data race error", "[ValgrindOutputParser]") {
    const std::string output = "<valgrindoutput>\n"
                                "<tool>helgrind</tool>\n"
                                "<error>\n"
                                "  <kind>Race</kind>\n"
                                "  <xwhat>\n"
                                "    <text>Possible data race during write of size 4 at 0x5C4C0E8 by thread #2</text>\n"
                                "    <hthreadid>2</hthreadid>\n"
                                "  </xwhat>\n"
                                "  <stack>\n"
                                "    <frame>\n"
                                "      <ip>0x109180</ip>\n"
                                "      <obj>/home/user/a.out</obj>\n"
                                "      <fn>writer</fn>\n"
                                "      <dir>/home/user/project</dir>\n"
                                "      <file>race.cpp</file>\n"
                                "      <line>15</line>\n"
                                "    </frame>\n"
                                "  </stack>\n"
                                "  <auxwhat>This conflicts with a previous write of size 4 by thread #1</auxwhat>\n"
                                "  <stack>\n"
                                "    <frame>\n"
                                "      <ip>0x109200</ip>\n"
                                "      <obj>/home/user/a.out</obj>\n"
                                "      <fn>reader</fn>\n"
                                "      <dir>/home/user/project</dir>\n"
                                "      <file>race.cpp</file>\n"
                                "      <line>25</line>\n"
                                "    </frame>\n"
                                "  </stack>\n"
                                "</error>\n"
                                "</valgrindoutput>\n";

    const std::vector<ValgrindFinding> findings = ParseValgrindXml(output);
    REQUIRE(findings.size() == 1);
    CHECK(findings[0].tool == "helgrind");
    CHECK(findings[0].kind == "Race");
    CHECK(findings[0].message == "Possible data race during write of size 4 at 0x5C4C0E8 by thread #2");
    // Only the FIRST <stack> block's own frame is used -- the "previous
    // write" second stack's line (25) must not win.
    CHECK(findings[0].file == "/home/user/project/race.cpp");
    CHECK(findings[0].line == 15);
}

TEST_CASE("A frame with no file at all in any stack leaves the location empty", "[ValgrindOutputParser]") {
    const std::string output = "<valgrindoutput>\n"
                                "<tool>memcheck</tool>\n"
                                "<error>\n"
                                "  <kind>InvalidFree</kind>\n"
                                "  <what>Invalid free() / delete / delete[]</what>\n"
                                "  <stack>\n"
                                "    <frame>\n"
                                "      <ip>0x4849f2c</ip>\n"
                                "      <obj>/usr/lib/valgrind/vgpreload_memcheck-amd64-linux.so</obj>\n"
                                "      <fn>free</fn>\n"
                                "    </frame>\n"
                                "  </stack>\n"
                                "</error>\n"
                                "</valgrindoutput>\n";

    const std::vector<ValgrindFinding> findings = ParseValgrindXml(output);
    REQUIRE(findings.size() == 1);
    CHECK(findings[0].kind == "InvalidFree");
    CHECK(findings[0].file.empty());
    CHECK(findings[0].line == 0);
}

TEST_CASE("Multiple independent error blocks in one document each produce a finding", "[ValgrindOutputParser]") {
    const std::string output = "<valgrindoutput>\n"
                                "<tool>memcheck</tool>\n"
                                "<error>\n"
                                "  <kind>InvalidRead</kind>\n"
                                "  <what>Invalid read of size 4</what>\n"
                                "  <stack>\n"
                                "    <frame>\n<dir>/a</dir>\n<file>a.cpp</file>\n<line>1</line>\n</frame>\n"
                                "  </stack>\n"
                                "</error>\n"
                                "<error>\n"
                                "  <kind>InvalidWrite</kind>\n"
                                "  <what>Invalid write of size 8</what>\n"
                                "  <stack>\n"
                                "    <frame>\n<dir>/b</dir>\n<file>b.cpp</file>\n<line>2</line>\n</frame>\n"
                                "  </stack>\n"
                                "</error>\n"
                                "<errorcounts>\n</errorcounts>\n"
                                "</valgrindoutput>\n";

    const std::vector<ValgrindFinding> findings = ParseValgrindXml(output);
    REQUIRE(findings.size() == 2);
    CHECK(findings[0].kind == "InvalidRead");
    CHECK(findings[0].file == "/a/a.cpp");
    CHECK(findings[1].kind == "InvalidWrite");
    CHECK(findings[1].file == "/b/b.cpp");
}

TEST_CASE("A file without a dir tag is used bare", "[ValgrindOutputParser]") {
    const std::string output = "<valgrindoutput>\n"
                                "<tool>memcheck</tool>\n"
                                "<error>\n"
                                "  <kind>UninitCondition</kind>\n"
                                "  <what>Conditional jump or move depends on uninitialised value(s)</what>\n"
                                "  <stack>\n"
                                "    <frame>\n<file>generated.c</file>\n<line>5</line>\n</frame>\n"
                                "  </stack>\n"
                                "</error>\n"
                                "</valgrindoutput>\n";

    const std::vector<ValgrindFinding> findings = ParseValgrindXml(output);
    REQUIRE(findings.size() == 1);
    CHECK(findings[0].file == "generated.c");
    CHECK(findings[0].line == 5);
}
