#pragma once

#include <string>
#include <vector>


namespace xstring {

const auto WHITESPACE = " \n\r\t";

inline bool contains(const std::string& lookInStr, const std::string& lookForStr) noexcept
{
    return std::string::npos != lookInStr.find(lookForStr);
}

inline bool isEmptyOrWhitespace(const std::string* const pStr)
{
    return pStr ? (pStr->empty() || std::string::npos == pStr->find_first_not_of(WHITESPACE)) : true;
}

inline bool trySplit(const std::string& rIn, std::vector<std::string>& _rOut, char delim)
{
    if (rIn.empty())
        return false;
    
    _rOut.clear();

    size_t spos, epos;
    epos = 0;
    do
    {
        spos = rIn.find_first_not_of(delim, epos);  // guarantees epos != spos and handles continuous delimiters
        epos = rIn.find(delim, spos);
        if (spos < rIn.length() && (epos>spos))  // no empty splits
        {
            _rOut.push_back(rIn.substr(spos, epos-spos));  // epos may exceed strlen
            // added element should not be empty, but can be whitespace
        }
    } while (epos < rIn.length()-1);

    return (1 == _rOut.size()) ? false : !_rOut.empty();
}

inline std::string trim(const std::string& str, const char* trimChars  = WHITESPACE)
{
    size_t spos, epos;
    spos = str.find_first_not_of(trimChars);
    epos = str.find_last_not_of(trimChars);
    return ((std::string::npos != spos) &&
    (std::string::npos != epos) &&
    (epos >= spos)) ? str.substr(spos, epos-spos+1) : std::string("");
}

inline void filter(std::string& str, std::string filterChars)
{
    for (auto&& c : str)
    {
        if (std::string::npos != filterChars.find(c))
        {
            c = ' ';
        }
    }
}

inline void erase_any(std::string &str, std::string charsToErase)
{
    for (auto&& c : charsToErase)
    {
        str.erase(remove(str.begin(), str.end(), c), str.end());
    }
}

}// namespace xstring