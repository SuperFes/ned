//
// The pure half of file-rename propagation (Editor/ImportFixup.h): given a
// specifier's own style and where the file it named has moved to, what the
// specifier should say now. No filesystem access at all -- every path here
// is arithmetic over absolute paths that need not exist, which is the point
// of the module being pure.
//

#include <catch2/catch_test_macros.hpp>

#include "Editor/ImportFixup.h"

using ned::editor::importfix::DottedModuleFor;
using ned::editor::importfix::RewriteRequest;
using ned::editor::importfix::RewriteSpec;
using ned::editor::importfix::SpecKind;

TEST_CASE("A quoted C include keeps its bare, extension-carrying style", "[ImportFixup]") {
    RewriteRequest request;
    request.kind              = SpecKind::RelativePath;
    request.spec              = "Widget.h";
    request.importerDirectory = "/p/Source/UI";
    request.newTarget         = "/p/Source/UI/Core/Widget.h";

    REQUIRE(RewriteSpec(request) == "Core/Widget.h");
}

TEST_CASE("An include that has to ascend says so even without a ./ original", "[ImportFixup]") {
    RewriteRequest request;
    request.kind              = SpecKind::RelativePath;
    request.spec              = "Widget.h";
    request.importerDirectory = "/p/Source/UI";
    request.newTarget         = "/p/Source/Text/Widget.h";

    REQUIRE(RewriteSpec(request) == "../Text/Widget.h");
}

TEST_CASE("A JS relative import keeps its ./ prefix and its missing extension", "[ImportFixup]") {
    RewriteRequest request;
    request.kind              = SpecKind::RelativePath;
    request.spec              = "./widget";
    request.importerDirectory = "/p/src";
    request.newTarget         = "/p/src/ui/widget.ts";

    REQUIRE(RewriteSpec(request) == "./ui/widget");
}

TEST_CASE("A JS import written with its extension keeps it", "[ImportFixup]") {
    RewriteRequest request;
    request.kind              = SpecKind::RelativePath;
    request.spec              = "./widget.js";
    request.importerDirectory = "/p/src";
    request.newTarget         = "/p/src/ui/widget.js";

    REQUIRE(RewriteSpec(request) == "./ui/widget.js");
}

TEST_CASE("An ascending relative import never grows a ./ prefix", "[ImportFixup]") {
    RewriteRequest request;
    request.kind              = SpecKind::RelativePath;
    request.spec              = "./widget";
    request.importerDirectory = "/p/src/app";
    request.newTarget         = "/p/src/ui/widget.ts";

    REQUIRE(RewriteSpec(request) == "../ui/widget");
}

TEST_CASE("An absolute specifier is declined, not restyled", "[ImportFixup]") {
    RewriteRequest request;
    request.kind              = SpecKind::RelativePath;
    request.spec              = "/usr/include/widget.h";
    request.importerDirectory = "/p/src";
    request.newTarget         = "/p/src/widget.h";

    REQUIRE_FALSE(RewriteSpec(request).has_value());
}

TEST_CASE("An unsupported specifier kind is always declined", "[ImportFixup]") {
    RewriteRequest request;
    request.kind              = SpecKind::Unsupported;
    request.spec              = "App\\Models\\User";
    request.importerDirectory = "/p/src";
    request.newTarget         = "/p/src/Models/User.php";

    REQUIRE_FALSE(RewriteSpec(request).has_value());
}

TEST_CASE("A dotted Python module is recomputed against its own root", "[ImportFixup]") {
    RewriteRequest request;
    request.kind           = SpecKind::DottedModule;
    request.spec           = "pkg.widget";
    request.resolutionRoot = "/p";
    request.newTarget      = "/p/pkg/ui/widget.py";

    REQUIRE(RewriteSpec(request) == "pkg.ui.widget");
}

TEST_CASE("A dotted module that leaves its root is declined", "[ImportFixup]") {
    RewriteRequest request;
    request.kind           = SpecKind::DottedModule;
    request.spec           = "pkg.widget";
    request.resolutionRoot = "/p/src";
    request.newTarget      = "/p/vendor/widget.py";

    REQUIRE_FALSE(RewriteSpec(request).has_value());
}

TEST_CASE("A package index file names its package, not itself", "[ImportFixup]") {
    REQUIRE(DottedModuleFor("/p/pkg/sub/__init__.py", "/p", "__init__") == "pkg.sub");
    REQUIRE(DottedModuleFor("/p/pkg/sub/mod.py", "/p", "__init__") == "pkg.sub.mod");
    REQUIRE_FALSE(DottedModuleFor("/p/__init__.py", "/p", "__init__").has_value());
}

TEST_CASE("A Python relative import recomputes its dot count", "[ImportFixup]") {
    RewriteRequest sameDirectory;
    sameDirectory.kind              = SpecKind::RelativeModule;
    sameDirectory.spec              = "widget";
    sameDirectory.importerDirectory = "/p/pkg";
    sameDirectory.newTarget         = "/p/pkg/widget.py";
    REQUIRE(RewriteSpec(sameDirectory) == ".widget");

    RewriteRequest oneDeeper = sameDirectory;
    oneDeeper.newTarget      = "/p/pkg/ui/widget.py";
    REQUIRE(RewriteSpec(oneDeeper) == ".ui.widget");

    RewriteRequest oneUp = sameDirectory;
    oneUp.newTarget      = "/p/widget.py";
    REQUIRE(RewriteSpec(oneUp) == "..widget");

    RewriteRequest twoUpAndOver    = sameDirectory;
    twoUpAndOver.importerDirectory = "/p/pkg/deep/deeper";
    twoUpAndOver.newTarget         = "/p/pkg/other/widget.py";
    REQUIRE(RewriteSpec(twoUpAndOver) == "...other.widget");
}

TEST_CASE("A relative import of a package names the package", "[ImportFixup]") {
    RewriteRequest request;
    request.kind              = SpecKind::RelativeModule;
    request.spec              = "sub";
    request.importerDirectory = "/p/pkg";
    request.newTarget         = "/p/pkg/moved/sub/__init__.py";

    REQUIRE(RewriteSpec(request) == ".moved.sub");
}

//
// The planner half: real files on disk, because resolving an import to the
// file it names is an on-disk question. Every test here plans BEFORE the
// move, which is the contract (see ImportFixup.h).
//

#include <filesystem>
#include <fstream>

#include "Editor/ImportFixup.h"
#include "Editor/Project/Root.h"
#include "Editor/Project/Search.h"

using ned::editor::importfix::ApplyFixup;
using ned::editor::importfix::CandidatePattern;
using ned::editor::importfix::FixupPlan;
using ned::editor::importfix::MovedFile;
using ned::editor::importfix::PlanImportFixups;

namespace {

void Write(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path) << text;
}

// Scratch project root, restored process-wide on the way out -- ProjectRoot
// is one of this codebase's mutex-guarded static settings and every other
// test shares it.
struct ScratchProject {
    std::filesystem::path root;
    std::filesystem::path previousRoot;

    explicit ScratchProject(const std::string& name) : root(std::filesystem::temp_directory_path() / ("ned_importfixup_test_" + name)),
                                                       previousRoot(ned::editor::ProjectRoot()) {
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        ned::editor::SetProjectRoot(root);
    }
    ~ScratchProject() {
        ned::editor::SetProjectRoot(previousRoot);
        std::filesystem::remove_all(root);
    }
    ScratchProject(const ScratchProject&)            = delete;
    ScratchProject& operator=(const ScratchProject&) = delete;
};

ned::editor::importfix::TextReader DiskReader() {
    return [](const std::filesystem::path& path) -> std::optional<std::string> {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return std::nullopt;
        }
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };
}

// The candidate list a real rename builds: Editor/Project/Search.h's own
// scan for the moved file's name, which is what BufferView feeds the
// planner.
std::vector<std::filesystem::path> Candidates(const std::filesystem::path&                          root,
                                              const std::vector<ned::editor::importfix::MovedFile>& moved) {
    const std::string                  pattern = ned::editor::importfix::CandidatePattern(moved);
    std::vector<std::filesystem::path> files;
    std::filesystem::path              previous;
    for (const ned::editor::SearchMatch& match : ned::editor::SearchDirectory(root, pattern)) {
        if (match.file == previous) {
            continue;
        }
        previous = match.file;
        files.push_back(match.file);
    }
    return files;
}

} // namespace

TEST_CASE("A moved header's includers are rewritten, keeping their root-relative style", "[ImportFixup]") {
    const ScratchProject project("root_relative_include");
    Write(project.root / "Editor" / "Widget.h", "#pragma once\n");
    Write(project.root / "UI" / "Pane.cpp", "#include \"Editor/Widget.h\"\n\nint main() {}\n");

    const std::vector<MovedFile> moved{{project.root / "Editor" / "Widget.h", project.root / "Text" / "Widget.h"}};
    const FixupPlan              plan = PlanImportFixups(moved, Candidates(project.root, moved), DiskReader());

    REQUIRE(plan.files.size() == 1);
    CHECK(plan.files[0].file == project.root / "UI" / "Pane.cpp");
    CHECK(ApplyFixup(plan.files[0]) == "#include \"Text/Widget.h\"\n\nint main() {}\n");
}

TEST_CASE("A sibling include stays relative to the file that wrote it", "[ImportFixup]") {
    const ScratchProject project("sibling_include");
    Write(project.root / "UI" / "Widget.h", "#pragma once\n");
    Write(project.root / "UI" / "Pane.cpp", "#include \"Widget.h\"\n");

    const std::vector<MovedFile> moved{{project.root / "UI" / "Widget.h", project.root / "UI" / "Core" / "Widget.h"}};
    const FixupPlan              plan = PlanImportFixups(moved, Candidates(project.root, moved), DiskReader());

    REQUIRE(plan.files.size() == 1);
    CHECK(ApplyFixup(plan.files[0]) == "#include \"Core/Widget.h\"\n");
}

TEST_CASE("An angle-form include is never rewritten", "[ImportFixup]") {
    const ScratchProject project("angle_include");
    Write(project.root / "vector", "");
    Write(project.root / "UI" / "Pane.cpp", "#include <vector>\n");

    const std::vector<MovedFile> moved{{project.root / "vector", project.root / "std" / "vector"}};
    const FixupPlan              plan = PlanImportFixups(moved, Candidates(project.root, moved), DiskReader());

    CHECK(plan.files.empty());
    CHECK(plan.declined == 1);
}

TEST_CASE("The moved file's own relative imports follow it", "[ImportFixup]") {
    const ScratchProject project("moved_files_own_imports");
    Write(project.root / "src" / "util.js", "export const x = 1;\n");
    Write(project.root / "src" / "app.js", "import { x } from './util';\n");

    const std::vector<MovedFile> moved{{project.root / "src" / "app.js", project.root / "src" / "deep" / "app.js"}};
    const FixupPlan              plan = PlanImportFixups(moved, Candidates(project.root, moved), DiskReader());

    REQUIRE(plan.files.size() == 1);
    CHECK(plan.files[0].file == project.root / "src" / "deep" / "app.js");
    CHECK(ApplyFixup(plan.files[0]) == "import { x } from '../util';\n");
}

TEST_CASE("Both directions are planned by one call", "[ImportFixup]") {
    const ScratchProject project("both_directions");
    Write(project.root / "src" / "util.js", "export const x = 1;\n");
    Write(project.root / "src" / "app.js", "import { x } from './util';\n");
    Write(project.root / "src" / "main.js", "import './app';\n");

    const std::vector<MovedFile> moved{{project.root / "src" / "app.js", project.root / "src" / "deep" / "app.js"}};
    const FixupPlan              plan = PlanImportFixups(moved, Candidates(project.root, moved), DiskReader());

    REQUIRE(plan.files.size() == 2);
    std::string appText;
    std::string mainText;
    for (const auto& fixup : plan.files) {
        (fixup.file.filename() == "app.js" ? appText : mainText) = ApplyFixup(fixup);
    }
    CHECK(appText == "import { x } from '../util';\n");
    CHECK(mainText == "import './deep/app';\n");
}

TEST_CASE("A Python dotted import is rewritten against its own root", "[ImportFixup]") {
    const ScratchProject project("python_dotted");
    Write(project.root / "pkg" / "__init__.py", "");
    Write(project.root / "pkg" / "widget.py", "VALUE = 1\n");
    Write(project.root / "app.py", "from pkg.widget import VALUE\n");

    const std::vector<MovedFile> moved{
        {project.root / "pkg" / "widget.py", project.root / "pkg" / "ui" / "widget.py"}};
    const FixupPlan plan = PlanImportFixups(moved, Candidates(project.root, moved), DiskReader());

    REQUIRE(plan.files.size() == 1);
    CHECK(ApplyFixup(plan.files[0]) == "from pkg.ui.widget import VALUE\n");
}

TEST_CASE("A Python relative import's dot count is recomputed", "[ImportFixup]") {
    const ScratchProject project("python_relative");
    Write(project.root / "pkg" / "__init__.py", "");
    Write(project.root / "pkg" / "widget.py", "VALUE = 1\n");
    Write(project.root / "pkg" / "app.py", "from .widget import VALUE\n");

    const std::vector<MovedFile> moved{{project.root / "pkg" / "widget.py", project.root / "widget.py"}};
    const FixupPlan              plan = PlanImportFixups(moved, Candidates(project.root, moved), DiskReader());

    REQUIRE(plan.files.size() == 1);
    CHECK(ApplyFixup(plan.files[0]) == "from ..widget import VALUE\n");
}

TEST_CASE("A file that never names the moved file is not even parsed", "[ImportFixup]") {
    const ScratchProject project("prefilter");
    Write(project.root / "Editor" / "Widget.h", "#pragma once\n");
    Write(project.root / "UI" / "Pane.cpp", "#include \"Editor/Widget.h\"\n");
    Write(project.root / "UI" / "Other.cpp", "#include <vector>\n");

    const std::vector<MovedFile> moved{{project.root / "Editor" / "Widget.h", project.root / "Text" / "Widget.h"}};
    const FixupPlan              plan = PlanImportFixups(moved, Candidates(project.root, moved), DiskReader());

    // Pane.cpp plus the moved file itself (always scanned, for its own
    // imports) -- Other.cpp never spells "Widget", so it is never parsed.
    CHECK(plan.scanned == 2);
    CHECK(plan.files.size() == 1);
}

TEST_CASE("The text reader's live content is what gets planned against", "[ImportFixup]") {
    const ScratchProject project("live_reader");
    Write(project.root / "Editor" / "Widget.h", "#pragma once\n");
    Write(project.root / "UI" / "Pane.cpp", "#include \"Editor/Widget.h\"\n");

    const std::vector<MovedFile> moved{{project.root / "Editor" / "Widget.h", project.root / "Text" / "Widget.h"}};
    const FixupPlan              plan = PlanImportFixups(
        moved, Candidates(project.root, moved),
        [](const std::filesystem::path& path) -> std::optional<std::string> {
            if (path.filename() != "Pane.cpp") {
                return std::string("#pragma once\n");
            }
            return std::string("// unsaved edit\n#include \"Editor/Widget.h\"\n");
        });

    REQUIRE(plan.files.size() == 1);
    CHECK(ApplyFixup(plan.files[0]) == "// unsaved edit\n#include \"Text/Widget.h\"\n");
}

TEST_CASE("An import that already resolves correctly is left alone", "[ImportFixup]") {
    const ScratchProject project("no_change");
    Write(project.root / "Editor" / "Widget.h", "#pragma once\n");
    Write(project.root / "UI" / "Pane.cpp", "#include \"Editor/Widget.h\"\n");

    // The file moves within its own directory's name only -- the includer
    // said nothing that has to change.
    const std::vector<MovedFile> moved{{project.root / "UI" / "Pane.cpp", project.root / "UI" / "Pane2.cpp"}};
    const FixupPlan              plan = PlanImportFixups(moved, Candidates(project.root, moved), DiskReader());

    CHECK(plan.files.empty());
    CHECK(plan.declined == 0);
}

//
// Planning AFTER the move -- what an externally detected move (a git mv, a
// file manager) leaves behind: the file a specifier named is already gone,
// so there is nothing left to resolve and the inverse question has to answer
// instead.
//

TEST_CASE("An import is fixed even when the move already happened", "[ImportFixup]") {
    const ScratchProject project("post_move_root_relative");
    Write(project.root / "Text" / "Widget.h", "#pragma once\n"); // already at its new home
    Write(project.root / "UI" / "Pane.cpp", "#include \"Editor/Widget.h\"\n");

    const std::vector<MovedFile> moved{{project.root / "Editor" / "Widget.h", project.root / "Text" / "Widget.h"}};
    const FixupPlan              plan = PlanImportFixups(moved, Candidates(project.root, moved), DiskReader());

    REQUIRE(plan.files.size() == 1);
    CHECK(ApplyFixup(plan.files[0]) == "#include \"Text/Widget.h\"\n");
}

TEST_CASE("A post-move sibling import keeps its importer-relative style", "[ImportFixup]") {
    const ScratchProject project("post_move_sibling");
    Write(project.root / "src" / "ui" / "widget.ts", "export const x = 1;\n");
    Write(project.root / "src" / "app.ts", "import { x } from './widget';\n");

    const std::vector<MovedFile> moved{
        {project.root / "src" / "widget.ts", project.root / "src" / "ui" / "widget.ts"}};
    const FixupPlan plan = PlanImportFixups(moved, Candidates(project.root, moved), DiskReader());

    REQUIRE(plan.files.size() == 1);
    CHECK(ApplyFixup(plan.files[0]) == "import { x } from './ui/widget';\n");
}

TEST_CASE("An import broken for unrelated reasons is left alone", "[ImportFixup]") {
    const ScratchProject project("unrelated_broken_import");
    Write(project.root / "Text" / "Widget.h", "#pragma once\n");
    Write(project.root / "UI" / "Pane.cpp", "#include \"Editor/Typo.h\"\n#include \"Editor/Widget.h\"\n");

    const std::vector<MovedFile> moved{{project.root / "Editor" / "Widget.h", project.root / "Text" / "Widget.h"}};
    const FixupPlan              plan = PlanImportFixups(moved, Candidates(project.root, moved), DiskReader());

    // Only the second line named the moved file; the typo resolves to
    // nothing this move can explain and stays exactly as written.
    REQUIRE(plan.files.size() == 1);
    CHECK(ApplyFixup(plan.files[0]) == "#include \"Editor/Typo.h\"\n#include \"Text/Widget.h\"\n");
}

TEST_CASE("A file moved externally gets its own relative imports fixed", "[ImportFixup]") {
    const ScratchProject project("post_move_own_imports");
    Write(project.root / "src" / "util.js", "export const x = 1;\n");
    // app.js is already at its new home: its own "./util" now has to ascend.
    Write(project.root / "src" / "deep" / "app.js", "import { x } from './util';\n");

    const std::vector<MovedFile> moved{
        {project.root / "src" / "app.js", project.root / "src" / "deep" / "app.js"}};
    const FixupPlan plan = PlanImportFixups(moved, Candidates(project.root, moved), DiskReader());

    REQUIRE(plan.files.size() == 1);
    CHECK(plan.files[0].file == project.root / "src" / "deep" / "app.js");
    CHECK(ApplyFixup(plan.files[0]) == "import { x } from '../util';\n");
}
