#pragma once

#include <QString>

namespace pvf::next {

// Result of probing the system for a git binary.  PVForge never asks the user
// to pick a path: it searches PATH like a Minecraft launcher searches Java.
struct GitInfo
{
    bool found = false;
    QString executable; // absolute path, empty when not found
    QString version;    // e.g. "git version 2.43.0"
    QString error;      // human-readable reason when found == false
};

// Runs `git --version` against QStandardPaths::findExecutable("git").
GitInfo detectGit();

} // namespace pvf::next
