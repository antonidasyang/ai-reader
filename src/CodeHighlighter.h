#pragma once

#include <QString>

// Minimal syntax-highlighting helper used by both the chat-pane CodeBlock
// QML item and the inline-code-block path inside MarkdownRenderer. Covers
// the languages most commonly seen in LLM replies (cpp, python, js/ts,
// json, bash). Output is HTML with inline `<span style="color:…">` tags
// inside an HTML-escaped body — safe to drop into a TextEdit set to
// RichText. Unknown languages get HTML-escaped only.
//
// This is a deliberate substitute for the upstream-spec'd
// KSyntaxHighlighting (which would pull in the KDE Frameworks build
// chain). The token coverage here is narrower but the dependency
// footprint stays at zero.
namespace CodeHighlighter {

QString htmlEscape(const QString &s);
QString highlight(const QString &source, const QString &language);

} // namespace CodeHighlighter
