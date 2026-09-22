#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "Editor/BundledLanguages.h"
#include "Editor/LanguageDefinition.h"
#include "Editor/Mode.h"

using ned::editor::BundledLanguages;
using ned::editor::HighlightSpan;
using ned::editor::LanguageDefinition;
using ned::editor::Mode;
using ned::editor::ModeFromDefinition;
using ned::editor::SyntaxClass;

namespace {

Mode WgslMode() {
    for (const LanguageDefinition& definition : BundledLanguages()) {
        if (definition.name == "wgsl") {
            return ModeFromDefinition(definition);
        }
    }
    FAIL("no bundled wgsl definition");
    return {};
}

bool HasSpanContaining(const std::vector<HighlightSpan>& spans, std::size_t offset, SyntaxClass cls) {
    for (const HighlightSpan& span : spans) {
        if (span.startByte <= offset && offset < span.endByte && span.syntaxClass == cls) {
            return true;
        }
    }
    return false;
}

const std::string kShader = "// a vertex shader\n"
                            "#import bevy_pbr::mesh_functions\n"
                            "\n"
                            "struct Vertex {\n"
                            "    @location(0) position: vec3<f32>,\n"
                            "};\n"
                            "\n"
                            "@vertex\n"
                            "fn vertex(in: Vertex) -> @builtin(position) vec4<f32> {\n"
                            "    let scale = 2.0;\n"
                            "    return vec4<f32>(in.position * scale, 1.0);\n"
                            "}\n";

} // namespace

// The grammar ships no queries, so every class here comes from ned's own
// highlights.janet.
TEST_CASE("A WGSL shader highlights its declarations, types and attributes", "[Wgsl]") {
    const auto mode  = WgslMode();
    const auto spans = mode.highlight(kShader, ned::editor::HighlightWindow{});

    CHECK(HasSpanContaining(spans, kShader.find("// a vertex shader"), SyntaxClass::Comment));
    CHECK(HasSpanContaining(spans, kShader.find("#import"), SyntaxClass::Keyword));
    CHECK(HasSpanContaining(spans, kShader.find("struct"), SyntaxClass::Keyword));
    CHECK(HasSpanContaining(spans, kShader.find("Vertex {"), SyntaxClass::Type));
    CHECK(HasSpanContaining(spans, kShader.find("@location"), SyntaxClass::Attribute));
    CHECK(HasSpanContaining(spans, kShader.find("vec3"), SyntaxClass::TypeBuiltin));
    CHECK(HasSpanContaining(spans, kShader.find("fn vertex") + 3, SyntaxClass::Function));
    CHECK(HasSpanContaining(spans, kShader.find("2.0"), SyntaxClass::Number));
}
