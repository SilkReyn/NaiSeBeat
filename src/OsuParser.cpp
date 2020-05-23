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

enum class TimingIndex_t : uint8_t
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

enum class HitIndex : uint8_t
{
    loc_x = 0,
    loc_y,
    timestamp,
    typeId,
    soundId,
    attrib
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
    return string{};
}

template<typename T>
T getAttribute_(const StringSequenceT& rInSrc, const char* property, T nullValue) noexcept { return nullValue; }

template<>
string getAttribute_<string>(const StringSequenceT& rInSrc, const char* property, string nullValue)  noexcept
{
    string str = getStrAttribute(rInSrc, property);
    return (str.empty() ? nullValue : str);
}

template<>
int getAttribute_<int>(const StringSequenceT& rInSrc, const char* property, int nullValue) noexcept
{ 
    try
    {
        return stoi(getStrAttribute(rInSrc, property));
    } catch (exception ex) {
        return nullValue;
    }
}

template<>
float getAttribute_<float>(const StringSequenceT& rInSrc, const char* property, float nullValue) noexcept
{ 
    try
    {
        return stof(getStrAttribute(rInSrc, property));
    } catch (exception ex) {
        return nullValue;
    }
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


 // events
bool assignFromSequence(const StringSequenceT& rInSeq, vector<EventT>& rOut)
{
    if (!rInSeq.Distance)
    {
        return false;
    }

    xvec<TimingIndex_t, string> args;
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
            assert((uint8_t)TimingIndex_t::_size <= args.size());
            try
            {
                ev.Timestamp = stof(args[TimingIndex_t::timestamp]);
                ev.Value = stof(args[TimingIndex_t::timePerBeat]);
            } catch (exception ex) { continue; }
            if (ev.Value < 0)
            {
                ev.Value = abs(ev.Value) / 100.f * baseVal;
            } else {
                baseVal = ev.Value;
            }

            try
            {
                iEvent = stoi(args[TimingIndex_t::kiaiState]);
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
bool assignFromSequence(const StringSequenceT& rInSeq, vector<EntityT>& rOut)
{
    if (!rInSeq.Distance)
    {
        return false;
    }

    xvec<HitIndex, string> args;
    EntityT obj;

    assert(rInSeq.Distance <= rOut.max_size());
    rOut.reserve(rInSeq.Distance);

    for (auto it=rInSeq.Begin+1; it!=rInSeq.End; ++it)  // skip header
    {
        // assuming no commentary is found here
        if (xstring::trySplit(*it, args, ','))
        {
            if (any_of(args.cbegin(), args.cend() - 1,
                [](string sx) { return xstring::contains(sx, "-"); }))  // no negative values, excludig attributes part
            {
                continue;
            }
            assert(UINT8_MAX >= stoi(args[HitIndex::typeId]));  // within expected range
            try
            {
                obj.Location.first = min(Os_Map_Width, (uint16_t)stoi(args[HitIndex::loc_x]));
                obj.Location.second = min(Os_Map_Height, (uint16_t)stoi(args[HitIndex::loc_y]));
                obj.SpawnTime = stof(args[HitIndex::timestamp]);  // may repeat
                obj.Type.RawType = (uint8_t)(0xFF & stoi(args[HitIndex::typeId]));  // trimmed if over 255
            } catch (exception e) { continue; }

            if (obj.Type.OsuType.IsContinous)
            {// try get hold-duration
                if(xstring::trySplit(string(args.back()), args, ':'))  // args is initially cleared on call. does not work inplace if rIn references a element of args
                {
                    try
                    {
                        obj.Value = stof(args.front());  // end of hold timestamp
                    } catch (exception ex) {
                        obj.Type.OsuType.IsContinous = false;
                        obj.Value = 0.f;
                    }
                } else {
                    obj.Type.OsuType.IsContinous = false;
                }
            } else if (obj.Type.OsuType.IsSlider) {
                if (args.size() > 7)
                {
                    try
                    {
                        // slider size in pixel: repetitions * lenght
                        obj.Value = stof(args[enum_cast(HitIndex::attrib) + 1]) * stof(args[enum_cast(HitIndex::attrib) + 2]);
                    }
                    catch (exception e) {
                        obj.Type.OsuType.IsSlider = false;
                        obj.Value = 0;
                    }
                }else {
                    continue;
                }
            }else if (obj.Type.OsuType.IsSpin) {
                try
                {
                    obj.Value = stof(args[HitIndex::attrib]);  // end of spin timestamp
                }
                catch (exception e) {
                    obj.Type.OsuType.IsSpin = false;
                    obj.Value = 0;
                }
            } else {
                try
                {
                    assert(UINT8_MAX >= stoi(args[HitIndex::soundId]));  // within expected range
                    obj.Value = (float)(0xFF & stoi(args[HitIndex::soundId]));  // hit sound id
                } catch (exception e) { obj.Value = 0; }
            }

            if (!obj.Type.OsuType.IsComboStart && !(obj.Type.OsuType.IsCircle ^ obj.Type.OsuType.IsSlider ^ obj.Type.OsuType.IsSpin ^ obj.Type.OsuType.IsContinous))
                continue;  // is not: exactly one of a kind or combo start

            rOut.push_back(obj);
        }// split
    }// loop lines
    return rOut.size();
}


bool COsuParser::tryParse(const CBeatmap& rIn, BeatSetT& rOut)
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
    rOut.Setting.SubgridSize = getAttribute_<int>(seq, Properties::Osu_iSubgridSize, 8);
    if (0 <= (idxPair = getMappedPair(dic, Tags::Osu_Setting)).first)
    {
        StringSequenceT subSeq = seq.make_subsequence(idxPair.second, getNextMappedLine(dic, idxPair.first));
        if (subSeq.Distance)
        {
            rOut.Setting.MapName = rIn.getFilename();
            rOut.Media.Filename = getStrAttribute(subSeq, Properties::Osu_sMediaName);
            rOut.Media.PreviewStart_ms = getAttribute_<int>(subSeq, Properties::Osu_iPreviewStart, 0);
            rOut.Setting.LeadIn_ms = getAttribute_<int>(subSeq, Properties::Osu_iLeadIn, 0);
            switch (getAttribute_<int>(subSeq, Properties::Osu_iMode, -1))
            {
            case 3:
                rOut.Setting.Mode = GameMode_t::os_mania;
                break;

            case 1:
                rOut.Setting.Mode = GameMode_t::os_taiko;
                break;

            default:
                rOut.Setting.Mode = GameMode_t::undefined;
                break;
            }
        }
    }
    pass &=
        !(xstring::isEmptyOrWhitespace(&(rOut.Media.Filename)) ||
        (rOut.Setting.Mode == GameMode_t::undefined) ||
            xstring::isEmptyOrWhitespace(&(rOut.Setting.MapName)));

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

