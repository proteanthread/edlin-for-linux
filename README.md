[![GitGem](https://gitgem.org/api/badge/github/proteanthread/edlin-for-linux.svg)](https://gitgem.org/github/proteanthread/edlin-for-linux)


Updated 'edlin.c' to 'edlin.cpp' - Version 2.0 (added syntax highlighting)

# BASIC++ text editors

The original Microsoft DOS line-oriented text editor, EDLIN, has been ported to strict ANSI C89 for integration with BASIC++ and compatibility with FreeDOS. The updated version is designed to remain highly portable and will also compile and run on modern Windows and Linux systems. The editor is intended to function across all major Linux distributions with minimal platform-specific dependencies.

Unlike the original MS-DOS version of EDLIN, this port includes a built-in online help system, providing users with accessible command documentation directly within the editor. The project also includes support and compatibility considerations for other classic text editors, including vi, WS (WordStar-style editor), and ed, allowing users to choose the editing environment that best fits their workflow.

The goal of these editors is to provide lightweight, portable, and historically inspired text-editing tools suitable for BASIC++ development, legacy system environments, and modern cross-platform use.

## Architecture and Design

The application logic avoids dynamic allocation where possible, aiming to operate safely within a 512 KB memory limit for reliable execution on constrained and legacy systems.

Should compile cleanly on FreeDOS, Windows, and Linux.
