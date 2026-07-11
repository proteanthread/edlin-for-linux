/*
 * edlin.cpp - Portable Line Editor (colored syntax edition)
 * Standalone DOS EDLIN Clone
 * VERSION: 3.1.0
 * LICENSE: MIT License
 * COPYLEFT: BASIC++ Community
 *
 *    The editor presents a '*' prompt and accepts single-letter
 *    commands: l(ist), i(nsert), d(elete), e(nd/save), q(uit),
 *    c(opy), m(ove), p(age), s(earch), r(eplace), t(ransfer),
 *    w(rite), a(ppend), h/?. Numeric input edits that specific line.
 *
 */

/* Standard Library */

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <string_view>

/* Platform Detection */

#ifdef _WIN32
#  include <io.h>
#ifndef NOMINMAX
#  define NOMINMAX      /* Prevents Windows from defining min() and max() macros */
#endif
#  include <windows.h>
#else
#  include <unistd.h>
#  include <sys/ioctl.h>
#endif

/* Compile-Time Constants */

#define MAX_LINES   1024   /* Maximum lines held in the memory buffer. */
#define LINE_LENGTH  128   /* Maximum bytes per line (127 chars + NUL). */
#define PAGE_LENGTH   23   /* Lines shown by the 'p' (page) command.   */

static constexpr const char *EDLIN_VERSION = "3.1.0";

/* 
 *  SECTION 1 - Color / Terminal Configuration
 * 
 */

namespace cfg {

bool force_no_color = false;

constexpr std::string_view RESET   = "\033[0m";
constexpr std::string_view CYAN    = "\033[36m";
constexpr std::string_view GREEN   = "\033[32m";
constexpr std::string_view MAGENTA = "\033[35m";
constexpr std::string_view YELLOW  = "\033[33m";
constexpr std::string_view GRAY    = "\033[90m";

/* Returns true the first time it is called, then caches the result. */
inline bool detect_color_output_enabled()
{
    if (force_no_color || std::getenv("NO_COLOR")) {
        return false;
    }

#ifdef _WIN32
    if (!_isatty(_fileno(stdout))) {
        return false;
    }
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD mode = 0;
    if (!GetConsoleMode(h, &mode)) {
        return false;
    }
    return SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
    if (!isatty(fileno(stdout))) {
        return false;
    }
    const char *term = std::getenv("TERM");
    return term != nullptr && std::strcmp(term, "dumb") != 0;
#endif
}

inline bool cached_color_output_enabled()
{
    static const bool enabled = detect_color_output_enabled();
    return enabled;
}

inline int get_terminal_height()
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        return csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }
    return PAGE_LENGTH + 1;
#else
    struct winsize w;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) != -1) {
        return w.ws_row;
    }
    return PAGE_LENGTH + 1;
#endif
}

/* Supported language families for syntax highlighting. */
enum class Lang {
    CPP, C, JAVA, JS, ASM, PASCAL_LANG, BASIC, POWERSHELL, VBSCRIPT, BATCH,
    BASH, FORTRAN, ALGOL, MODULA, OBJC, FORTH, XML, YAML, JSON, UNKNOWN
};

/* Sorted keyword list used by binary_search in print_highlighted(). */
constexpr std::array<std::string_view, 133> KEYWORDS = {
    "BEGIN", "BNE", "CALL", "CMP", "DATA", "DIM", "DO", "ELSE", "END",
    "FOR", "FUNCTION", "GOSUB", "GOTO", "IF", "INPUT", "INTEGER", "JMP",
    "LDA", "LET", "MOV", "NEXT", "NOP", "POP", "PRINT", "PROCEDURE",
    "PROGRAM", "PUSH", "READ", "REAL", "REM", "RET", "STA", "SUBROUTINE",
    "THEN", "VAR", "WHILE", "WRITE", "abstract", "asm", "assert", "auto",
    "await", "bash", "bool", "boolean", "break", "byte", "case", "catch",
    "char", "class", "const", "continue", "debugger", "default", "delete",
    "do", "double", "echo", "else", "enum", "export", "extends", "extern",
    "false", "fi", "final", "finally", "float", "for", "from", "function",
    "goto", "if", "implements", "import", "in", "inline", "instanceof",
    "int", "interface", "java", "javascript", "json", "let", "long", "module",
    "namespace", "native", "new", "null", "package", "pascal", "powershell",
    "private", "protected", "public", "return", "set", "short", "signed",
    "sizeof", "static", "strictfp", "struct", "super", "switch", "synchronized",
    "template", "then", "this", "throw", "throws", "transient", "true", "try",
    "typedef", "typeof", "unsigned", "uses", "var", "vbscript", "void", "volatile",
    "while", "with", "xml", "yaml", "yield"
};

} /* namespace cfg */

/* 
 *  SECTION 2 - Syntax Highlighter
 * 
 */

class SyntaxHighlighter {
public:

    /* Detect language from filename extension; fall back to content heuristics. */
    static cfg::Lang detect_language(
        const char *filename,
        const std::vector<std::string> &buf)
    {
        std::string_view fn(filename ? filename : "");

        /* Helper: true when fn ends with ext. */
        auto ends = [&](std::string_view ext) -> bool {
            return fn.size() >= ext.size()
                && fn.substr(fn.size() - ext.size()) == ext;
        };

        if (ends(".cpp") || ends(".cc") || ends(".cxx") ||
            ends(".hpp") || ends(".h"))           return cfg::Lang::CPP;
        if (ends(".c"))                           return cfg::Lang::C;
        if (ends(".java"))                        return cfg::Lang::JAVA;
        if (ends(".js")  || ends(".mjs") ||
            ends(".cjs") || ends(".jsx"))         return cfg::Lang::JS;
        if (ends(".asm") || ends(".s")   ||
            ends(".a86") || ends(".a65") ||
            ends(".a09") || ends(".z80"))         return cfg::Lang::ASM;
        if (ends(".pas") || ends(".pp")  ||
            ends(".lpr") || ends(".tp")  ||
            ends(".qp"))                          return cfg::Lang::PASCAL_LANG;
        if (ends(".bas") || ends(".gwb") ||
            ends(".qbs"))                         return cfg::Lang::BASIC;
        if (ends(".ps1"))                         return cfg::Lang::POWERSHELL;
        if (ends(".vbs"))                         return cfg::Lang::VBSCRIPT;
        if (ends(".bat") || ends(".cmd"))         return cfg::Lang::BATCH;
        if (ends(".sh"))                          return cfg::Lang::BASH;
        if (ends(".f")   || ends(".for") ||
            ends(".f90"))                         return cfg::Lang::FORTRAN;
        if (ends(".alg"))                         return cfg::Lang::ALGOL;
        if (ends(".mod"))                         return cfg::Lang::MODULA;
        if (ends(".m"))                           return cfg::Lang::OBJC;
        if (ends(".fs"))                          return cfg::Lang::FORTH;
        if (ends(".xml"))                         return cfg::Lang::XML;
        if (ends(".yml") || ends(".yaml"))        return cfg::Lang::YAML;
        if (ends(".json"))                        return cfg::Lang::JSON;

        return detect_by_content(buf);
    }

    /* Print one line with ANSI color codes, if the terminal supports them. */
    static void print_highlighted(std::string_view line, cfg::Lang lang)
    {
        const bool ansi = cfg::cached_color_output_enabled();

        auto color = [&](std::string_view c) {
            if (ansi) std::cout << c;
        };

        bool in_string = false;
        char quote = '\0';

        for (size_t i = 0; i < line.size(); ) {
            /* Rest-of-line comment? */
            if (!in_string && starts_comment(line, i, lang)) {
                color(cfg::GRAY);
                std::cout << line.substr(i);
                color(cfg::RESET);
                break;
            }

            /* String literals */
            if (line[i] == '"' || line[i] == '\'') {
                if (!in_string) {
                    in_string = true;
                    quote = line[i];
                    color(cfg::GREEN);
                    std::cout << line[i];
                } else if (line[i] == quote) {
                    in_string = false;
                    std::cout << line[i];
                    color(cfg::RESET);
                } else {
                    std::cout << line[i];
                }
                ++i;
                continue;
            }

            if (in_string) {
                std::cout << line[i++];
                continue;
            }

            /* Numeric literals */
            if (std::isdigit(static_cast<unsigned char>(line[i]))) {
                color(cfg::MAGENTA);
                while (i < line.size() &&
                       (std::isalnum(static_cast<unsigned char>(line[i])) ||
                        line[i] == '.' || line[i] == '_')) {
                    std::cout << line[i++];
                }
                color(cfg::RESET);
                continue;
            }

            /* Structural punctuation */
            if (line[i] == '<' || line[i] == '>' ||
                line[i] == '{' || line[i] == '}' || line[i] == ':') {
                color(cfg::YELLOW);
                std::cout << line[i++];
                color(cfg::RESET);
                continue;
            }

            /* Identifiers - check against keyword list */
            if (std::isalpha(static_cast<unsigned char>(line[i])) ||
                line[i] == '_') {
                size_t s = i;
                while (i < line.size() &&
                       (std::isalnum(static_cast<unsigned char>(line[i])) ||
                        line[i] == '_' || line[i] == '$')) {
                    ++i;
                }
                std::string_view word = line.substr(s, i - s);
                if (std::binary_search(cfg::KEYWORDS.begin(),
                                       cfg::KEYWORDS.end(), word)) {
                    color(cfg::CYAN);
                    std::cout << word;
                    color(cfg::RESET);
                } else {
                    std::cout << word;
                }
                continue;
            }

            std::cout << line[i++];
        }

        color(cfg::RESET);
        std::cout << '\n';
    }

private:

    /* True when line[i..] starts with the literal string m. */
    static bool starts_with(std::string_view s, size_t i, std::string_view m)
    {
        return i + m.size() <= s.size() && s.substr(i, m.size()) == m;
    }

    /* True when position i in line is the start of a line comment for lang. */
    static bool starts_comment(std::string_view line, size_t i, cfg::Lang lang)
    {
        switch (lang) {
            case cfg::Lang::CPP:
            case cfg::Lang::C:
            case cfg::Lang::JAVA:
            case cfg::Lang::JS:
            case cfg::Lang::OBJC:
            case cfg::Lang::ALGOL:
                return starts_with(line, i, "//") || starts_with(line, i, "/*");

            case cfg::Lang::ASM:
                return line[i] == ';';

            case cfg::Lang::PASCAL_LANG:
                return starts_with(line, i, "//") ||
                       starts_with(line, i, "{")  ||
                       starts_with(line, i, "(*");

            case cfg::Lang::BASIC:
            case cfg::Lang::VBSCRIPT:
                return line[i] == '\'' ||
                       starts_with(line, i, "REM") ||
                       starts_with(line, i, "rem");

            case cfg::Lang::POWERSHELL:
            case cfg::Lang::BASH:
            case cfg::Lang::YAML:
                return line[i] == '#';

            case cfg::Lang::BATCH:
                return starts_with(line, i, "::") ||
                       starts_with(line, i, "REM") ||
                       starts_with(line, i, "rem");

            case cfg::Lang::FORTRAN:
                return line[i] == '!' ||
                       (i == 0 && (line[i] == 'c' ||
                                   line[i] == 'C' ||
                                   line[i] == '*'));

            case cfg::Lang::MODULA:
                return starts_with(line, i, "(*");

            case cfg::Lang::FORTH:
                return line[i] == '\\';

            case cfg::Lang::XML:
                return starts_with(line, i, "<!--");

            case cfg::Lang::JSON:
            case cfg::Lang::UNKNOWN:
                return false;
        }
        return false;
    }

    /* True when line contains at least one non-empty string from needles. */
    template <size_t N>
    static bool contains_any(std::string_view line,
                              const std::array<std::string_view, N> &needles)
    {
        for (std::string_view n : needles) {
            if (!n.empty() && line.find(n) != std::string_view::npos) {
                return true;
            }
        }
        return false;
    }

    /* Score a single line against keyword / remark patterns for a language. */
    static int score_line(
        std::string_view l,
        const std::array<std::string_view, 8> &kw,
        const std::array<std::string_view, 8> &cmd,
        const std::array<std::string_view, 8> &decl,
        const std::array<std::string_view, 8> &fn,
        const std::array<std::string_view, 4> &vars,
        const std::array<std::string_view, 4> &remarks)
    {
        int s = 0;
        if (contains_any(l, kw))   s += 2;
        if (contains_any(l, cmd))  s += 2;
        if (contains_any(l, decl)) s += 2;
        if (contains_any(l, fn))   s += 1;
        if (contains_any(l, vars)) s += 1;
        for (std::string_view r : remarks) {
            if (!r.empty() && starts_with(l, 0, r)) s += 1;
        }
        return s;
    }

    /* Heuristic content-based language detection (scans first 80 lines). */
    static cfg::Lang detect_by_content(
        const std::vector<std::string> &buf)
    {
        /* Per-language score accumulators. */
        int cpp = 0, js = 0, java = 0, asmx = 0, pas = 0, bas  = 0,
            psh = 0, vbs = 0, bat  = 0, bash = 0, fort = 0, xml = 0,
            yaml = 0, json = 0;

        /* Keyword fingerprints for each language. */
        const std::array<std::string_view, 8> cpp_kw  =
            {"#include","namespace","template","std::","::","->","nullptr","constexpr"};
        const std::array<std::string_view, 8> js_kw   =
            {"function ","=>","console.","let ","const ","var ","export ","import "};
        const std::array<std::string_view, 8> java_kw =
            {"public class","private ","protected ","implements ",
             "extends ","package ","import java","System.out"};
        const std::array<std::string_view, 8> asm_kw  =
            {" mov "," jmp "," lda "," sta "," org "," equ "," db "," dw "};
        const std::array<std::string_view, 8> pas_kw  =
            {"begin","end.","procedure","function","var ","uses ","unit ",":="};
        const std::array<std::string_view, 8> bas_kw  =
            {"PRINT ","INPUT ","GOTO ","GOSUB ","DIM ","THEN","NEXT","WEND"};
        const std::array<std::string_view, 8> psh_kw  =
            {"Write-Host","Get-","Set-","$true","$false","param(","function ","|"};
        const std::array<std::string_view, 8> vbs_kw  =
            {"Option Explicit","WScript.","MsgBox","Dim ",
             "Set ","Function ","Sub ","End Sub"};
        const std::array<std::string_view, 8> bat_kw  =
            {"@echo off","set ","if ","goto ","call ","rem ","::","%" };
        const std::array<std::string_view, 8> bash_kw =
            {"#!/bin/","echo ","fi","then","done","$PATH",
             "export ","#!/usr/bin/env bash"};
        const std::array<std::string_view, 8> fort_kw =
            {"PROGRAM ","SUBROUTINE ","INTEGER ","REAL ",
             "END PROGRAM","IMPLICIT NONE","!","DO "};
        const std::array<std::string_view, 8> xml_kw  =
            {"<?xml","</","/>","<",">","<!--","<!DOCTYPE","<tag"};
        const std::array<std::string_view, 8> yaml_kw =
            {"---",": ","- ","#","true","false","null","  "};
        const std::array<std::string_view, 8> json_kw =
            {"{","}","[","]","\":",  "true","false","null"};

        /* Empty padding arrays for unused scoring columns. */
        const std::array<std::string_view, 8> e8 = {"","","","","","","",""};
        const std::array<std::string_view, 4> v4 = {"$","@","_","this"};
        const std::array<std::string_view, 4> r4 = {"//","#",";","--"};

        const int limit = std::min(static_cast<int>(buf.size()), 80);
        for (int i = 0; i < limit; ++i) {
            std::string_view l(buf[static_cast<size_t>(i)].c_str());
            cpp  += score_line(l, cpp_kw,  e8, e8, e8, v4, r4);
            js   += score_line(l, js_kw,   e8, e8, e8, v4, r4);
            java += score_line(l, java_kw, e8, e8, e8, v4, r4);
            asmx += score_line(l, asm_kw,  e8, e8, e8, v4, r4);
            pas  += score_line(l, pas_kw,  e8, e8, e8, v4, r4);
            bas  += score_line(l, bas_kw,  e8, e8, e8, v4, r4);
            psh  += score_line(l, psh_kw,  e8, e8, e8, v4, r4);
            vbs  += score_line(l, vbs_kw,  e8, e8, e8, v4, r4);
            bat  += score_line(l, bat_kw,  e8, e8, e8, v4, r4);
            bash += score_line(l, bash_kw, e8, e8, e8, v4, r4);
            fort += score_line(l, fort_kw, e8, e8, e8, v4, r4);
            xml  += score_line(l, xml_kw,  e8, e8, e8, v4, r4);
            yaml += score_line(l, yaml_kw, e8, e8, e8, v4, r4);
            json += score_line(l, json_kw, e8, e8, e8, v4, r4);
        }

        /* Pick the highest-scoring language (minimum score of 3 to commit). */
        int best = 0;
        cfg::Lang lang = cfg::Lang::UNKNOWN;

        auto pick = [&](int score, cfg::Lang l) {
            if (score > best) { best = score; lang = l; }
        };

        pick(cpp,  cfg::Lang::CPP);        pick(js,   cfg::Lang::JS);
        pick(java, cfg::Lang::JAVA);        pick(asmx, cfg::Lang::ASM);
        pick(pas,  cfg::Lang::PASCAL_LANG);      pick(bas,  cfg::Lang::BASIC);
        pick(psh,  cfg::Lang::POWERSHELL);  pick(vbs,  cfg::Lang::VBSCRIPT);
        pick(bat,  cfg::Lang::BATCH);       pick(bash, cfg::Lang::BASH);
        pick(fort, cfg::Lang::FORTRAN);     pick(xml,  cfg::Lang::XML);
        pick(yaml, cfg::Lang::YAML);        pick(json, cfg::Lang::JSON);

        return (best >= 3) ? lang : cfg::Lang::UNKNOWN;
    }

}; /* class SyntaxHighlighter */

/* 
 *  SECTION 3 - EdlinEditor
 * 
 */

class EdlinEditor {
    std::vector<std::string> text_buffer;
    int current_page = 0;
    std::string current_filename;
    cfg::Lang current_lang = cfg::Lang::UNKNOWN;

    const char* buf(int i) const { return text_buffer[static_cast<size_t>(i)].c_str(); }

    static void sanitize_ascii(char* str) {
        if (!str) return;
        char* p = str;
        while (*str) {
            if (static_cast<unsigned char>(*str) < 128) {
                *p++ = *str;
            }
            str++;
        }
        *p = '\0';
    }

    int get_int_prompt(const char *prompt) {
        char input[4096];
        std::cout << prompt;
        if (!std::fgets(input, sizeof(input), stdin)) return 0;
        sanitize_ascii(input);
        return std::atoi(input);
    }

    void get_string_prompt(const char *prompt, char *buffer) {
        char input[4096];
        std::cout << prompt;
        if (std::fgets(input, sizeof(input), stdin)) {
            sanitize_ascii(input);
            input[std::strcspn(input, "\n")] = '\0';
            std::strncpy(buffer, input, 4095);
            buffer[4095] = '\0';
        } else {
            buffer[0] = '\0';
        }
    }

    void print_line(int i) const {
        int width = 1;
        int n = static_cast<int>(text_buffer.size());
        while (n >= 10) { n /= 10; ++width; }
        const int line_no = i + 1;
        int digits = 1;
        n = line_no;
        while (n >= 10) { n /= 10; ++digits; }
        for (int pad = width - digits; pad > 0; --pad) std::cout << ' ';
        std::cout << line_no << ": ";
        SyntaxHighlighter::print_highlighted(buf(i), current_lang);
    }

    bool bad_range(int start, int end) const {
        if (start < 0 || end >= (int)text_buffer.size() || start > end) {
            std::cout << "Error: Invalid range.\n";
            return true;
        }
        return false;
    }

public:
    void display_help() const {
        std::cout << "\nedlin - Portable Line Editor (Version " << EDLIN_VERSION << ")\n";
        std::cout << "Buffer limits:  Lines: Dynamic,  Line Length: Dynamic,  Page Length: Dynamic\n";
        std::cout << "Available Commands:\n";
        std::cout << "  [line] - Edit a specific line (enter line number)\n";
        std::cout << "  a     - Append lines from disk into memory\n";
        std::cout << "  c     - Copy lines\n";
        std::cout << "  d     - Delete line(s)\n";
        std::cout << "  e     - End editing (Save and Exit)\n";
        std::cout << "  h / ? - Display this help screen\n";
        std::cout << "  i     - Insert line(s)\n";
        std::cout << "  l     - List line(s)\n";
        std::cout << "  m     - Move line(s)\n";
        std::cout << "  p     - Page display\n";
        std::cout << "  q     - Quit (without saving)\n";
        std::cout << "  r     - Replace text\n";
        std::cout << "  s     - Search text\n";
        std::cout << "  t     - Transfer file\n";
        std::cout << "  w     - Write lines to disk\n";
        std::cout << "--------------------------------------------------------\n";
    }

    void load_file(const char *filename) {
        text_buffer.clear();
        current_filename = filename;
        FILE *f = std::fopen(filename, "r");
        if (!f) {
            std::cout << "New file.\n";
            return;
        }
        char input[4096];
        while (std::fgets(input, sizeof(input), f)) {
            sanitize_ascii(input);
            size_t len = std::strlen(input);
            if (len && input[len - 1] == '\n') input[len - 1] = '\0';
            text_buffer.emplace_back(input);
        }
        std::fclose(f);
        current_lang = SyntaxHighlighter::detect_language(current_filename.c_str(), text_buffer);
    }

    void save_file() const {
        if (current_filename.empty()) {
            std::cout << "No filename specified.\n";
            return;
        }
        FILE *f = std::fopen(current_filename.c_str(), "w");
        if (!f) {
            std::cout << "Error writing to file.\n";
            return;
        }
        for (const auto& line : text_buffer) {
            std::fprintf(f, "%s\n", line.c_str());
        }
        std::fclose(f);
        std::cout << "Saved " << current_filename << "\n";
    }

    void list_lines(int start, int end) const {
        if (text_buffer.empty()) { std::cout << "(empty)\n"; return; }
        if (bad_range(start, end)) return;
        for (int i = start; i <= end; ++i) {
            print_line(i);
        }
    }

    void insert_lines() {
        char input[4096];
        while (true) {
            std::cout << (text_buffer.size() + 1) << ":*";
            if (!std::fgets(input, sizeof(input), stdin)) break;
            sanitize_ascii(input);
            input[std::strcspn(input, "\n")] = '\0';
            if (std::strcmp(input, ".") == 0) break;
            text_buffer.emplace_back(input);
        }
        current_lang = SyntaxHighlighter::detect_language(current_filename.c_str(), text_buffer);
    }

    void edit_line(int idx) {
        if (text_buffer.empty()) {
            std::cout << "(empty)\n";
            return;
        }
        if (idx < 0 || idx >= (int)text_buffer.size()) {
            std::cout << "Error: Invalid line number.\n";
            return;
        }
        print_line(idx);
        std::cout << (idx + 1) << ":*";
        char input[4096];
        if (std::fgets(input, sizeof(input), stdin)) {
            sanitize_ascii(input);
            input[std::strcspn(input, "\n")] = '\0';
            if (std::strlen(input) > 0) {
                text_buffer[static_cast<size_t>(idx)] = input;
                current_lang = SyntaxHighlighter::detect_language(current_filename.c_str(), text_buffer);
            }
        }
    }

    void delete_line(int idx) {
        if (idx < 0 || idx >= (int)text_buffer.size()) {
            std::cout << "Error: Invalid line number.\n";
            return;
        }
        text_buffer.erase(text_buffer.begin() + idx);
    }

    void delete_lines(int start, int end) {
        if (bad_range(start, end)) return;
        text_buffer.erase(text_buffer.begin() + start, text_buffer.begin() + end + 1);
    }

    void page_display() {
        if (text_buffer.empty()) {
            std::cout << "(empty)\n";
            return;
        }
        int page_len = cfg::get_terminal_height() - 2;
        if (current_page >= (int)text_buffer.size()) {
            current_page = 0;
        }
        const int end = std::min(current_page + page_len, (int)text_buffer.size());
        for (int i = current_page; i < end; ++i) {
            print_line(i);
        }
        current_page = end;
    }

    void search_text(int start, int end, const char* term) const {
        if (bad_range(start, end)) return;
        const size_t t_len = std::strlen(term);
        if (t_len == 0) return;
        for (int i = start; i <= end; ++i) {
            if (std::strstr(buf(i), term)) {
                print_line(i);
            }
        }
    }

    void replace_text(int start, int end, const char* old_term, const char* new_term) {
        if (bad_range(start, end)) return;
        const size_t o_len = std::strlen(old_term);
        if (o_len == 0) return;
        for (int i = start; i <= end; ++i) {
            size_t pos = text_buffer[static_cast<size_t>(i)].find(old_term);
            if (pos != std::string::npos) {
                text_buffer[static_cast<size_t>(i)].replace(pos, o_len, new_term);
                print_line(i);
            }
        }
    }

    void transfer_file(const char* filename, int dest) {
        if (dest < 0) dest = 0;
        if (dest > (int)text_buffer.size()) dest = static_cast<int>(text_buffer.size());
        FILE* f = std::fopen(filename, "r");
        if (!f) {
            std::cout << "Error reading file.\n";
            return;
        }
        char input[4096];
        std::vector<std::string> temp;
        while (std::fgets(input, sizeof(input), f)) {
            sanitize_ascii(input);
            size_t len = std::strlen(input);
            if (len && input[len - 1] == '\n') input[len - 1] = '\0';
            temp.emplace_back(input);
        }
        std::fclose(f);
        text_buffer.insert(text_buffer.begin() + dest, temp.begin(), temp.end());
    }

    void copy_lines(int start, int end, int dest, int count) {
        if (count <= 0 || text_buffer.empty()) return;
        if (bad_range(start, end)) return;
        
        if (dest < 0) dest = 0;
        if (dest > (int)text_buffer.size()) dest = static_cast<int>(text_buffer.size());
        
        std::vector<std::string> temp(text_buffer.begin() + start, text_buffer.begin() + end + 1);
        for (int i = 0; i < count; ++i) {
            text_buffer.insert(text_buffer.begin() + dest, temp.begin(), temp.end());
            dest += static_cast<int>(temp.size());
        }
    }

    void move_lines(int start, int end, int dest) {
        if (bad_range(start, end)) return;
        if (dest < 0) dest = 0;
        if (dest > (int)text_buffer.size()) dest = static_cast<int>(text_buffer.size());
        
        if (dest >= start && dest <= end + 1) {
            std::cout << "Error: Destination within range.\n";
            return;
        }
        
        std::vector<std::string> temp(text_buffer.begin() + start, text_buffer.begin() + end + 1);
        text_buffer.erase(text_buffer.begin() + start, text_buffer.begin() + end + 1);
        
        if (dest > start) dest -= (end - start + 1);
        text_buffer.insert(text_buffer.begin() + dest, temp.begin(), temp.end());
    }

    void write_lines(int count) {
        if (count <= 0 || text_buffer.empty()) return;
        if (current_filename.empty()) {
            std::cout << "No filename specified.\n";
            return;
        }
        if (count > (int)text_buffer.size()) count = static_cast<int>(text_buffer.size());
        FILE* f = std::fopen(current_filename.c_str(), "a");
        if (!f) {
            std::cout << "Error appending to file.\n";
            return;
        }
        for (int i = 0; i < count; ++i) {
            std::fprintf(f, "%s\n", text_buffer[static_cast<size_t>(i)].c_str());
        }
        std::fclose(f);
        text_buffer.erase(text_buffer.begin(), text_buffer.begin() + count);
    }

    void append_lines() {
        if (current_filename.empty()) {
            std::cout << "No filename specified.\n";
            return;
        }
        FILE* f = std::fopen(current_filename.c_str(), "r");
        if (!f) return;
        
        int skip = static_cast<int>(text_buffer.size());
        char input[4096];
        while (skip > 0 && std::fgets(input, sizeof(input), f)) {
            skip--;
        }
        
        while (std::fgets(input, sizeof(input), f)) {
            sanitize_ascii(input);
            size_t len = std::strlen(input);
            if (len && input[len - 1] == '\n') input[len - 1] = '\0';
            text_buffer.emplace_back(input);
        }
        std::fclose(f);
    }

    void run()
    {
        char cmd[LINE_LENGTH];

        while (true) {
            std::cout << "*";
            if (!std::fgets(cmd, LINE_LENGTH, stdin)) break;
            
            // sanitize_ascii inside loop
            if (cmd[0] == '\0') continue;
            char* p = cmd;
            char* str = cmd;
            while (*str) {
                if (static_cast<unsigned char>(*str) < 128) {
                    *p++ = *str;
                }
                str++;
            }
            *p = '\0';
            
            cmd[std::strcspn(cmd, "\n")] = '\0';

            const unsigned char first =
                static_cast<unsigned char>(cmd[0]);

            if (std::isdigit(first)) {
                edit_line(std::atoi(cmd) - 1);
            } else if (first == 'a' || first == 'A') {
                append_lines();
            } else if (first == 'c' || first == 'C') {
                int dest = get_int_prompt("Destination: ");
                int count = get_int_prompt("Count: ");
                if (count > 0) {
                    int start = get_int_prompt("Start line: ") - 1;
                    int end = get_int_prompt("End line: ") - 1;
                    copy_lines(start, end, dest - 1, count);
                }
            } else if (first == 'd' || first == 'D') {
                int start = get_int_prompt("Start line: ") - 1;
                int end = get_int_prompt("End line: ") - 1;
                delete_lines(start, end);
            } else if (first == 'e' || first == 'E') {
                save_file();
                break;
            } else if (first == 'h' || first == 'H' || first == '?') {
                display_help();
            } else if (first == 'i' || first == 'I') {
                insert_lines();
            } else if (first == 'l' || first == 'L') {
                int start = get_int_prompt("Start line: ") - 1;
                int end = get_int_prompt("End line: ") - 1;
                list_lines(start, end);
            } else if (first == 'm' || first == 'M') {
                int start = get_int_prompt("Start line: ") - 1;
                int end = get_int_prompt("End line: ") - 1;
                int dest = get_int_prompt("Destination: ");
                move_lines(start, end, dest - 1);
            } else if (first == 'p' || first == 'P') {
                page_display();
            } else if (first == 'q' || first == 'Q') {
                break;
            } else if (first == 'r' || first == 'R') {
                int start = get_int_prompt("Start line: ") - 1;
                int end = get_int_prompt("End line: ") - 1;
                char old_t[LINE_LENGTH]; get_string_prompt("Old text: ", old_t);
                char new_t[LINE_LENGTH]; get_string_prompt("New text: ", new_t);
                replace_text(start, end, old_t, new_t);
            } else if (first == 's' || first == 'S') {
                int start = get_int_prompt("Start line: ") - 1;
                int end = get_int_prompt("End line: ") - 1;
                char old_t[LINE_LENGTH]; get_string_prompt("Search text: ", old_t);
                search_text(start, end, old_t);
            } else if (first == 't' || first == 'T') {
                char fn[LINE_LENGTH]; get_string_prompt("Filename: ", fn);
                int dest = get_int_prompt("Destination: ");
                transfer_file(fn, dest - 1);
            } else if (first == 'w' || first == 'W') {
                int count = get_int_prompt("Number of lines: ");
                write_lines(count);
            } else if (std::strlen(cmd) > 0) {
                std::cout << "Entry error (type '?' for help)\n";
            }
        }
    }
}; /* class EdlinEditor */



/* 
 *  SECTION 4 - CLI Flag Handlers
 * 
 */

static void print_usage()
{
    std::cout <<
        "Usage: edlin [--no-color] [--help] [--version] [--about] [--license]"
        " <filename>\n"
        "Options:\n"
        "  --no-color  Disable syntax highlighting (dumb terminal mode).\n"
        "  --help      Display this help message.\n"
        "  --version   Output current version.\n"
        "  --about     Describe the application's purpose.\n"
        "  --license   Display the full MIT License text.\n";
}

static void print_about()
{
    std::cout <<
        "edlin is a portable, classic-style line editor with optional\n"
        "syntax highlighting for many languages.\n";
}

static void print_license()
{
    std::cout <<
        "MIT License\n\n"
        "Copyright (c) 2026 BASIC++ Community\n\n"
        "Permission is hereby granted, free of charge, to any person obtaining"
        " a copy of this software and associated documentation files (the"
        " \"Software\"), to deal in the Software without restriction, including"
        " without limitation the rights to use, copy, modify, merge, publish,"
        " distribute, sublicense, and/or sell copies of the Software, and to"
        " permit persons to whom the Software is furnished to do so, subject to"
        " the following conditions:\n\n"
        "The above copyright notice and this permission notice shall be included"
        " in all copies or substantial portions of the Software.\n\n"
        "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND,"
        " EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF"
        " MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT."
        " IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY"
        " CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,"
        " TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE"
        " SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.\n";
}

/* 
 *  SECTION 5 - Entry Point
 * 
 */

int main(int argc, char *argv[])
{
    const char *filename = nullptr;

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (std::strcmp(arg, "--no-color") == 0) {
            cfg::force_no_color = true;
        } else if (std::strcmp(arg, "--help") == 0) {
            print_usage();
            return 0;
        } else if (std::strcmp(arg, "--version") == 0) {
            std::cout << "edlin version " << EDLIN_VERSION << "\n";
            return 0;
        } else if (std::strcmp(arg, "--about") == 0) {
            print_about();
            return 0;
        } else if (std::strcmp(arg, "--license") == 0) {
            print_license();
            return 0;
        } else if (arg[0] == '-') {
            std::cout << "Unknown option: " << arg << "\n";
            print_usage();
            return 1;
        } else {
            if (filename) {
                std::cout << "Error: Multiple filenames provided.\n";
                print_usage();
                return 1;
            }
            filename = arg;
        }
    }

    if (!filename) {
        std::cout << "Error: No file specified.\n";
        print_usage();
        return 1;
    }

    EdlinEditor editor;
    editor.load_file(filename);
    std::cout << "Type '?' or 'h' for a list of available commands.\n";
    editor.run();
    return 0;
}
