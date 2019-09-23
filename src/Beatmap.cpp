#include "Beatmap.h"

#include <fstream>


using namespace std;


CBeatmap::CBeatmap(const vector<string>& strLines, NaiSe::GameTypes_t game) :
	mStrLines(strLines),
    mType(game)
{}


CBeatmap::CBeatmap(vector<string>&& strLines, NaiSe::GameTypes_t game) :
	mStrLines(move(strLines)),
    mType(game)
{}


bool CBeatmap::initFromPath(const string& fullpath)
{
	if (fullpath.empty())
	{
		return false;
	}

	ifstream fs(fullpath);
	string strbuff;

	if (!fs.is_open())
	{
		return false;
	}

    mType = (string::npos != fullpath.rfind(".osu")) ? NaiSe::GameTypes_t::osu : NaiSe::GameTypes_t::unknown;
    size_t pos = fullpath.find_last_of("/\\") + 1;
    if (pos < fullpath.length())
    {
        mFilename = fullpath.substr(pos, fullpath.find_last_of('.')-pos);  // excludes file extention
    } else {
        mFilename = fullpath;  // might end with slash
    }
	
	mStrLines.clear();
	while (fs.good() && !fs.eof())
	{
		getline(fs, strbuff);  // note, beatsaber map is one line!
        pos = strbuff.find("//");  // filter commentary
        if (string::npos != pos)
        {
            if (pos > 0)
            {
                mStrLines.emplace_back(strbuff, size_t(0), pos);  // substring ctor
            }
        } else {
		    mStrLines.push_back(move(strbuff));
        }
	}
    assert(fs.eof());  // nothing skipped
    fs.close();

	return !mStrLines.empty();
}


bool CBeatmap::isValid() const
{
    return !mStrLines.empty() && NaiSe::GameTypes_t::unknown != mType && !mFilename.empty();
}


void CBeatmap::writeMap(string name)
{
    if (mStrLines.empty())
        return;

    if (name.empty())
    {
        name = "newBeatmap";
    }

    ofstream fs(name + ((mType == NaiSe::GameTypes_t::beatsaber) ? ".dat" : ".osu"));

    if (!fs.is_open())
        return;

    for (auto lneIt=mStrLines.cbegin(); lneIt!=mStrLines.cend(); ++lneIt)
    {
        fs << *lneIt << '\n';
    }
    fs.flush();
    fs.close();
}


