#include "OsuParser.h"
#include "Beatmap.h"

#include <cstdlib> // abs
#include <regex>
#include <algorithm>  // find_if...
#include <unordered_map>
#include <set>

#include "common.hpp"
#include "util/xstring.hpp"


using namespace std;
using namespace NaiSe;
using namespace std::string_literals;
using IndexDict = unordered_map<string, pair<int, size_t>>;

namespace{

struct Tags
{
    inline static const std::string Osu_Setting{"[General]"};
    inline static const std::string Osu_Media{"[Metadata]"};
    inline static const std::string Osu_Complexity{"[Difficulty]"};
    inline static const std::string Osu_Trigger{"[Events]"};
    inline static const std::string Osu_Timing{"[TimingPoints]"};
    inline static const std::string Osu_Target{"[HitObjects]"};
    //static const uint8_t Osu_Tag_Count{6};
};

struct Properties
{
    static constexpr auto const Osu_iLeadIn{"AudioLeadIn"};
    static constexpr auto const Osu_iApproachTime{"ApproachRate"};
    static constexpr auto const Osu_iBaseWidth{"CircleSize"};
    static constexpr auto const Osu_iMode{"Mode"};
    static constexpr auto const Osu_fStage{"OverallDifficulty"};
    static constexpr auto const Osu_iSubgridSize{"GridSize"};

    static constexpr auto const Osu_sMediaName{"AudioFilename"};
    static constexpr auto const Osu_sArtist{"Artist"};
    static constexpr auto const Osu_sTitle{"Title"};
    static constexpr auto const Osu_sAuthor{"Creator"};
    static constexpr auto const Osu_iPreviewStart{"PreviewTime"};
};

enum class TpIndex : uint8_t
{
    timestamp = 0,
    timePerBeat,
    beatsPerSample,
    setId,
    setSubIndex,
    volume,
    inheritance,
    kiaiState,
    _size
};

enum class HoIndex : uint8_t
{
    loc_x = 0,
    loc_y,
    timestamp,
    typeId,
    soundId,
    extra
};

// Using regex to trim whitespace
string str_trim(const string& str)
{
    // removes \r\n\t\f\v at begin and end of string
    return regex_replace(str, regex("(^\\s+)|(\\s+$)"), string(""), regex_constants::match_any | regex_constants::match_not_null);
}

// Using regex to detect (only) osu tags
IndexDict mapTags(const StringSequenceT& rInSeq)
{
    size_t lneNo=0;
    int elNo = 0;
    IndexDict dic;
    smatch match;
    regex pattern("^\\s*\\[\\w+\\]");  // first alphanumeric characters enclosed in brackets

    for (auto it=rInSeq.Begin; it!=rInSeq.End; ++it)
    {
        if (regex_search(*it, match, pattern))
        {
            dic[match.str()] = make_pair(elNo, lneNo);
            ++elNo;
        }
        ++lneNo;
    }
    return dic;
}

auto getMappedPair(const IndexDict& map, const string& key)
{
    auto kvpIt = map.find(key);
    if (map.end() != kvpIt)
    {
        return kvpIt->second;
    }
    return pair<int, size_t>(-1, 0);  // no match
}

size_t getNextMappedLine(const IndexDict& map, int n)
{
    assert(!map.empty());
    if (n<0)
        n = 0;
    /*return (++n < map.size()) ?
        next(map.cbegin(), n)->second.second :
        SIZE_MAX;  // last mapped line*/
    if (++n < map.size())
    {
        for (auto&& kvp : map)
        {// keyValuePair.Value.MapIndex | LineIndex
            if (n == kvp.second.first)
                return kvp.second.second;
        }
    }
    return SIZE_MAX;  // no match or last line
}

string getStrAttribute(const StringSequenceT& rInSrc, const char* property)
{
    auto lneIt = find_if(rInSrc.Begin, rInSrc.End, [property](const string& str) {
        return string::npos != str.find(property);
    });
    if (rInSrc.End != lneIt)  // if no match, last is returned
    {
        auto offs = (*lneIt).find(':') + 1;
        if (offs < (*lneIt).length())
        {
            return str_trim((*lneIt).substr(offs));
        }

    }
    return "0"s;
}

float commonUnit(float a, float b)
{
    if (a == 0) return abs(b);
    if (b == 0) return abs(a);
    do {
        if (b > a)
            swap(a, b);
    } while ((b>1.f) && (abs(a -= b) > FLT_EPSILON));
    return abs(b);
}

float evaluateTiming(const vector<EventT>& events)
{
    set<float> periods;
    for (auto it=events.cbegin(); it!=events.cend(); ++it)
    {
        if (EventType_t::shift == it->EventType)
            periods.insert(it->Value);
    }
    assert(!periods.empty()); // must have at least one
    float val = 0;
    if (!periods.empty())
        val = *periods.cbegin();
    for (auto it= ++periods.cbegin(); it!=periods.cend(); ++it)
    {
        val = commonUnit(val, *it);
    }
    if (val < 200.f)// shortest possible beat duration (300bpm)
    {
        val = find_if( events.cbegin(), events.cend(), [ ](EventT evi) {
            return evi.EventType == EventType_t::shift;
        })->Value;  //return first beat duration
    }
    return val;
}

}// anonymous namespace


bool COsuParser::tryParse(const CBeatmap& rIn, BeatSetT& rOut) const
{// rIn must remain unchanged for the duration of the call!
    if (GameTypes_t::osu != rIn.getGameType() || !rIn.isValid())
    {// Understands only osu beatmap
        return false;
    }
    
    auto seq = rIn.getSequence();
    if (seq.isEmpty())
    {
        return false;
    }

    const auto dic = mapTags(seq);
    pair<int, size_t> idxPair;  // first: element iteration by order of read-in; second: file line number
    bool pass = true;

    // General
    rOut.Game = GameTypes_t::osu;
    try
    {
        rOut.Setting.SubgridSize = stoi(getStrAttribute(seq, Properties::Osu_iSubgridSize));
    } catch (exception ex){ rOut.Setting.SubgridSize = 8; }
    if (0 <= (idxPair = getMappedPair(dic, Tags::Osu_Setting)).first)
    {
        StringSequenceT subSeq = seq.make_subsequence(idxPair.second, getNextMappedLine(dic, idxPair.first));
        if (subSeq.Distance)
        {
            rOut.Setting.MapName = rIn.getFilename();
            rOut.Media.Filename = getStrAttribute(subSeq, Properties::Osu_sMediaName);
            try
            {
                rOut.Media.PreviewStart_ms = stoi(getStrAttribute(subSeq, Properties::Osu_iPreviewStart));
                rOut.Setting.LeadIn_ms = stoi(getStrAttribute(subSeq, Properties::Osu_iLeadIn));
                rOut.Setting.Mode = (stoi(getStrAttribute(subSeq, Properties::Osu_iMode)) == 3) ?
                    GameMode_t::os_mania : GameMode_t::undefined;
            } catch (exception ex) { pass = false; }

            pass &=
                !(xstring::isEmptyOrWhitespace(&(rOut.Media.Filename)) ||
                (rOut.Setting.Mode == GameMode_t::undefined) ||
                    xstring::isEmptyOrWhitespace(&(rOut.Setting.MapName)));
        }
    }
    
    // Metadata
    if (0 <= (idxPair = getMappedPair(dic, Tags::Osu_Media)).first)
    {
        StringSequenceT subSeq = seq.make_subsequence(idxPair.second, getNextMappedLine(dic, idxPair.first));
        if (subSeq.Distance)
        {
            rOut.Media.Title = getStrAttribute(subSeq, Properties::Osu_sTitle);
            rOut.Media.Artist = getStrAttribute(subSeq, Properties::Osu_sArtist);
            rOut.Media.Author = getStrAttribute(subSeq, Properties::Osu_sAuthor);
        }
    }
    
    // TimingPoints
    if (0 <= (idxPair = getMappedPair(dic, Tags::Osu_Timing)).first)
    {
        if (assignFromSequence(
            seq.make_subsequence(idxPair.second, getNextMappedLine(dic, idxPair.first)),
            rOut.Events))
        {
            rOut.Media.AverageRate_bpm = 60000.f / evaluateTiming(rOut.Events);
        } else {
            pass = false;  // missing bpm
        }
    }// valid pair

    // HitObjects
    if (0 <= (idxPair = getMappedPair(dic, Tags::Osu_Target)).first)
    {
        pass &= assignFromSequence(
            seq.make_subsequence(idxPair.second, getNextMappedLine(dic, idxPair.first)),
            rOut.Targets
        );
    }// valid range
    return pass;
}


/*
// Deprecated: can not distinguish between duplicate property names. Some properties are in "Metadata"
bool COsuParser::assignFromSeqence(const NaiSe::StringSequenceT& rInSeq, SettingT& rOut) const
{
    if (!rInSeq.Distance)
    {
        return false;
    }
    //_pOut->MapName;
    try
    {
        rOut.LeadIn_ms = stoi(getStrAttribute(rInSeq, Properties::Osu_iLeadIn));
        //rOut.ApproachTime_ms = stoi(getStrAttribute(rInSeq, Properties::Osu_iApproachTime));
        switch (stoi(getStrAttribute(rInSeq, Properties::Osu_iMode)))
        {
        case 3:
            rOut.Mode = GameMode_t::os_mania;
            break;
        default:
            rOut.Mode = GameMode_t::undefined;
            break;
        }
    } catch (exception ex) {
        return false;
    }
    //_pOut->Stage  [TODO]: assign from version, overalldifficulty or calculate new
    return rOut.Mode != GameMode_t::undefined;
}


//deprecated: can not distinguish between duplicate property names. Some properties are in "General".
bool COsuParser::assignFromSeqence(const NaiSe::StringSequenceT& rInSeq, NaiSe::MediaInfoT& rOut) const
{
    if (!rInSeq.Distance)
    {
        return false;
    }
    rOut.Filename = getStrAttribute(rInSeq, Properties::Osu_sMediaName);
    try
    {
        rOut.PreviewStart_ms = stoi(getStrAttribute(rInSeq, Properties::Osu_iPreviewStart));
    } catch (exception ex) {
        rOut.PreviewStart_ms = 0u;
    }
    rOut.Title = getStrAttribute(rInSeq, Properties::Osu_sTitle);
    rOut.Artist = getStrAttribute(rInSeq, Properties::Osu_sArtist);
    rOut.Author = getStrAttribute(rInSeq, Properties::Osu_sAuthor);

    return !xstring::isEmptyOrWhitespace(&(rOut.Filename));
}
*/


// events
bool COsuParser::assignFromSequence(const NaiSe::StringSequenceT& rInSeq, vector<EventT>& rOut) const
{
    if (!rInSeq.Distance)
    {
        return false;
    }

    vector<string> args;
    EventT ev;
    float baseVal=1.f;
    float lastVal = baseVal;
    bool state = false;
    int iEvent;

    assert(rInSeq.Distance <= rOut.max_size());
    rOut.reserve(rInSeq.Distance);

    // optional: check kind of header - Events OR TimingPoints
    for (auto it=rInSeq.Begin+1; it!=rInSeq.End; ++it)  // skip header
    {
        // assuming no commentary is found here
        if (xstring::trySplit(*it, args, ','))
        {
            assert((uint8_t)TpIndex::_size <= args.size());
            try
            {
                ev.Timestamp = stof(args[(uint8_t)TpIndex::timestamp]);
                ev.Value = stof(args[(uint8_t)TpIndex::timePerBeat]);
            } catch (exception ex) { continue; }
            if (ev.Value < 0)
            {
                ev.Value = abs(ev.Value) / 100.f * baseVal;
            } else {
                baseVal = ev.Value;
            }

            try
            {
                iEvent = stoi(args[(uint8_t)TpIndex::kiaiState]);
            } catch (exception ex) { iEvent = 0; }
            if (!state && (bool)iEvent)  // on rising
            {
                ev.EventType = EventType_t::kiai;
                state = true;
            } else if (  // bpm or kiai changed
                (state != (bool)iEvent) ||
                (FLT_EPSILON < abs(ev.Value-lastVal)))
            {
                lastVal = ev.Value;  // always beat duration
                state = (iEvent == (int)EventType_t::kiai);
                ev.EventType = EventType_t::shift;
            } else {
                ev.EventType = EventType_t::ignore;
            }
            
            if (!rOut.empty() && ev.Timestamp == rOut.back().Timestamp)
            {
                if (ev.EventType == EventType_t::ignore)
                    continue;

                rOut.back() = ev;  // re-evaluated and overwritten by order of read-in
            } else {
                rOut.push_back(ev);  // is not cleared before appending;
            }
            assert(rOut.empty() ? ev.Timestamp >= 0 : ev.Timestamp >= rOut.back().Timestamp);  // timestamps must appear sorted ascendingly
            
        }// split
    }// loop lines
    // has at least one beat duration over 1ms
    return !rOut.empty() && (rOut.front().Value > 1);  // can be external contents
}


//targets
bool COsuParser::assignFromSequence(const NaiSe::StringSequenceT& rInSeq, vector<NaiSe::EntityT>& rOut) const
{
    if (!rInSeq.Distance)
    {
        return false;
    }

    vector<string> args;
    EntityT obj;

    assert(rInSeq.Distance <= rOut.max_size());
    rOut.reserve(rInSeq.Distance);

    for (auto it=rInSeq.Begin+1; it!=rInSeq.End; ++it)  // skip header
    {
        // assuming no commentary is found here
        if (xstring::trySplit(*it, args, ','))
        {
            if (any_of(args.cbegin(), args.cend() - 1,
                [](string sx) { return xstring::contains(sx, "-"); }))  // no negative values, excludig extra part
            {
                continue;
            }
            assert(UINT8_MAX >= stoi(args[(uint8_t)HoIndex::typeId]));  // within expected range
            try
            {
                obj.Location.first = min(Os_Map_Width, (uint16_t)stoi(args[(uint8_t)HoIndex::loc_x]));
                obj.Location.second = min(Os_Map_Height, (uint16_t)stoi(args[(uint8_t)HoIndex::loc_y]));
                obj.SpawnTime = stof(args[(uint8_t)HoIndex::timestamp]);  // may repeat
                obj.Type.RawType = (uint8_t)stoi(args[(uint8_t)HoIndex::typeId]);  // trimmed if over 255
            } catch (exception e) { continue; }
            
            if (obj.Type.OsuType.IsContinous)
            {// try get hold-duration
                if(xstring::trySplit(string(args.back()), args, ':'))  // args is initially cleared on call. does not work inplace if rIn references a element of args
                {
                    try
                    {
                        obj.Value = stof(args.front());
                    } catch (exception ex) {
                        obj.Type.OsuType.IsContinous = false;
                        obj.Value = 0.f;
                    }
                } else {
                    obj.Type.OsuType.IsContinous = false;
                }
            }

            if (!obj.Type.OsuType.IsComboStart && !(obj.Type.OsuType.IsCircle ^ obj.Type.OsuType.IsSlider ^ obj.Type.OsuType.IsSpin ^ obj.Type.OsuType.IsContinous))
                continue;  // is not: exactly one of a kind or combo start

            rOut.push_back(obj);
        }// split
    }// loop lines
    return rOut.size();
}