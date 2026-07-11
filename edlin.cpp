/*
 * edlin.cpp - Portable Line Editor (colored syntax edition)
 * Standalone DOS EDLIN Clone
 * VERSION: 3.0.0
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

static constexpr const char *EDLIN_VERSION = "2.0.0";

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
        const std::array<std::array<char, LINE_LENGTH>, MAX_LINES> &buf,
        int lines)
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

        return detect_by_content(buf, lines);
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
        const std::array<std::array<char, LINE_LENGTH>, MAX_LINES> &buf,
        int lines)
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

        const int limit = std::min(lines, 80);
        for (int i = 0; i < limit; ++i) {
            std::string_view l(buf[static_cast<size_t>(i)].data());
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

    /* -- Data members ------------------------------------------------------- */

    std::array<std::array<char, LINE_LENGTH>, MAX_LINES> text_buffer{};
    int current_lines    = 0;
    int current_page     = 0;
    std::array<char, LINE_LENGTH> current_filename{};
    cfg::Lang current_lang = cfg::Lang::UNKNOWN;

    /* Private helpers */

    /* Accessor that safely casts to size_t to avoid sign-conversion warnings */
    char* buf(int i) { return text_buffer[static_cast<size_t>(i)].data(); }
    const char* buf(int i) const { return text_buffer[static_cast<size_t>(i)].data(); }


    /* Filter to ensure strictly 7-bit ASCII */
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

    /* Safe copy that guarantees NUL-termination */
    static void safe_copy(char* dst, const char* src) {
        std::strncpy(dst, src, LINE_LENGTH - 1);
        dst[LINE_LENGTH - 1] = '\0';
    }

    /* Prompt, read a line, return its integer value (0 on EOF/error). */
    int get_int_prompt(const char *prompt)
    {
        char input[LINE_LENGTH];
        std::cout << prompt;
        if (!std::fgets(input, LINE_LENGTH, stdin)) return 0;
        sanitize_ascii(input);
        return std::atoi(input);
    }

    /* Prompt, read a line into buffer, strip trailing newline. */
    void get_string_prompt(const char *prompt, char *buffer)
    {
        
        
        std::cout << prompt;
        if (std::fgets(buffer, LINE_LENGTH, stdin)) {
            sanitize_ascii(buffer);
            
        buffer[std::strcspn(buffer, "\n")] = '\0';
        } else {
            buffer[0] = '\0';
        }
    }

    /*
     * Print one line with its 1-based line number, right-aligned to the
     * width of current_lines, followed by syntax highlighting.
     */
    void print_line(int i) const
    {
        /* Compute digit widths for right-alignment. */
        int width  = 1;
        int n      = current_lines;
        while (n >= 10) { n /= 10; ++width; }

        const int line_no = i + 1;
        int digits = 1;
        n = line_no;
        while (n >= 10) { n /= 10; ++digits; }

        for (int pad = width - digits; pad > 0; --pad) std::cout << ' ';
        std::cout << line_no << ": ";
        SyntaxHighlighter::print_highlighted(buf(i), current_lang);
    }

    /* Range-validation helper */

    /*
     * Returns true and prints an error if [start, end] is not a valid
     * sub-range of [0, current_lines-1].
     */
    bool bad_range(int start, int end) const
    {
        if (start < 0 || end >= current_lines || start > end) {
            std::cout << "Error: Invalid range.\n";
            return true;
        }
        return false;
    }

public:

    /* Public interface command implementations */

    /* 'h' / '?' - Show the full built-in help, matching the edlin.c 1.3.1
     *             command table exactly; also reports compile-time limits. */
    void display_help() const
    {
        std::cout << "\nedlin - Portable Line Editor (Version "
                  << EDLIN_VERSION << ")\n";
        std::cout << "Buffer limits:  Lines: " << MAX_LINES
                  << ",  Line Length: " << LINE_LENGTH
                  << ",  Page Length: Dynamic\n";
        std::cout << "Available Commands:\n";
        std::cout << "  [line] - Edit a specific line (enter line number)\n";
        std::cout << "  a     - Append lines from disk into memory\n";
        std::cout << "  c     - Copy lines\n";
        std::cout << "  d     - Delete line(s)\n";
        std::cout << "  e     - End editing (Save and Exit)\n";
        std::cout << "  h, ?  - Display this built-in help message\n";
        std::cout << "  i     - Insert lines at the end of the file\n";
        std::cout << "  l     - List all lines currently in the memory buffer\n";
        std::cout << "  m     - Move lines\n";
        std::cout << "  p     - Page display\n";
        std::cout << "  q     - Quit the editor immediately without saving changes\n";
        std::cout << "  r     - Replace text\n";
        std::cout << "  s     - Search text\n";
        std::cout << "  t     - Transfer (merge) another file\n";
        std::cout << "  w     - Write lines to disk\n\n";
    }

    /* Load filename into the buffer; create a new buffer if the file is absent. */
    void load_file(const char *filename)
    {
        FILE *f = std::fopen(filename, "r");
        if (f) {
            current_lines = 0;
            while (current_lines < MAX_LINES &&
                   std::fgets(buf(current_lines), LINE_LENGTH, f)) {
                
        
        size_t len = std::strlen(buf(current_lines));
                if (len && text_buffer[static_cast<size_t>(current_lines)][len - 1] == '\n') {
                    text_buffer[static_cast<size_t>(current_lines)][len - 1] = '\0';
                }
                ++current_lines;
            }
            std::fclose(f);
            std::cout << "End of input file\n";
        } else {
            std::cout << "New file\n";
        }

        std::strncpy(current_filename.data(), filename, LINE_LENGTH - 1);
        current_filename[LINE_LENGTH - 1] = '\0';

        current_lang = SyntaxHighlighter::detect_language(
            current_filename.data(), text_buffer, current_lines);
    }

    /* Write the full buffer to disk (overwrites existing content). */
    void save_file()
    {
        FILE *f = std::fopen(current_filename.data(), "w");
        if (!f) {
            std::cout << "Error: Cannot save file.\n";
            return;
        }
        for (int i = 0; i < current_lines; ++i) {
            std::fprintf(f, "%s\n", buf(i));
        }
        std::fclose(f);
    }

    /* 'l' - List all lines with line numbers and syntax highlighting. */
    void list_lines()
    {
        for (int i = 0; i < current_lines; ++i) {
            print_line(i);
        }
    }

    /* 'i' - Append new lines interactively; '.' on its own line to stop. */
    void insert_line()
    {
        char input[LINE_LENGTH];
        while (current_lines < MAX_LINES) {
            std::cout << (current_lines + 1) << ":*";
            if (!std::fgets(input, LINE_LENGTH, stdin)) break;
            sanitize_ascii(input);
            input[std::strcspn(input, "\n")] = '\0';
            if (std::strcmp(input, ".") == 0) break;
            safe_copy(buf(current_lines), input);
            ++current_lines;
        }
    }

    /* 'd' - Delete a single line by 1-based number. */
    void delete_line()
    {
        
        
        if (current_lines == 0) {
            std::cout << "Error: Buffer is empty.\n";
            return;
        }

        const int idx = get_int_prompt("Line to delete: ") - 1;
        if (idx < 0 || idx >= current_lines) {
            std::cout << "Error: Invalid line number.\n";
            return;
        }

        for (int i = idx; i < current_lines - 1; ++i) {
            safe_copy(buf(i), buf(i + 1));
        }
        --current_lines;
        std::cout << "Line deleted.\n";
    }

    /* '[n]' - Edit the line at 0-based index idx (called from main loop). */
    void edit_line(int idx)
    {
        if (idx < 0 || idx >= current_lines) {
            std::cout << "Error: Invalid line number.\n";
            return;
        }

        print_line(idx);
        std::cout << (idx + 1) << ":*";

        char input[LINE_LENGTH];
        if (std::fgets(input, LINE_LENGTH, stdin)) {
            sanitize_ascii(input);
            
        input[std::strcspn(input, "\n")] = '\0';
            if (std::strlen(input) > 0) {
                safe_copy(buf(idx), input);
            }
        }
    }

    /* 'p' - Display the next N lines (dynamic terminal height); wraps to the top when done. */
    void page_display()
    {
        if (current_lines == 0) {
            std::cout << "Buffer is empty.\n";
            return;
        }
        int page_len = cfg::get_terminal_height() - 1;
        if (page_len < 1) page_len = PAGE_LENGTH;

        if (current_page >= current_lines) {
            current_page = 0;
        }
        const int end = std::min(current_page + page_len, current_lines);
        for (int i = current_page; i < end; ++i) {
            print_line(i);
        }
        current_page = end;
    }

    /* 's' - Search a range of lines for a sub-string. */
    void search_text()
    {
        const int start = get_int_prompt("Start line: ") - 1;
        const int end   = get_int_prompt("End line: ")   - 1;

        char search_str[LINE_LENGTH];
        get_string_prompt("Search for: ", search_str);

        if (bad_range(start, end) || std::strlen(search_str) == 0) return;

        int found = 0;
        for (int i = start; i <= end; ++i) {
            if (std::strstr(buf(i), search_str)) {
                print_line(i);
                ++found;
            }
        }
        std::cout << found << " matches found.\n";
    }

    /* 'r' - Replace first occurrence of a sub-string on each matching line. */
    void replace_text()
    {
        const int start = get_int_prompt("Start line: ") - 1;
        const int end   = get_int_prompt("End line: ")   - 1;

        char search_str[LINE_LENGTH];
        char replace_str[LINE_LENGTH];
        get_string_prompt("Search for: ",    search_str);
        get_string_prompt("Replace with: ",  replace_str);

        if (bad_range(start, end) || std::strlen(search_str) == 0) return;

        int replaced = 0;
        for (int i = start; i <= end; ++i) {
            char *pos = std::strstr(buf(i), search_str);
            if (!pos) continue;

            char temp[LINE_LENGTH];
            const int prefix_len = static_cast<int>(pos - buf(i));

            std::strncpy(temp, buf(i), static_cast<size_t>(prefix_len));
            temp[prefix_len] = '\0';
            std::strncat(temp, replace_str,
                         LINE_LENGTH - std::strlen(temp) - 1);
            std::strncat(temp, pos + std::strlen(search_str),
                         LINE_LENGTH - std::strlen(temp) - 1);

            safe_copy(buf(i), temp);
            print_line(i);
            ++replaced;
        }
        std::cout << replaced << " lines updated.\n";
    }

    /* 't' - Transfer (merge) an external file before a given line. */
    void transfer_file()
    {
        int dest = get_int_prompt("Insert before line: ") - 1;
        if (dest < 0)               dest = 0;
        if (dest > current_lines)   dest = current_lines;

        char filename[LINE_LENGTH];
        get_string_prompt("Filename: ", filename);

        FILE *f = std::fopen(filename, "r");
        if (!f) {
            std::cout << "Error: Cannot open " << filename << "\n";
            return;
        }

        char input[LINE_LENGTH];
        while (current_lines < MAX_LINES &&
               std::fgets(input, LINE_LENGTH, f)) {
            
        
        input[std::strcspn(input, "\n")] = '\0';

            /* Shift everything from dest downward to open a slot. */
            for (int i = current_lines; i > dest; --i) {
                safe_copy(buf(i), buf(i - 1));
            }
            safe_copy(buf(dest), input);
            ++current_lines;
            ++dest;
        }
        std::fclose(f);
        std::cout << "File transferred.\n";
    }

    /* 'w' - Write N lines to disk in append mode, then clear them from memory. */
    void write_lines()
    {
        const int count = get_int_prompt("Number of lines to write: ");
        if (count <= 0 || count > current_lines) return;

        FILE *f = std::fopen(current_filename.data(), "a");
        if (!f) {
            std::cout << "Error: Cannot write to file.\n";
            return;
        }
        for (int i = 0; i < count; ++i) {
            std::fprintf(f, "%s\n", buf(i));
        }
        std::fclose(f);

        /* Shift remaining lines to the front. */
        for (int i = count; i < current_lines; ++i) {
            safe_copy(buf(i - count), buf(i));
        }
        current_lines -= count;
        std::cout << count << " lines written to disk and cleared from memory.\n";
    }

    /* 'a' - Append from disk the lines not yet in the memory buffer. */
    void append_lines()
    {
        if (current_lines >= MAX_LINES) {
            std::cout << "Error: Memory buffer is full.\n";
            return;
        }

        FILE *f = std::fopen(current_filename.data(), "r");
        if (!f) {
            std::cout << "Error: Cannot read file.\n";
            return;
        }

        /* Skip over lines already loaded into memory. */
        char input[LINE_LENGTH];
        for (int skip = current_lines;
             skip > 0 && std::fgets(input, LINE_LENGTH, f);
             --skip) {
            /* discard */
        }

        int appended = 0;
        while (current_lines < MAX_LINES &&
               std::fgets(input, LINE_LENGTH, f)) {
            
        
        input[std::strcspn(input, "\n")] = '\0';
            safe_copy(buf(current_lines), input);
            ++current_lines;
            ++appended;
        }
        std::fclose(f);
        std::cout << appended << " lines appended from disk.\n";
    }

    /* 'c' - Copy a range of lines to a destination index. */
    void copy_lines()
    {
        const int start = get_int_prompt("Start line: ")       - 1;
        const int end   = get_int_prompt("End line: ")         - 1;
        const int dest  = get_int_prompt("Destination line: ") - 1;

        if (bad_range(start, end) || dest < 0 || dest > current_lines) return;

        if (dest >= start && dest <= end) {
            std::cout << "Error: Cannot copy into source range.\n";
            return;
        }

        const int count = end - start + 1;
        if (current_lines + count > MAX_LINES) {
            std::cout << "Error: Memory limit reached.\n";
            return;
        }

        /* Open a gap of 'count' slots at dest. */
        for (int i = current_lines - 1; i >= dest; --i) {
            safe_copy(buf(i + count), buf(i));
        }

        /* Fill the gap from the source (which shifted if dest < start). */
        if (dest < start) {
            for (int j = 0; j < count; ++j) {
                safe_copy(buf(dest + j), buf(start + count + j));
            }
        } else {
            for (int j = 0; j < count; ++j) {
                safe_copy(buf(dest + j), buf(start + j));
            }
        }

        current_lines += count;
        std::cout << count << " lines copied.\n";
    }

    /* 'm' - Move a range of lines to a destination index. */
    void move_lines()
    {
        const int start = get_int_prompt("Start line: ")       - 1;
        const int end   = get_int_prompt("End line: ")         - 1;
        const int dest  = get_int_prompt("Destination line: ") - 1;

        if (bad_range(start, end) || dest < 0 || dest > current_lines) return;

        if (dest >= start && dest <= end) {
            std::cout << "Error: Cannot move into source range.\n";
            return;
        }

        const int count = end - start + 1;

        /* Temporarily open a gap at dest (same as copy). */
        for (int i = current_lines - 1; i >= dest; --i) {
            safe_copy(buf(i + count), buf(i));
        }

        /* Copy source into the gap; calculate where originals now live. */
        int del_start = start;
        if (dest < start) {
            for (int j = 0; j < count; ++j) {
                safe_copy(buf(dest + j), buf(start + count + j));
            }
            del_start = start + count;
        } else {
            for (int j = 0; j < count; ++j) {
                safe_copy(buf(dest + j), buf(start + j));
            }
        }

        /* Close the gap left by the original lines. */
        for (int i = del_start; i < current_lines - count; ++i) {
            safe_copy(buf(i), buf(i + count));
        }
        /* Note: total line count is unchanged by a move. */
        std::cout << count << " lines moved.\n";
    }

    /* Main command loop */

    void run()
    {
        char cmd[LINE_LENGTH];

        while (true) {
            std::cout << "*";
            if (!std::fgets(cmd, LINE_LENGTH, stdin)) break;
            sanitize_ascii(cmd);
            cmd[std::strcspn(cmd, "\n")] = '\0';

            const unsigned char first =
                static_cast<unsigned char>(cmd[0]);

            if (std::isdigit(first)) {
                
        
        edit_line(std::atoi(cmd) - 1);
            } else if (first == 'a' || first == 'A') {
                append_lines();
            } else if (first == 'c' || first == 'C') {
                copy_lines();
            } else if (first == 'd' || first == 'D') {
                delete_line();
            } else if (first == 'e' || first == 'E') {
                save_file();
                break;
            } else if (first == 'h' || first == 'H' || first == '?') {
                display_help();
            } else if (first == 'i' || first == 'I') {
                insert_line();
            } else if (first == 'l' || first == 'L') {
                list_lines();
            } else if (first == 'm' || first == 'M') {
                move_lines();
            } else if (first == 'p' || first == 'P') {
                page_display();
            } else if (first == 'q' || first == 'Q') {
                break;
            } else if (first == 'r' || first == 'R') {
                replace_text();
            } else if (first == 's' || first == 'S') {
                search_text();
            } else if (first == 't' || first == 'T') {
                transfer_file();
            } else if (first == 'w' || first == 'W') {
                write_lines();
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
