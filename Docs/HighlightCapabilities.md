# Highlight capabilities

The language-agnostic vocabulary of things a syntax highlighter can name, and what each
one means. This is the list every bundled `*.scm` highlight query should emit into, every
theme should be able to style, and every future language should map its own constructs
onto — rather than each language inventing its own parallel set of names.

Derived by taking the union of the per-language attribute trees JetBrains exposes
(C/C++, Rust, PHP, JavaScript, TypeScript were surveyed) and collapsing away everything
that was only a different spelling of the same idea. A trait, an interface, a protocol
and a C++ concept are one capability, not four.

Ground-truthed against `Source/Editor/SyntaxTheme.h`'s `ResolvedCaptureOverride`,
`Source/Editor/Mode.cpp`'s `SyntaxClassForCapture`/`CaptureTable`, and
`Source/UI/Theme.h`'s `BrushFor(cls, captureId)`.

## How this list is meant to work

Nothing here needs a new engine. Ned already resolves a dotted capture name
most-specific-first, one segment at a time, **per style field**:

```
function.method.static.call
  -> function.method.static
    -> function.method
      -> function
```

`ResolvedCaptureOverride` keeps the first value it finds for each of foreground /
background / bold / italic / underlined / strikethrough independently, then
`Theme::BrushFor(cls, captureId)` merges that on top of the `SyntaxClass` tier and the
built-in theme. So a theme that styles only `function` still colours every leaf under it,
and a theme that adds `italic` to `function.method` inherits `function`'s colour while
overriding only the one field — JetBrains' "Inherit values from:" checkbox, already
implemented, already per-attribute.

That has three consequences worth stating plainly, because they are what make a list this
long affordable:

1. **A name costs nothing until someone styles it.** An unstyled leaf is byte-for-byte its
   parent. There is no per-name table to populate, no theme obliged to mention it.
2. **The list is the contract, not the implementation.** A query emits a name; a theme may
   or may not have an opinion about it. The two never have to agree.
3. **Depth is free but not weightless.** Every extra segment is one more `find` on the
   parse-time path. Three segments is the working ceiling; the handful of four-segment
   names below are the ones where the fourth genuinely carries meaning
   (`function.method.static.call`).

The `SyntaxClass` enum (`Source/Editor/Mode.h`) stays the coarse tier underneath — the
fallback when a capture name is unrecognised, and what non-tree-sitter highlighters (Org's
hand-built one) still produce directly. This list sits above it.

## Naming rules

- **Role, not syntax.** `type.interface` covers a Rust trait, a Java interface, a TS
  interface, a Swift protocol and a C++ concept. If two languages spell the same idea
  differently, that is one name.
- **Specialise leftwards.** The first segment is the broad category — the thing a minimal
  theme colours. Each following segment narrows. Never `call.function`.
- **Orthogonal facets go last, in a fixed order:** `<category>.<kind>.<role>.<visibility>`,
  where *role* is `declaration`/`call` and *visibility* is `private`/`protected`/`public`.
- **Lowercase, dot-separated, no underscores.** Matches the tree-sitter/Neovim convention
  every bundled query is already written against.
- **A leading `_` is never a highlight** — `IsHighlightableCapture` already filters those,
  along with `spell`/`nospell`/`none`.

Where the tree-sitter/Neovim convention already has a name, this list uses it unchanged,
so vendored upstream queries keep working untouched. Names marked **(ned)** have no
upstream equivalent and are this project's own.

---

## 1. Comments

| Name | Meaning |
|---|---|
| `comment` | Any comment. The root every comment leaf falls back to. |
| `comment.line` | Single-line (`//`, `#`, `--`). |
| `comment.block` | Delimited (`/* */`). |
| `comment.todo` **(ned)** | `TODO`/`FIXME`/`HACK`/`XXX` markers inside any comment. |
| `comment.documentation` | A doc comment: Doxygen, JSDoc, PHPDoc, Rustdoc, a Python docstring. |
| `comment.documentation.tag` | The directive itself — `@param`, `\brief`, `#[doc]`. |
| `comment.documentation.name` **(ned)** | The identifier a tag names — `$abc` in `@param $abc`, a JSDoc namepath. |
| `comment.documentation.type` **(ned)** | A type written inside doc syntax — JSDoc `{string?}`, a PHPDoc template type. |
| `comment.documentation.text` | Prose body, as opposed to the structured parts above. |
| `comment.documentation.markup` **(ned)** | Formatting *inside* a doc comment. Children mirror `markup.*` below: `.strong`, `.emphasis`, `.heading`, `.link`, `.raw`. |

Rustdoc's Bold/Italic/Heading/Link/Code land under `comment.documentation.markup.*` rather
than `markup.*` deliberately — they should read as doc-comment first and formatting second,
which single-parent inheritance gives for free.

## 2. Literals

| Name | Meaning |
|---|---|
| `string` | A string literal, and its body text. |
| `string.escape` | An escape sequence inside one (`\n`, `\u{1F600}`). |
| `string.escape.invalid` **(ned)** | An escape that is not valid in this language. |
| `string.regexp` | A regex literal, or a string a language treats as one. |
| `string.template` **(ned)** | An interpolating string (JS template literal, PHP double-quoted, Python f-string). |
| `string.heredoc` **(ned)** | Heredoc/nowdoc body. |
| `string.heredoc.delimiter` **(ned)** | Its opening/closing identifier. |
| `string.special` | A string with non-string meaning. |
| `string.special.include` | An include/import path (`<vector>`). |
| `string.special.path` | A filesystem path. |
| `string.special.url` | A URL. |
| `string.special.key` | A key position in a mapping (JSON/YAML). |
| `string.special.symbol` | A symbol/atom/keyword literal (`:foo`, `'sym`). |
| `string.special.format` **(ned)** | A format placeholder — `%d`, `{}`, `{0}`. |
| `string.special.format.specifier` **(ned)** | The flags/width/precision inside one — `:>8.2f`. |
| `string.special.shell` **(ned)** | A string executed as a shell command (PHP backticks). |
| `character` | A single-character literal (`'a'`, Rust `char`). |
| `number` | Any numeric literal. |
| `number.integer` **(ned)** | Integer form. |
| `number.float` | Floating-point form. |
| `boolean` | `true`/`false`. |

## 3. Constants

| Name | Meaning |
|---|---|
| `constant` | A named value that cannot be reassigned. |
| `constant.builtin` | Language-provided: `null`, `nil`, `undefined`, `NULL`, `None`. |
| `constant.macro` | A macro that expands to a value (`#define MAX 10`). |
| `constant.enum` **(ned)** | An enum member/variant — C enum constant, Rust enum variant, TS enum member. |

## 4. Variables

| Name | Meaning |
|---|---|
| `variable` | Any identifier bound to a value. The catch-all; JetBrains' "Default" row. |
| `variable.local` | Scoped to a function/block. |
| `variable.global` | File- or program-scoped. |
| `variable.static` **(ned)** | Static storage duration. |
| `variable.static.mutable` **(ned)** | A mutable static (Rust `static mut`). |
| `variable.mutable` **(ned)** | A binding explicitly declared mutable in a language where immutable is the default (Rust `let mut`). |
| `variable.exported` **(ned)** | Re-exported from this module. |
| `variable.special` **(ned)** | An indirect/dynamic binding (PHP `$$name`). |
| `variable.builtin` | Language-provided: `this`, `self`, `arguments`, `__FILE__`. |
| `variable.builtin.self` **(ned)** | The receiver specifically, where a language distinguishes it. |
| `variable.builtin.self.mutable` **(ned)** | Rust's `&mut self`. |
| `variable.parameter` | A declared parameter. |
| `variable.parameter.named` **(ned)** | A named-argument label at a *call* site (`foo(count: 3)`). |
| `variable.parameter.macro` **(ned)** | A macro parameter / metavariable (`$x` in `macro_rules!`). |
| `variable.parameter.mutable` **(ned)** | A parameter declared mutable. |
| `variable.member` | A field/property of a type or object. Alias: `property`. |
| `variable.member.static` **(ned)** | A static/class-level field. |
| `variable.member.private` **(ned)** | Private visibility. |
| `variable.member.protected` **(ned)** | Protected visibility. |
| `variable.member.public` **(ned)** | Explicitly public, where a language marks it. |
| `variable.member.magic` **(ned)** | A member resolved by a language's dynamic-access hook (PHP `__get`). |

## 5. Functions

Two facets stack: **what kind** of callable it is, then **declaration vs call**.

| Name | Meaning |
|---|---|
| `function` | Any callable. |
| `function.declaration` **(ned)** | At its definition site. |
| `function.call` | At a call site. |
| `function.builtin` | Language/standard-library-provided (`print`, `strlen`). |
| `function.macro` | A macro used as a callable (`assert!`, a C function-like macro). |
| `function.local` **(ned)** | Declared in an inner scope. |
| `function.global` **(ned)** | Declared at top level. |
| `function.exported` **(ned)** | Re-exported from this module. |
| `function.method` | Bound to a type/object. |
| `function.method.declaration` **(ned)** | At its definition site. |
| `function.method.call` | At a call site. |
| `function.method.static` **(ned)** | Bound to the type, not an instance — a static method, a Rust associated function. |
| `function.method.static.call` / `.declaration` **(ned)** | Those two split. |
| `function.method.interface` **(ned)** | Declared by an interface/trait/protocol rather than the concrete type. |
| `function.method.private` / `.protected` / `.public` **(ned)** | Visibility. |
| `constructor` | A constructor/initialiser, including a Rust `Self::new`-shaped one. |

## 6. Types

| Name | Meaning |
|---|---|
| `type` | Any named type. |
| `type.builtin` | Primitive/standard: `int`, `string`, `bool`, `usize`. |
| `type.class` **(ned)** | A class. |
| `type.interface` **(ned)** | A contract without (full) implementation — interface, Rust trait, Swift protocol, C++ concept. |
| `type.struct` **(ned)** | A plain record/struct. |
| `type.enum` **(ned)** | An enumeration type (the type itself; its members are `constant.enum`). |
| `type.union` **(ned)** | A union type. |
| `type.alias` **(ned)** | `typedef`, `using X = Y`, `type X = Y`, a PHP `use ... as`. |
| `type.definition` | The defining occurrence of a type, where a language distinguishes it (C++ deduction guide). |
| `type.return` **(ned)** | A function's declared return type, distinct from other type uses. |
| `type.parameter` | A generic type parameter (`T`). |
| `type.parameter.const` **(ned)** | A generic parameter carrying a value, not a type (C++ non-type template parameter, Rust `const N: usize`). |
| `type.parameter.lifetime` **(ned)** | A Rust lifetime — a generic parameter over regions. |
| `type.narrowed` **(ned)** | A reference whose type a flow analysis narrowed (TS type guard). |
| `type.dependent` **(ned)** | A name that cannot be resolved until instantiation (C++ dependent code). |
| `type.macro` **(ned)** | A type designator inside macro syntax (`:expr`, `:ident`). |

## 7. Modules

| Name | Meaning |
|---|---|
| `module` | A namespace, module, package or crate name. |
| `module.builtin` | Language-provided (`std`, `core`). |
| `module.alias` **(ned)** | A module referenced through an alias. |

## 8. Keywords

Adopted wholesale from the tree-sitter/Neovim set, which `CaptureTable` already mirrors,
plus one addition.

| Name | Meaning |
|---|---|
| `keyword` | Any reserved word not covered below. |
| `keyword.conditional` | `if`, `else`, `switch`, `match`. |
| `keyword.conditional.ternary` | `?` / `:`. |
| `keyword.repeat` | `for`, `while`, `loop`, `do`. |
| `keyword.return` | `return`, and other control transfer (`break`, `continue`, `goto`). |
| `keyword.exception` | `try`, `catch`, `throw`, `finally`. |
| `keyword.import` | `import`, `use`, `include`, `require`. |
| `keyword.function` | The word that introduces a function (`fn`, `function`, `def`). |
| `keyword.type` | The word that introduces a type (`class`, `struct`, `enum`, `trait`). |
| `keyword.operator` | A word spelled as an operator (`new`, `sizeof`, `instanceof`, `in`). |
| `keyword.modifier` | Visibility/storage/mutability qualifiers: `public`, `static`, `const`, `mut`, `async`. |
| `keyword.coroutine` | `async`, `await`, `yield`. |
| `keyword.debug` | `assert`, `debugger`. |
| `keyword.unsafe` **(ned)** | `unsafe`, `unchecked` — a keyword that widens what the code is permitted to do. |
| `keyword.directive` | A preprocessor/compiler directive (`#include`, `#pragma`). |
| `keyword.directive.define` | `#define` specifically. |

## 9. Operators and punctuation

| Name | Meaning |
|---|---|
| `operator` | Any operator token. |
| `operator.overloaded` **(ned)** | An operator resolving to a user-defined implementation. |
| `operator.arrow` **(ned)** | `=>`, `->` as a lambda/return-type introducer. |
| `operator.try` **(ned)** | Rust's `?`. |
| `operator.concat` **(ned)** | String concatenation where a language gives it its own token (PHP `.`). |
| `punctuation` | Structural punctuation with no operator meaning. |
| `punctuation.bracket` | Any paired delimiter. |
| `punctuation.bracket.round` / `.square` / `.brace` / `.angle` **(ned)** | Split by shape, for rainbow/matched-pair styling. |
| `punctuation.delimiter` | A separator. |
| `punctuation.delimiter.comma` / `.semicolon` / `.colon` / `.dot` **(ned)** | Split by token. |
| `punctuation.special` | Punctuation that carries meaning. |
| `punctuation.special.interpolation` **(ned)** | `${` / `}` around an interpolated expression. |
| `punctuation.special.macro` **(ned)** | Macro syntax punctuation — Rust's `!`, `$`, macro grouping tokens. |
| `punctuation.special.closure` **(ned)** | Rust's closure `\| \|` pipes. |

## 10. Markup and attached names

| Name | Meaning |
|---|---|
| `label` | A goto/loop label, at declaration or reference. |
| `attribute` | An annotation/decorator/attribute — Java `@Override`, Rust `#[derive]`, PHP `#[Attr]`, a JS decorator, an XML/HTML attribute name. |
| `attribute.builtin` | Language-provided rather than user-defined. |
| `tag` | A markup element name. |
| `tag.builtin` | A standard element (`<div>`). |
| `tag.component` **(ned)** | A user-defined component element (JSX/Vue/Svelte). |
| `tag.attribute` | An attribute name within a tag. |
| `tag.delimiter` | `<`, `>`, `</`, `/>`, and a language's own code fences (PHP `<?php`/`?>`). |

## 11. Prose markup

The Org/Markdown/AsciiDoc tier. Today these live as dedicated `SyntaxClass` values
(`HeadlineLevel1..3`, `TodoKeyword`, `Checkbox`, `Strong`, ...); as capture names they
generalise across formats, which is the whole point — a Markdown task list and an Org
checkbox are one capability.

| Name | Meaning |
|---|---|
| `markup.heading` | A heading/headline. |
| `markup.heading.1` … `.6` | By level. |
| `markup.strong` | Bold. |
| `markup.emphasis` | Italic. |
| `markup.underline` | Underlined. |
| `markup.strikethrough` | Struck through. |
| `markup.link` | A link, whole. |
| `markup.link.label` | Its visible text. |
| `markup.link.url` | Its destination. |
| `markup.raw` | Inline code / verbatim / a fenced block's content. |
| `markup.list` | A list item's bullet or number. |
| `markup.list.checked` / `.unchecked` | A task/checkbox item's state. |
| `markup.quote` | Block quote. |
| `markup.marker` **(ned)** | The structural syntax itself — `#`, `*`, `-`, `>`, fence delimiters — dimmed so content reads first. Deliberately not `punctuation`: that is tuned for code. |
| `markup.todo` **(ned)** | An open workflow keyword (`TODO`, `NEXT`, `WAITING`). |
| `markup.done` **(ned)** | A closed one (`DONE`, `CANCELLED`). |

Level and keyword sets stay data, not fixed names: Org resolves its own keywords from
`org::TodoKeywords()` at runtime, and heading levels cycle.

## 12. Not syntax classes

Everything above answers "what *is* this token". The following JetBrains rows answer a
different question — "what *state* is this region in" — and do not belong in the same
namespace.

| Name | Meaning |
|---|---|
| `region.disabled` **(ned)** | Excluded by the preprocessor / `cfg` — C's "conditionally non-compiled code", Rust's "conditionally disabled code". |
| `region.generated` **(ned)** | Produced by macro expansion rather than written. |
| `region.unsafe` **(ned)** | Inside an `unsafe` block. |
| `region.injected` **(ned)** | An embedded fragment of another language. Ned's existing `embedded` capture is this. |
| `region.deprecated` **(ned)** | Marked deprecated (an LSP semantic-token modifier). |
| `region.unused` **(ned)** | Statically unreachable or unreferenced. |

**These need a mechanism Ned does not have yet.** A `HighlightSpan` is winner-takes-all
per byte range — the "later span wins" rule plus `SpanCollector`'s specificity tie-break
means a region marker painted over a function name *replaces* the function's styling
instead of tinting it. Supporting this properly means a second, additive span layer that
contributes background/effects only and never a foreground. That is a real piece of work,
not a query change, and it is the one item on this page that is not just data.

Two more rows from the survey are separate features rather than names:

- **Semantic highlighting** (JetBrains' `Color#1..#5` / `SC1.1..SC4.4` spectrum) — a
  deterministic per-identifier hue so two locals in one function never collide. A
  generator, not a capability: it produces a colour, it does not name a category.
- **Inline error/warning messages, inline explanations** (Rust pane) — editor chrome,
  already Ned's `Editor/InlineDiagnostics.h`.

And one genuine error class that *is* a syntax name:

| Name | Meaning |
|---|---|
| `error` | A token the parser could not accept. |
| `error.character` **(ned)** | A byte illegal in this language — JetBrains' "Bad character" / "Unknown character". |

## 13. Defaults: derive, don't enumerate

A hundred-and-twenty names must not become a hundred-and-twenty hand-picked colours. Every
theme would have to be rewritten, and no human would keep them coherent.

They don't have to. The list is a tree and inheritance is per-field, so a default only has
to exist at two places: **a hue per root**, and **a style delta per facet suffix**. Everything
else falls out.

### 13a. Root hues

`ThemeFromPalette` (`Source/UI/ThemePalette.cpp`) already assigns roles from the eight
accent slots, and that mapping is the one to extend — not replace, so every bundled and
cloned theme keeps reading the way it does today:

| Root | Palette slot | Already assigned? |
|---|---|---|
| `comment` | `subtleForeground` | yes |
| `string`, `character` | `green` | yes (string) |
| `number`, `boolean` | `magenta` | yes (number) |
| `constant` | `purple` | yes |
| `variable` | `foreground` | yes |
| `function`, `constructor` | `cyan` | yes |
| `type` | `yellow` | yes |
| `module` | `purple` | yes (namespace) |
| `keyword` | `blue` | yes |
| `operator` | `red` | yes |
| `punctuation` | `subtleForeground` | yes |
| `label` | `purple` | yes |
| `attribute` | `orange` | yes |
| `tag` | `blue` | yes |
| `markup` | `foreground` + per-leaf Org/Markdown fields | partly |
| `error` | `red` | no — see §15 |
| `region` | *background wash only* | no — see §15 |

Only two roots are genuinely new. The other fifteen already have a `Theme` field with a
palette derivation behind it; the work is pointing the capture names at them.

Two established exceptions stay as they are: `variable.member` (property) and
`string.escape` are `Color::Interpolate`d toward `foreground` rather than given their own
slot — a brighter same-family variant for "emphasis within an already-coloured span". That
technique is the model for the deltas below.

### 13b. Facet deltas

Each facet suffix carries one consistent, hue-independent style change, applied on top of
whatever the parent resolved to. These are the rules, not a table of values — they work
for every root, every theme, light or dark, without anybody choosing anything:

| Suffix | Delta | Rationale |
|---|---|---|
| `.builtin` | italic | "not yours" — the same cue every IDE uses for library surface |
| `.declaration` | bold | the defining occurrence should out-weigh its uses |
| `.call` | *(none)* | the common case stays the baseline |
| `.static` | italic | bound to the type, not an instance |
| `.mutable` | underlined | Rust's whole point; cheap and unmistakable |
| `.private` | blend 30% toward `subtleForeground` | receded, still legibly its own family |
| `.protected` | blend 15% toward `subtleForeground` | half a step |
| `.public` | *(none)* | the default state needs no marking |
| `.exported`, `.global` | blend 25% toward `foreground` | brighter same-family variant — `variable.member`'s existing technique |
| `.local` | *(none)* | baseline |
| `.invalid` | `red` foreground + underwaved | it is an error, and should read as one |
| `.special` | blend 25% toward `foreground` | same brighten |
| `.marker` | `subtleForeground` | structure recedes so content reads |

Composition follows the resolver: `function.method.static.call` is bold-free (`.call`),
italic (`.static`), cyan (`function`). Nobody wrote that down; it fell out.

This is what keeps a theme's job small. `ThemeFromPalette` grows a handful of derivations,
not a hundred-and-twenty fields, and a hand-built theme that never mentions
`function.method.static.call` still renders it correctly.

### 13c. Prose markup

`markup.*` is the one family whose leaves want real per-leaf values rather than deltas,
because bold/italic/strikethrough *are* the content and can't double as facet cues. Those
fields already exist (`Theme`'s Org headline/todo/checkbox/markup-marker group); the work
is renaming what feeds them from the `SyntaxClass` tier to capture names, so Markdown,
Org, AsciiDoc and a Rustdoc comment all reach the same three heading colours instead of
each carrying its own.

## 14. Customisation

The whole surface already exists in Janet, at both tiers, with getters alongside every
setter. Nothing new is needed for a user to style any name on this page:

```janet
# The coarse tier: a SyntaxClass, affects every capture that falls back to it.
(ned/set-syntax-foreground "comment" "#7f849c")
(ned/set-syntax-italic "comment" true)

# The fine tier: one capture name. Inherits per-field from less specific names.
(ned/set-capture-foreground "function.method.static" "#89b4fa")
(ned/set-capture-italic "function.method.static" true)

# Re-base a whole subtree by remapping what class it falls back to.
(ned/set-capture-class "type.interface" "type-builtin")

# Discovery.
(ned/syntax-classes)   # every class name
(ned/capture-names)    # built-ins merged with every name seen from a loaded grammar
```

`ned/set-capture-{foreground,background,bold,italic,underlined,strikethrough}` and their
`ned/capture-*` readers cover all six style fields; `ned/theme-set` persists the result as
runnable Janet. Because `ResolvedCaptureOverride` inherits per *field*, adding italic to
`function.method` leaves `function`'s colour intact — the user never has to restate what
they didn't want to change.

Three gaps worth closing as the names land:

1. **`ned/capture-names` only knows what it has seen.** It merges the built-in table with
   names from loaded grammars, so a name this page defines but no bundled query emits yet
   is invisible to completion. It should also seed from the canonical list.
2. **No subtree operation.** Styling every `function.*` leaf means naming them one at a
   time; `(ned/set-capture-foreground "function" ...)` sets the root's *own* value, which
   inheritance then propagates — correct, but "restyle this subtree and nothing else" has
   no spelling.
3. **The theme gallery doesn't enumerate capture names.** `M-x theme-gallery` reads back
   composited cells per themed surface; capture-level overrides are invisible there, which
   is exactly where a user would want to see the effect of §13b's deltas.

## 15. Theme extras to bolt on

The short list of things that genuinely need new fields rather than derivation:

| Addition | Where | Why |
|---|---|---|
| `region*Background` — disabled, generated, unsafe, injected, deprecated, unused | `Theme` | §12's overlay layer. Backgrounds only, never a foreground, so they compose with syntax colour instead of replacing it. Derivable from `background` blended toward `subtleForeground` / `accent` — no new palette slots. |
| `errorForeground` / `errorCharacterBackground` | `Theme` | "Bad character" has no home today; it currently falls to `Default` and disappears. |
| A semantic spectrum — N hues for per-identifier colouring | `ThemePalette` | JetBrains' `Color#1..#5`/`SC1.1..SC4.4`. Generate from the eight accents by rotation rather than storing new slots, so every existing theme gets one free. |
| `markup.heading` levels 4–6 | `Theme` | Three exist (Org's curated subset); Markdown and HTML both go to six. |

Everything else on this page derives. That is the design constraint: if a new capability
needs a new palette slot, the capability is probably wrong.

## What adopting this costs

In rough dependency order, and none of it is required all at once — the list is useful as
a naming contract before any of it lands.

1. **Nothing, to start.** Every name above already resolves through
   `ResolvedCaptureOverride` today. A theme can style `function.method.static` this
   afternoon; it just won't match anything until a query emits it.
2. **Base styling.** Two new roots (`error`, `region`) plus §13b's facet deltas in
   `ThemeFromPalette`. Fifteen of the seventeen roots already have a field and a palette
   derivation; leaves stay unstyled by design and inherit.
3. **`CaptureTable` entries** mapping each new name down to its nearest `SyntaxClass`, so
   the coarse tier degrades correctly for terminals/themes that only know the enum.
4. **Query work, per language.** The bundled `highlights.scm` files currently emit ~95
   distinct names; most of the specificity above (declaration-vs-call, visibility,
   static-vs-instance) simply isn't captured yet and needs real patterns written against
   each grammar.
5. **The additive overlay layer** for §12, plus the §15 fields it needs. This is the only
   genuine engine change on the page; everything above it is data and derivation.

`Tests/ThemeKeyDocsTest.cpp`'s existing both-directions check between `ThemeFile.cpp`'s key
table and `Docs/Themes.md` is the precedent for keeping this page honest once the names
are real.

---

## Appendix: survey coverage

Every row from the five language panes, and where it lands. Rows collapsing to the same
name are what makes this list shorter than the sum of its sources.

### C/C++

| CLion row | Name |
|---|---|
| Bad character | `error.character` |
| Braces / Brackets / Parentheses | `punctuation.bracket.brace` / `.square` / `.round` |
| Comma / Semicolon / Dot | `punctuation.delimiter.comma` / `.semicolon` / `.dot` |
| Operator sign | `operator` |
| Overloaded operator | `operator.overloaded` |
| Class/struct/enum/union | `type.class` / `type.struct` / `type.enum` / `type.union` |
| Block comment / Line comment | `comment.block` / `comment.line` |
| Doxygen Command / Command value / Text | `comment.documentation.tag` / `.name` / `.text` |
| Conditionally non-compiled code | `region.disabled` |
| Enum constant | `constant.enum` |
| Function Call / Declaration | `function.call` / `function.declaration` |
| 'this' keyword | `variable.builtin.self` |
| Builtin type | `type.builtin` |
| Control flow keyword | `keyword.conditional` / `keyword.repeat` |
| Control transfer keyword | `keyword.return` |
| Keyword | `keyword` |
| Macro name | `function.macro` / `constant.macro` |
| Namespace | `module` |
| Number | `number` |
| Macro parameter / Parameter | `variable.parameter.macro` / `variable.parameter` |
| Preprocessor Directive | `keyword.directive` |
| Header path | `string.special.include` |
| Semantic highlighting | *(generator — see §12)* |
| Escape Sequence | `string.escape` |
| Format specifier in string argument | `string.special.format` |
| String text | `string` |
| Struct field | `variable.member` |
| Concept | `type.interface` |
| Deduction guide | `type.definition` |
| Dependent code | `type.dependent` |
| Non-type parameter / Type parameter | `type.parameter.const` / `type.parameter` |
| Typedef | `type.alias` |
| Constant / Global variable / Local variable | `constant` / `variable.global` / `variable.local` |

### Rust

| CLion row | Name |
|---|---|
| Attribute | `attribute` |
| ? operator | `operator.try` |
| Operation sign | `operator` |
| Block/Line comment | `comment.block` / `comment.line` |
| Conditionally disabled code | `region.disabled` |
| `macro_rules!` / Macro / Macro name | `function.macro` |
| Macro colon / dollar / exclamation mark / grouping tokens | `punctuation.special.macro` |
| Macro metavariable identifier | `variable.parameter.macro` |
| Macro type designator | `type.macro` |
| Associated function call / declaration | `function.method.static.call` / `.declaration` |
| Associated trait function call / declaration | `function.method.interface` (+ `.call`/`.declaration`) |
| Trait method call / declaration | `function.method.interface` (+ `.call`/`.declaration`) |
| Closure punctuation | `punctuation.special.closure` |
| Function call / declaration | `function.call` / `function.declaration` |
| Method call / declaration | `function.method.call` / `function.method.declaration` |
| Overloaded operator | `operator.overloaded` |
| Inline error / warning messages, Inline explanations | *(editor chrome — see §12)* |
| Items generated by macros | `region.generated` |
| Keyword | `keyword` |
| Unsafe / Unsafe code | `keyword.unsafe` / `region.unsafe` |
| Char / Number / String | `character` / `number` / `string` |
| Escape sequence Valid / Invalid | `string.escape` / `string.escape.invalid` |
| Format parameter | `string.special.format` |
| Format specifier inside format parameter | `string.special.format.specifier` |
| Const parameter | `type.parameter.const` |
| Lifetime | `type.parameter.lifetime` |
| Mutable parameter | `variable.parameter.mutable` |
| Mutable self parameter / Self parameter | `variable.builtin.self.mutable` / `variable.builtin.self` |
| Parameter / Type parameter | `variable.parameter` / `type.parameter` |
| Rustdoc Bold / Italic / Heading / Link / Code / Comment | `comment.documentation.markup.strong` / `.emphasis` / `.heading` / `.link` / `.raw` / `comment.documentation` |
| Crate / Module | `module` |
| Enum / Enum variant | `type.enum` / `constant.enum` |
| Primitive / Struct / Trait / Type alias / Union | `type.builtin` / `type.struct` / `type.interface` / `type.alias` / `type.union` |
| Constant / Default / Field | `constant` / `variable` / `variable.member` |
| Mutable binding / Mutable static / Static | `variable.mutable` / `variable.static.mutable` / `variable.static` |
| Self expression | `variable.builtin.self` |

### PHP

| PhpStorm row | Name |
|---|---|
| Attributes | `attribute` |
| Operators | `operator` |
| Class / Interface | `type.class` / `type.interface` |
| Instance Property / Static Property | `variable.member` / `variable.member.static` |
| Private / Protected Instance Property | `variable.member.private` / `.protected` |
| Comments | `comment` |
| DQL Builder Expression / Placeholder | `region.injected` (+ `string.special.format`) |
| Declaration / Function Call | `function.declaration` / `function.call` |
| Instance Method Call | `function.method.call` |
| Private / Protected Instance Method Call | `function.method.private` / `.protected` (+ `.call`) |
| Static Method Call | `function.method.static.call` |
| Parameter | `variable.parameter` |
| Heredoc content / Heredoc ID | `string.heredoc` / `string.heredoc.delimiter` |
| 'this' variable | `variable.builtin.self` |
| Alias reference | `module.alias` |
| Constant / Default / Variable | `constant` / `variable` / `variable` |
| Goto label | `label` |
| Magic Member Access | `variable.member.magic` |
| Predefined symbols | `variable.builtin` |
| Primitive Type Hint | `type.builtin` |
| Variable variable | `variable.special` |
| Keywords / Numbers | `keyword` / `number` |
| Named Arguments | `variable.parameter.named` |
| PHP Code Background / Tags | `region.injected` / `tag.delimiter` |
| PHPDoc Identifier / Method Declaration / Parameter / Property / Variable | `comment.documentation.name` |
| PHPDoc Markup / Tag / Template Type / Text | `comment.documentation.markup` / `.tag` / `.type` / `.text` |
| Semantic highlighting | *(generator — see §12)* |
| Shell command | `string.special.shell` |
| Characters / Concatenation / Escape sequences | `character` / `operator.concat` / `string.escape` |
| Unknown character | `error.character` |

### JavaScript / TypeScript

| PhpStorm row | Name |
|---|---|
| Arrow function | `operator.arrow` |
| Operation / Parenthesis | `operator` / `punctuation.bracket.round` |
| Class name / Exported class | `type.class` / `type.class` + `variable.exported` |
| Static method / Static property | `function.method.static` / `variable.member.static` |
| JSDoc Tag / Tag namepath / Text / Type | `comment.documentation.tag` / `.name` / `.text` / `.type` |
| Documentation Tag / Text / Value (TS) | `comment.documentation.tag` / `.text` / `.name` |
| JSDoc type | `comment.documentation.type` |
| Decorator | `attribute` |
| Exported function / variable | `function.exported` / `variable.exported` |
| Global function / variable | `function.global` / `variable.global` |
| Local function / variable | `function.local` / `variable.local` |
| Label / Parameter | `label` / `variable.parameter` |
| Injected Language Fragment | `region.injected` |
| JSX client component | `tag.component` |
| Keyword / Number | `keyword` / `number` |
| Object Method / Property | `function.method` / `variable.member` |
| Regular expression | `string.regexp` |
| String text / Escape Sequence Valid / Invalid | `string` / `string.escape` / `string.escape.invalid` |
| Template literal | `string.template` |
| Placeholder delimiters | `punctuation.special.interpolation` |
| Enum name / Member (TS) | `type.enum` / `constant.enum` |
| Interface / Module name (TS) | `type.interface` / `module` |
| Narrowed by a type guard (TS) | `type.narrowed` |
| Primitive type / Type alias / Type parameter (TS) | `type.builtin` / `type.alias` / `type.parameter` |
| Semantic highlighting | *(generator — see §12)* |
