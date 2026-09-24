#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

#include "Editor/FileMagic.h"
#include "Editor/ModeOverrides.h"
#include "Editor/Modeline.h"
#include "Text/Buffer.h"

namespace {

namespace fs = std::filesystem;

fs::path WriteSample(const std::string& name, const std::string& text) {
    const fs::path dir = fs::temp_directory_path() / ("ned-mode-detection-" + std::to_string(::getpid()));
    fs::create_directories(dir);
    const fs::path path = dir / name;
    std::ofstream(path, std::ios::binary) << text;
    return path;
}

} // namespace

TEST_CASE("libmagic names C, C++ and Objective-C sources", "[ModeDetection]") {
    CHECK(ned::editor::MimeTypeOf("#import <Foundation/Foundation.h>\n@interface Foo : NSObject\n@end\n") ==
          "text/x-objective-c");
    CHECK(ned::editor::MimeTypeOf("namespace x {\nclass A { public: int f(); };\n}\n") == "text/x-c++");
}

TEST_CASE("A shared .h resolves to C, C++ or Objective-C by content", "[ModeDetection]") {
    using ned::editor::ModeForPath;
    CHECK(ModeForPath(WriteSample("plain.h", "#include <stdio.h>\nint add(int a, int b);\n")).name == "c-mode");
    CHECK(ModeForPath(WriteSample("cls.h", "namespace x {\nclass A { public: int f(); };\n}\n")).name == "cpp-mode");
    CHECK(ModeForPath(WriteSample("iface.h", "#import <Foundation/Foundation.h>\n@interface Foo : NSObject\n- (void)bar;\n@end\n"))
              .name == "objc-mode");
    // Nothing to read yet: the extension's owner.
    CHECK(ModeForPath(fs::temp_directory_path() / "ned-mode-detection-none" / "new.h").name == "c-mode");
}

TEST_CASE("A .v file is Verilog unless it reads as V", "[ModeDetection]") {
    using ned::editor::ModeForPath;
    CHECK(ModeForPath(WriteSample("counter.v", "module counter(input clk, output reg [3:0] q);\n"
                                               "  always @(posedge clk) q <= q + 1;\nendmodule\n"))
              .name == "verilog-mode");
    CHECK(ModeForPath(WriteSample("main.v", "module main\n\nfn main() {\n\tprintln('hi')\n}\n")).name == "v-mode");
    CHECK(ModeForPath(WriteSample("point.v", "struct Point {\n\tx int\n}\n")).name == "v-mode");
}

TEST_CASE("Vim modelines name a language and indentation", "[ModeDetection]") {
    using ned::editor::ParseModeline;
    const auto plain = ParseModeline("// vim: ft=verilog ts=8 sw=4 et\nmodule m;\n");
    CHECK(plain.language == "verilog");
    CHECK(plain.width == 4); // shiftwidth over tabstop
    CHECK(plain.useTabs == false);

    const auto setForm = ParseModeline("int x;\n/* vim: set filetype=c++ noexpandtab tabstop=2: */\n");
    CHECK(setForm.language == "cpp");
    CHECK(setForm.useTabs == true);
    CHECK(setForm.width == 2);

    CHECK_FALSE(ParseModeline("// For example: see ex: foo\n").language.has_value());
    CHECK_FALSE(ParseModeline("url = \"http://vim:8080\"\n").language.has_value()); // `vim:` must follow whitespace
}

TEST_CASE("Emacs mode lines name a language and indentation", "[ModeDetection]") {
    using ned::editor::ParseModeline;
    const auto variables = ParseModeline("// -*- mode: c++; tab-width: 8; c-basic-offset: 2; indent-tabs-mode: nil -*-\n");
    CHECK(variables.language == "cpp");
    CHECK(variables.width == 2);
    CHECK(variables.useTabs == false);

    CHECK(ParseModeline("#!/bin/sh\n# -*- shell-script -*-\necho\n").language == "bash"); // second line after #!
    CHECK_FALSE(ParseModeline("x\n# -*- python -*-\n").language.has_value());             // only line 1 otherwise
}

TEST_CASE("Only the first and last five lines are read for a modeline", "[ModeDetection]") {
    std::string middle;
    for (int i = 0; i < 20; ++i) {
        middle += i == 10 ? "# vim: ft=python\n" : "x\n";
    }
    CHECK_FALSE(ned::editor::ParseModeline(middle).language.has_value());
    CHECK(ned::editor::ParseModeline(middle + "# vim: ft=ruby\n").language == "ruby");
}

TEST_CASE("A modeline outranks the file's extension", "[ModeDetection]") {
    CHECK(ned::editor::ModeForPath(WriteSample("notes.txt", "# vim: ft=python\nx = 1\n")).name == "python-mode");
    std::string longFile = "// -*- mode: c++ -*-\n";
    for (int i = 0; i < 2000; ++i) {
        longFile += "int x" + std::to_string(i) + ";\n";
    }
    CHECK(ned::editor::ModeForPath(WriteSample("long.c", longFile)).name == "cpp-mode");
    CHECK(ned::editor::ModeForPath(WriteSample("tail.c", longFile.substr(21) + "// vim: ft=cpp\n")).name == "cpp-mode");
}

TEST_CASE("A UTF-16 file's modeline is read from its text", "[ModeDetection]") {
    const auto utf16le = [](std::string_view text) {
        std::string bytes = "\xFF\xFE";
        for (const char c : text) {
            bytes += c;
            bytes += '\0';
        }
        return bytes;
    };
    CHECK(ned::editor::ModeForPath(WriteSample("wide.txt", utf16le("# vim: ft=python\nx = 1\n"))).name == "python-mode");

    // The tail can start on either byte of a unit.
    std::string body = "# vim: ft=python\n";
    for (int i = 0; i < 3000; ++i) {
        body += "x\n";
    }
    for (const std::string& tail : {std::string("# vim: ft=ruby\n"), std::string("#  vim: ft=ruby\n")}) {
        const std::string ends = ned::editor::ReadFileEnds(WriteSample("wide-tail.txt", utf16le(body + tail)));
        CHECK(ned::editor::ParseModeline(ends).language == "ruby");
    }
}

TEST_CASE("A chosen mode outlives cache flushes and goes with its buffer", "[ModeDetection]") {
    ned::text::Buffer buffer = ned::text::Buffer::FromFile(WriteSample("chosen.c", "int x;\n"));
    CHECK(ned::editor::CachedModeForBuffer(buffer).name == "c-mode");
    REQUIRE(ned::editor::SetChosenModeForBuffer(buffer, "cpp-mode"));
    CHECK_FALSE(ned::editor::SetChosenModeForBuffer(buffer, "no-such-mode"));
    ned::editor::ClearAllModeCaches();
    CHECK(ned::editor::CachedModeForBuffer(buffer).name == "cpp-mode");

    ned::editor::ClearModeCacheFor(buffer); // what closing the buffer runs
    CHECK(ned::editor::CachedModeForBuffer(buffer).name == "c-mode");
    ned::editor::ClearModeCacheFor(buffer);

    const std::vector<std::string> names = ned::editor::ModeNames();
    CHECK(std::ranges::find(names, "verilog-mode") != names.end());
    CHECK(std::ranges::is_sorted(names));
}
