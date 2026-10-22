/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

class EngineBase;
struct MarkdownConversion;

Str EngineMupdfToMarkdown(EngineBase*, const Vec<int>& pages, const MarkdownConversion&, Str& error);
