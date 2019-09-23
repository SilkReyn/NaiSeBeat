#pragma once

#include "common.hpp"


/// File handler and string reader.
class CBeatmap
{
	std::vector<std::string> mStrLines;
    NaiSe::GameTypes_t mType{NaiSe::GameTypes_t::unknown};
	std::string mFilename;

public:
    CBeatmap() = default;
    CBeatmap(const std::vector<std::string>& strLines, NaiSe::GameTypes_t game);
    CBeatmap(std::vector<std::string>&& strLines, NaiSe::GameTypes_t game);

    NaiSe::GameTypes_t getGameType() const { return mType; }
    std::string getFilename() const { return mFilename; }
    NaiSe::StringSequenceT getSequence() const { return { mStrLines.cbegin(), mStrLines.cend(), mStrLines.size() }; }
    

	bool initFromPath(const std::string& fullpath);
    void writeMap(std::string name);  // without extention
    bool isValid() const;
};

