# edlin for Linux
the original Microsoft DOS text editor, edlin, has been ported over to Linux. This should work under all linux distros.


## Architecture and Design
The application logic avoids dynamic allocation where possible, aiming to operate safely within a 512 KB memory limit for reliable execution on constrained and legacy systems.


## Compilation Instructions
**Linux (POSIX) via GCC:**
`cc -ansi -pedantic -Wall -o edlin edlin.c`


## Source Code Comments
/*
 * PROJECT ROADMAP
 * COMPLIANCE STATUS:
 * [MET] 2026-05-13: Full historical edlin command set implemented securely.
 * [MET] 2026-05-13: Zero dynamic memory allocation; strict in-place buffer manipulation ensures < 512KB footprint.
 * [MET] 2026-05-13: Resolved ISO C90 mixed declaration warnings for strict ANSI compatibility.
 * CANDIDATE CRITERIA:
 * 1. Implement regex-based line ranges (e.g., "1,5d") for advanced POSIX environments.
 * 2. Add an optional undo buffer (if memory limits permit on specific target architectures).
 * 3. Integrate automated memory profiling checks directly into the build and deployment script.
 * VERSION: 1.3.1
 * LICENSE: MIT License
 *
 * COMPILATION INSTRUCTIONS:
 * 
 * Linux (POSIX) via GCC:
 * Command: gcc -ansi -pedantic -Wall -o edlin edlin.c
 *
 * Linux (POSIX) via Bruce's C Compiler (BCC):
 * Command: bcc -ansi -o edlin edlin.c
 */
