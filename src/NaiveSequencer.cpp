#include <filesystem>

#include "NaiveSequencer.h"
#include "Beatmap.h"
#include "OsuParser.h"
#include "BsSequencer.h"

#include "common.hpp"
#include "util/xstring.hpp"


using namespace std;


namespace {
vector<NaiSe::BeatSetT> sMaps{};

bool tryMakeFoldername(
    const string& artist,
    const string& title,
    const string& author,
    string&       rOut)
{
    rOut = " -  ()";
    rOut.insert(5, author);
    rOut.insert(3, title);
    rOut.insert(0, artist);
    xstring::filter(rOut, R"(\/:*?"<>|)");
    if (!xstring::isEmptyOrWhitespace(&rOut))
        return true;

    return false;
}

}// anonymous ns

namespace stdfs = filesystem;
using namespace NaiSe;


BeatSetT CBeatTranslator::loadFile(const char* fullpath) const
{
    CBeatmap file;
    BeatSetT data;
    if (file.initFromPath(string{fullpath}))
    {
        if (COsuParser::tryParse(file, data))
            return data;
    }
    throw runtime_error("NaiveSequencer::loadFile(...) - failed");
    return data;
}


void CBeatTranslator::convertFile(const char* fullpath, uint8_t stage) const
{
    CBsSequencer seq;
    auto data = loadFile(fullpath);
    data.StageLevel = stage;

    switch (data.Game)
    {
    case GameTypes_t::osu:
        //pSeq = make_unique<CBsSequencer>();
        seq.transformBeatset(data);
        break;

    case GameTypes_t::beatsaber:
        // TODO implement internal mode-to-mode translation
        return;
        break;

    default:
        throw logic_error("NaiveSequencer::translateToBsFile(...) - Unsupported game type");
        break;
    }
    
    CBeatmap bsFile(seq.serializeBeatset(data), data.Game);
    string subdir;
    if (tryMakeFoldername(data.Media.Artist, data.Media.Title, data.Media.Author, subdir))
    {
        stdfs::path dir(subdir);  // relative
        if (stdfs::exists(dir) || stdfs::create_directory(dir))
        {
            bsFile.writeMap((dir /= data.Setting.MapName).string());
            return;
        }
    }
    bsFile.writeMap(data.Setting.MapName);
}


bool CBeatTranslator::appendFile(const char* fullpath, Difficulty_t stage)
{
    try
    {
        sMaps.push_back(loadFile(fullpath));
    } catch (exception ex) { return false; }

    switch (stage)
    {
    case Difficulty_t::easy:
        mAvailableStages[0] = true;
        sMaps.back().StageLevel = 1;
        break;

    case Difficulty_t::normal:
        mAvailableStages[1] = true;
        sMaps.back().StageLevel = 3;
        break;

    case Difficulty_t::hard:
        mAvailableStages[2] = true;
        sMaps.back().StageLevel = 5;
        break;

    case Difficulty_t::extra:
        mAvailableStages[3] = true;
        sMaps.back().StageLevel = 7;
        break;

    case Difficulty_t::special:
        mAvailableStages[4] = true;
        sMaps.back().StageLevel = 9;
        break;

    default:
        sMaps.pop_back();
        return false;
    }
    return true;
}


void CBeatTranslator::clear()
{
    sMaps.clear();
    memset(mAvailableStages, false, sizeof(mAvailableStages));
}


void CBeatTranslator::translate()
{
    if (sMaps.empty())
        return;

    CBsSequencer seq;
    stdfs::path root;
    const BeatSetT& cont = sMaps.front();
    string subdir;

    if (tryMakeFoldername(cont.Media.Artist, cont.Media.Title, cont.Media.Author, subdir))
    {
        stdfs::path dir(subdir);  // relative
        if (stdfs::exists(dir) || stdfs::create_directory(dir))
        {
            root = dir;
        }
    }
    
    for (auto&& map : sMaps)
    {
        seq.transformBeatset(map);  // changes map name too
        CBeatmap bsFile(seq.serializeBeatset(map), map.Game);
        bsFile.writeMap((root/map.Setting.MapName).string());  // names are referenced in map info!
    }
    
    vector<string> infostr;
    infostr.emplace_back(
        seq.createMapInfo(
            cont.Media,
            CBsSequencer::BsStageFlagsT{
                mAvailableStages[0], mAvailableStages[1], mAvailableStages[2], mAvailableStages[3], mAvailableStages[4]
            }
    ));
    CBeatmap info(move(infostr), GameTypes_t::beatsaber);
    info.writeMap((root/"Info").string());

    clear();
}

