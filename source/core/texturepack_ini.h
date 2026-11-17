#pragma once

// The one line that switches XIII to an alternative texture pack, kept in a plain header so it can be
// tested outside the fix.
//
// XIII.exe adds "..\TexturesPC\*.utx" to the package search paths itself. The engine searches the Paths
// entries of [Core.System] first and in order, so
//
//     [Core.System]
//     Paths=..\TexturesAI\*.utx        <- first Paths entry
//
// makes every package that exists in <game>\TexturesAI win over its TexturesPC original, and everything
// else falls through to TexturesPC. The originals are never touched. The engine reads the ini before
// the asi is loaded, so a change is next launch's.

#include <windows.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace texturepack
{
    inline constexpr const char* szPathsLine = "Paths=..\\TexturesAI\\*.utx";

    inline std::string Trimmed(const std::string& s)
    {
        const auto a = s.find_first_not_of(" \t\r");
        if (a == std::string::npos)
            return {};

        return s.substr(a, s.find_last_not_of(" \t\r") - a + 1);
    }

    inline bool EqualNoCase(const std::string& a, const std::string& b)
    {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y)
            { return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y)); });
    }

    // Adds (bOn) or removes the line. 1 = file rewritten, 0 = already as wanted, -1 = unreadable, no
    // [Core.System] section, or not writable. Bytes outside the one line are kept as they are, line
    // endings included.
    inline int SetPathsLine(const std::filesystem::path& ini, bool bOn)
    {
        std::ifstream in(ini, std::ios::binary);
        if (!in)
            return -1;

        const std::string sOld((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();

        const std::string sEol = sOld.find("\r\n") != std::string::npos ? "\r\n" : "\n";

        std::vector<std::string> aLines;
        for (size_t nPos = 0; nPos <= sOld.size();)
        {
            const auto nEnd = sOld.find('\n', nPos);
            auto sLine = sOld.substr(nPos, nEnd == std::string::npos ? std::string::npos : nEnd - nPos);

            if (!sLine.empty() && sLine.back() == '\r')
                sLine.pop_back();

            // The piece after a final newline is not a line of its own.
            if (nEnd != std::string::npos || !sLine.empty())
                aLines.push_back(sLine);

            if (nEnd == std::string::npos)
                break;

            nPos = nEnd + 1;
        }

        std::erase_if(aLines, [](const std::string& s) { return EqualNoCase(Trimmed(s), szPathsLine); });

        if (bOn)
        {
            size_t nSection = aLines.size();
            for (size_t i = 0; i < aLines.size(); ++i)
            {
                if (EqualNoCase(Trimmed(aLines[i]), "[Core.System]"))
                {
                    nSection = i;
                    break;
                }
            }

            if (nSection == aLines.size())
                return -1;

            // Ahead of the first Paths entry; a section without one gets it as its last line.
            auto nInsert = nSection + 1;
            for (; nInsert < aLines.size(); ++nInsert)
            {
                const auto s = Trimmed(aLines[nInsert]);
                if (s.rfind("[", 0) == 0 || (s.size() >= 6 && EqualNoCase(s.substr(0, 6), "Paths=")))
                    break;
            }

            aLines.insert(aLines.begin() + static_cast<std::ptrdiff_t>(nInsert), szPathsLine);
        }

        std::string sNew;
        for (const auto& s : aLines)
            sNew += s + sEol;

        // A file that did not end in a newline keeps not ending in one.
        if (!sOld.empty() && sOld.back() != '\n' && sNew.size() >= sEol.size())
            sNew.erase(sNew.size() - sEol.size());

        if (sNew == sOld)
            return 0;

        // Beside it and renamed over, so the game never sees half an ini.
        auto temp = ini;
        temp += L".texpack.tmp";

        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            if (!out || !out.write(sNew.data(), static_cast<std::streamsize>(sNew.size())) || !out.flush())
                return -1;
        }

        if (!MoveFileExW(temp.c_str(), ini.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            std::error_code ec;
            std::filesystem::remove(temp, ec);
            return -1;
        }

        return 1;
    }
}
