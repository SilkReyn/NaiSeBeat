#include "BsSequencer.h"

#include <algorithm>  // min max
#include <forward_list>
#include <iterator> // distance
#include <sstream>
//#include <fstream>

#include "common.hpp"

using namespace std;
using namespace NaiSe;


const char* CBsSequencer::STAGE_NAMES[] = {"Easy", "Normal", "Hard", "Expert", "ExpertPlus"};
const char* MODE_NAME_NA = "NoArrows";

namespace {

const uint8_t WALL_VERTICAL = 0u;
const uint8_t WALL_HORIZONTAL = 1u;
const float RING_MOV_TOGG_VAL = 0.f;
const float BLOCK_PLACEMENT_DOWNTIME_MS = 200.f;
const float BLOCK_PLACEMENT_DOWNTIME_NEIGHBOUR_MS = 125.f;


float quantizeTimestamp(float ts, double period, const uint8_t MAX_DENUM=8)
{
    if (ts == 0 || period == 0)
        return 0;

    period = abs(ts / period);
    ts = (float)(period - floor(period));  // fraction, never 1
    period = floor(period);  // whole
    float div;
    for (int i=2; i<=MAX_DENUM; i=i<<1)
    {
        div = 1.f / i;
        if (ts >= div)
        {
            ts -= div;
            period += div;  // add quant
        }
        if (ts < 0.125)
            break;
    }
    return (float)period;
}

float setSpeedByPeriod(float tDelta_ms)
{
    return floor(max(1.f, min(7.f, 1400.f/abs(tDelta_ms))));
}

void ss_appendEvent(stringstream& rOut, const NaiSe::EventT& ev)
{
    rOut << "{\"_time\":" << ev.Timestamp << ",\"_type\":" << (int)ev.EventType << ",\"_value\":" << (int)ev.Value << '}';
}

void ss_appendTarget(stringstream& rOut, const NaiSe::EntityT& tar)
{
    rOut << "{\"_time\":" << tar.SpawnTime << ",\"_lineIndex\":" << tar.Location.first << ",\"_lineLayer\":" << tar.Location.second << ",\"_type\":" << (int)tar.Type.RawType << ",\"_cutDirection\":" << (int)tar.Value << '}';
}

void ss_appendObject(stringstream& rOut, const NaiSe::EntityT& obj)
{
    rOut << "{\"_time\":" << obj.SpawnTime << ",\"_lineIndex\":" << obj.Location.first << ",\"_type\":" << (int)obj.Type.RawType << ",\"_duration\":" << obj.Value << ",\"_width\":" << obj.Location.second << '}';
}

void ss_appendStage(stringstream& rOut, const char* stageName, int rank,/* const string& filename,*/ const char* modeName, int offset)
{
    rOut << R"({"_difficulty":")" << stageName
        << R"(","_difficultyRank":)" << rank
        << R"(,"_beatmapFilename":")" << modeName << stageName << ".dat"
        << R"(","_noteJumpMovementSpeed":0.0,"_noteJumpStartBeatOffset":)" << offset
        << '}';
}

// Returns iterator to last element in a sweep pattern, 'end' if pattern is not fullfilled.
vector<EntityT>::const_iterator isSweepPattern(vector<EntityT>::const_iterator it, vector<EntityT>::const_iterator end, double tTolerance_ms)
{
    if (it==end)
        return end;

    assert(128 > it->Location.first || 384 <= it->Location.first);
    float ti[4]{};
    float dt_i, dt_j;
    bool isLeftSweep = (127 < it->Location.first);
    char idx = isLeftSweep ? 2 : 1;
    char i = 1;

    ti[0] = it->SpawnTime;
    ++it;  // can be the end!
    for (; (i<4) && (it!=end); ++it)
    {
        if (ti[i-1] < (it->SpawnTime))
        {// found greater timestamp
            if (ti[i] == 0)
            {
                ti[i] = it->SpawnTime;
            } else if (it->SpawnTime > ti[i]){ // is in 2nd next timeline
                break;
            }
            dt_j = ti[i]-ti[i-1];
            if (i==1)
                dt_i = dt_j;
            if (it->Type.OsuType.IsContinous || tTolerance_ms < dt_j || ((0.5 * BLOCK_PLACEMENT_DOWNTIME_MS) < abs(dt_j - dt_i)))
            {// is not in sweep time tolerance
                break;
            }
            if ((it->Type.OsuType.IsCircle) && (idx == (uint16_t)(4u * it->Location.first / Os_Map_Width)))
            {// is entity in a sweep chain
                dt_i = dt_j;
                isLeftSweep ? --idx : ++idx;
                ++i;
            }
        }
    }
    return (i>=4) ? it : end;
}

}// anonymous ns


void CBsSequencer::transformBeatset(BeatSetT& rInOut) const
{
    if (GameTypes_t::beatsaber == rInOut.Game)
        return;

    assert(GameTypes_t::osu == rInOut.Game);
    assert(GameMode_t::os_mania == rInOut.Setting.Mode);// TODO select based on map type
    assert(mEnabledModes == BsModeFlagsT::FREESTYLE ||
        mEnabledModes == BsModeFlagsT::SUPPORTED);

    const auto colSz = (float)Os_Map_Width / 4;  
    const auto rowSz = (float)Os_Map_Height / 3;
    const double baseTime_ms = 60000.f / max(rInOut.Media.AverageRate_bpm, 1.f);
    
    forward_list<EventT> evList;
    EventT evn;

    // Switch off all lights at start
    const EventType_t lights[] = {
        EventType_t::sw_lightBg,
        EventType_t::sw_lightSd,
        EventType_t::sw_laserLs,
        EventType_t::sw_laserRs,
        EventType_t::sw_lightLo
    };
    for (auto&& li : lights)
    {
        evn.EventType = li;
        evList.push_front(evn);
    }

    // __ Events __
    // After lead in: back lights
    // 1st target: environment light
    // combo : env color change
    // beat shift : laser speed
    // Kiai : toggle floor light, lasers, extend inner rings, turn off backlight
    // spinner : vertical wall
    // hold: horizontal wall
    evn.EventType = lights[0];
    evn.Timestamp = max(4.f, quantizeTimestamp(rInOut.Setting.LeadIn_ms, baseTime_ms, rInOut.Setting.SubgridSize));
    evn.Value = (float)Switch_t::s1_on;
    evList.push_front(evn);

    // Create speed change and key events
    float tVal{};
    for (auto&& evi : rInOut.Events)
    {
        evn.Timestamp = quantizeTimestamp(evi.Timestamp, baseTime_ms, rInOut.Setting.SubgridSize);
        switch (evi.EventType)
        {
        case EventType_t::shift:
            if (FLT_EPSILON < abs(tVal - evi.Value))  // bpm change
            {// TODO test this part on speed change
                evn.EventType = EventType_t::set_laserLsSpd;
                evn.Value = setSpeedByPeriod(evi.Value);
                evList.push_front(evn);

                evn.EventType = EventType_t::set_laserRsSpd;

                tVal = evi.Value;
            } else {  // kiai off
                evn.Value = (float)Switch_t::s2_fl_off;
                evn.EventType = EventType_t::sw_laserLs;
                evList.push_front(evn);

                evn.EventType = EventType_t::sw_laserRs;
                evList.push_front(evn);

                evn.EventType = EventType_t::sw_lightLo;
                evList.push_front(evn);

                evn.EventType = EventType_t::sw_lightBg;
                evn.Value = (float)Switch_t::s1_on;
                evList.push_front(evn);

                evn.EventType = EventType_t::ringMov;
                evn.Value = RING_MOV_TOGG_VAL;
            }
            break;

        case EventType_t::kiai:
            evn.EventType = EventType_t::sw_lightBg;
            evn.Value = (float)Switch_t::s_Off;
            evList.push_front(evn);

            evn.Value = (float)Switch_t::s2_on;
            evn.EventType = EventType_t::sw_laserLs;
            evList.push_front(evn);

            evn.EventType = EventType_t::sw_laserRs;
            evList.push_front(evn);

            evn.EventType = EventType_t::sw_lightLo;
            evList.push_front(evn);

            evn.EventType = EventType_t::ringMov;
            evn.Value = RING_MOV_TOGG_VAL;
            break;

        default:
            evn.EventType = EventType_t::ringRot;
            evn.Value = 0;
            break;
        }
        evList.push_front(evn);
        
    }

    auto dst = rInOut.Targets.begin();
    auto tarEnd = rInOut.Targets.cend();
    auto src = find_if(rInOut.Targets.cbegin(), tarEnd, [tOffs = rInOut.Setting.LeadIn_ms](EntityT en) {
        return en.SpawnTime >= tOffs;
    });

    // Turn on environment light (blue) on first target
    if (src!=tarEnd)
    {
        evn.Timestamp = quantizeTimestamp(src->SpawnTime, baseTime_ms, rInOut.Setting.SubgridSize);
        evn.Value = (float)Switch_t::s1_on;
        evn.EventType = EventType_t::sw_lightSd;
        evList.push_front(evn);
    }

    // Transform targets in-place
    // DO NOT ADD MORE TARGETS THAN CONTAINER SIZE
    EntityT obj;
    EntityT obs;
    bool isLeft{};
    bool fixedRow = GameMode_t::os_mania == rInOut.Setting.Mode;
    float timeSlots[4][3];  // 4:x, 3:y
    float lastSample{};
    //size_t equalCnt{};

    for (auto&& rows : timeSlots)
    {// Init each slot to 2sec.
        fill(rows, &rows[3], 2000.f);
    }

    rInOut.Objects.clear();
    tVal = 0;
    for (; src!=tarEnd; ++src)
    {// src and dst CAN be same -> obj used as work copy
        obj.Location.first = (uint16_t)(src->Location.first / colSz);
        obj.Location.second = fixedRow ? 0 : (uint16_t)(src->Location.second / rowSz);
        obj.Value = (float)Direction_t::midCenter;  // TODO give some direction logic
        obj.SpawnTime = quantizeTimestamp(src->SpawnTime, baseTime_ms, rInOut.Setting.SubgridSize);
        //obj.Type.RawType == 0
        assert(obj.Location.first < Bs_Map_Width);
        assert(obj.Location.second < Bs_Map_Height);

        if (src->Type.OsuType.IsComboStart)
        {// lights stay turned off till this type appears fist (might never be)
            evn.Timestamp = obj.SpawnTime;
            evn.EventType = EventType_t::sw_lightSd;
            evn.Value = (float)(isLeft ? Switch_t::s2_on : Switch_t::s1_on);  // toggle color
            isLeft = !isLeft;
            evList.push_front(evn);
        }

        // TODO implement placement checkup & validation:
        // - awkward strikes
        // - minimum temporal box distance other than 125/200ms
        // - handle minor temporal shifts
        if (src->Type.OsuType.IsCircle || 
            src->Type.OsuType.IsContinous)
        {
            // Add top row wall with target on front
            if (src->Type.OsuType.IsContinous){
                obs.Location.first = obj.Location.first;
                obs.Location.second = 1;  // used as wall width
                obs.SpawnTime = obj.SpawnTime;
                obs.Type.RawType = WALL_HORIZONTAL;
                obs.Value = quantizeTimestamp(src->Value - src->SpawnTime, baseTime_ms, rInOut.Setting.SubgridSize);  // used as duration
                if (1.f <= obs.Value)
                {
                    timeSlots[obs.Location.first][2] = timeSlots[obs.Location.first][1] = src->Value;  // (value:end timestamp) wall blocks upper rows for the duration
                    rInOut.Objects.push_back(obs);
                }
            } else if (0 == obj.Location.first || 3 == obj.Location.first) {// Detect temporal sweeps
                auto tmpIt = isSweepPattern(src, tarEnd, baseTime_ms);
                if (tarEnd != tmpIt)
                {
                    // wall blocks side columns for the duration
                    if (obj.Location.first)
                    {
                        obs.Location.first = 2;
                        timeSlots[1][2] = timeSlots[2][2] = timeSlots[3][2] =
                            timeSlots[1][1] = timeSlots[2][1] = timeSlots[3][1] =
                                timeSlots[1][0] = timeSlots[2][0] = timeSlots[3][0] = tmpIt->SpawnTime + BLOCK_PLACEMENT_DOWNTIME_MS;
                    } else {
                        obs.Location.first = 0;
                        timeSlots[0][2] = timeSlots[1][2] = timeSlots[2][2] =
                            timeSlots[0][1] = timeSlots[1][1] = timeSlots[2][1] =
                                timeSlots[0][0] = timeSlots[1][0] = timeSlots[2][0] = tmpIt->SpawnTime + BLOCK_PLACEMENT_DOWNTIME_MS;
                    }
                    obs.Type.RawType = WALL_VERTICAL;
                    obs.Location.second = 2;  // used as wall width
                    obs.SpawnTime = obj.SpawnTime;
                    obs.Value = quantizeTimestamp(tmpIt->SpawnTime - src->SpawnTime, baseTime_ms, rInOut.Setting.SubgridSize);
                    
                    if (obs.SpawnTime > tVal)
                    {// Avoid stacking or unevadable walls
                        rInOut.Objects.push_back(obs);
                        tVal = obs.SpawnTime + obs.Value + 1.f;  // or quantize tmpIt->SpawnTime
                    }
                    //src = tmpIt-1;  // last block of the sweep as next loop input
                    continue;
                }
            }

            // Minimum spacing of neighbour targets
            if (lastSample != obj.SpawnTime)  
            {// A new timeline (quantized timestamp!)
                if (BLOCK_PLACEMENT_DOWNTIME_NEIGHBOUR_MS > baseTime_ms * (obj.SpawnTime - lastSample))
                    continue;

                //equalCnt = 1;
                lastSample = obj.SpawnTime;
            }/*else {  // append to begun timeline
                ++equalCnt;

                // Restrict to 2 targets per row
                if (equalCnt > 2)
                {
                    if (!src->Type.OsuType.IsContinous)
                    {// Push to mid row
                        (obj.Location.first == 0 || obj.Location.first == 2) ? ++obj.Location.first : --obj.Location.first;
                        obj.Location.second = 1;
                    }
                }
            }*/
            
            // Minimum spacing (200ms is fastest beat, 55.6ms is world record in keyboard typing)
            // Blocks are touching each other under about 120ms
            if (BLOCK_PLACEMENT_DOWNTIME_MS < (src->SpawnTime - timeSlots[obj.Location.first][obj.Location.second]))  // negative considered as blocked
            {// Also avoids targets within upper walls
                timeSlots[obj.Location.first][obj.Location.second] = src->SpawnTime;
                swap(obj, *dst);  // do not use values of obj or src after this line!
                ++dst;
            }/*else if (obj.Location.second == 0) {
                --equalCnt;
            }*/
        }
        /* else if (ti.Type.OsuType.IsSpin){  // ignored in mania
            obj.Type.RawType = WALL_VERTICAL
            obj.Location.first = 1;
            obj.Location.second = 2;  // width
            obj.Value = 5; // TODO implement duration, arg[5]
            rIn.Objects.push_back(obj);
        } else if (ti.Type.OsuType.IsSlider) {  // ignored in mania
            obj.Type.RawType = (uint8_t)Cube_t::bomb;
        }
         */
    }
    if (dst != rInOut.Targets.end())
    {
        rInOut.Targets.erase(dst, rInOut.Targets.end());
    }

    // Sort added events by timestamp ascendingly and move into argument container
    evList.sort([ ](EventT evn, EventT other) {
        return evn.Timestamp < other.Timestamp;
    });
    rInOut.Events.resize(distance(evList.cbegin(), evList.cend()));  // list has no size()
    assert(rInOut.Events.size() > 0);
    move(evList.begin(), evList.end(), rInOut.Events.begin());
    
    // Set Bs specific meta
    rInOut.Game = NaiSe::GameTypes_t::beatsaber;
    rInOut.Setting.Mode = NaiSe::GameMode_t::bs_2H_free;  // TODO add other modes
    rInOut.Setting.MapName = MODE_NAME_NA;
    rInOut.Setting.MapName.append(STAGE_NAMES[rInOut.StageLevel>>1]);

    if (rInOut.Targets.empty())
        return;

    // Assign left/right targets
    ptrdiff_t tarCnt;
    auto lneEnd = rInOut.Targets.end();
        
    tarEnd = rInOut.Targets.cend();
    isLeft = true;
    src = rInOut.Targets.cbegin();
    dst = rInOut.Targets.begin();
    while (src != tarEnd)
    {
        lneEnd = upper_bound(dst, rInOut.Targets.end(), *dst, [ ](const EntityT& en, const EntityT& nxt) {
            return en.SpawnTime < nxt.SpawnTime;
        });
        tarCnt = distance(dst, lneEnd);
        
        switch (tarCnt)  // number of inline targets
        {
        case 0:
            break;

        // regular assign
        case 1:
        case 2:
            while (src != lneEnd)
            {
                dst->Type.RawType = (uint8_t)((src->Location.first < 2) ?
                    Cube_t::left : Cube_t::right);
                ++dst; src++;
            }
            break;

        // twist assign
        case 3:
            // count adjacent targets
            sort(dst, lneEnd, [](const EntityT& en, const EntityT& other) {
                return en.Location.first < other.Location.first;
            });
            tarCnt = 0;
            while (src != lneEnd)  
            {
                if (tarCnt == src->Location.first)
                    ++tarCnt;
                else
                    break;
                ++src;
            }
            switch (tarCnt)  // number of left adjacent (connected) targets
            {
            case 0:  // right side
                while (dst != lneEnd)
                {
                    dst->Type.RawType = (uint8_t)Cube_t::left;
                    ++dst;
                }
                break;

            case 1:
                (dst++)->Type.RawType = (uint8_t)Cube_t::left;
                (dst++)->Type.RawType = (uint8_t)Cube_t::bomb;
                (dst++)->Type.RawType = (uint8_t)Cube_t::bomb;
                break;

            case 2:
                (dst++)->Type.RawType = (uint8_t)Cube_t::bomb;
                (dst++)->Type.RawType = (uint8_t)Cube_t::bomb;
                (dst++)->Type.RawType = (uint8_t)Cube_t::right;
                break;

            case 3:  // left side
                while (dst != lneEnd)
                {
                    dst->Type.RawType = (uint8_t)Cube_t::right;
                    ++dst;
                }
                break;
            }
            break;

        // alternating assign
        case 4:
            while (dst != lneEnd)
            {
                dst->Type.RawType = (uint8_t)(isLeft ? Cube_t::left : Cube_t::right);
                ++dst;
            }
            isLeft = !isLeft;
            break;

        default:
            // has targets in upper rows
            break;
        }
        // keep dst, lneEnd and tarEnd iterators synced
        src = lneEnd;
    }
}


vector<string> CBsSequencer::serializeBeatset(const NaiSe::BeatSetT& rIn) const
{
    stringstream strbuff;
    vector<string> container;

    //strbuff.precision(rIn.Setting.SubgridSize); could limit precision on large numbers
    //strbuff.setf(ios::fixed, ios::floatfield); always appends zeroes

    // Header
    strbuff << "{\"_version\":" << '"' << getVersion() << "\",";

    strbuff << "\"_events\":[";
    if (!rIn.Events.empty())
    {
        auto evIt = rIn.Events.cbegin();
        ss_appendEvent(strbuff, *evIt);
        ++evIt;
        for (auto end=rIn.Events.cend(); evIt!=end; ++evIt)
        {
            strbuff << ',';
            ss_appendEvent(strbuff, *evIt);
        }
    }
    strbuff << "],";

    strbuff << "\"_notes\":[";
    if (!rIn.Targets.empty())
    {
        auto evIt = rIn.Targets.cbegin();
        ss_appendTarget(strbuff, *evIt);
        ++evIt;
        for (auto end=rIn.Targets.cend(); evIt!=end; ++evIt)
        {
            strbuff << ',';
            ss_appendTarget(strbuff, *evIt);
        }
    }
    strbuff << "],";

    strbuff << "\"_obstacles\":[";
    if (!rIn.Objects.empty())
    {
        auto evIt = rIn.Objects.cbegin();
        ss_appendObject(strbuff, *evIt);
        ++evIt;
        for (auto end=rIn.Objects.cend(); evIt!=end; ++evIt)
        {
            strbuff << ',';
            ss_appendObject(strbuff, *evIt);
        }
    }
    strbuff << "]}";

    strbuff.flush();
    container.push_back(strbuff.str());  // one line
    return container;
}


string CBsSequencer::createMapInfo(const MediaInfoT& rInMeta, const SettingT& rInConf, BsStageFlagsT stages) const
{
    stringstream sstr;

    sstr << R"({"_version":")" << getVersion();
    sstr << R"(","_songName":")" << rInMeta.Title;
    sstr << R"(","_songSubName":"","_songAuthorName":")" << rInMeta.Artist;
    sstr << R"(","_levelAuthorName":")" << rInMeta.Author;
    sstr << R"(","_beatsPerMinute":)" << rInMeta.AverageRate_bpm;
    sstr << R"(,"_songTimeOffset":0.0,"_shuffle":0.0,"_shufflePeriod":1.0,"_previewStartTime":)" << floor((float)rInMeta.PreviewStart_ms / 1000);
    sstr << R"(,"_previewDuration":10.0,"_songFilename":"Track.ogg","_coverImageFilename":"cover.png","_environmentName":"DefaultEnvironment","_difficultyBeatmapSets":[)";
    
    if (mEnabledModes == BsModeFlagsT::SUPPORTED ||
        mEnabledModes & BsModeFlagsT::FREESTYLE )
    {// TODO add other modes
        sstr << R"({"_beatmapCharacteristicName":")" << MODE_NAME_NA << R"(","_difficultyBeatmaps":[)";
    
        static_assert(sizeof(STAGE_NAMES)>=5, "Attempts to access 5th element, but has less");
        if (stages.Easy)
        {// TODO fill rank values from beatset StageLevel
            ss_appendStage(sstr, STAGE_NAMES[0], 1, MODE_NAME_NA, 0);
            if (stages.Normal | stages.Hard | stages.Expert | stages.ExpertPlus)
                sstr << ',';
        }
        
        if (stages.Normal)
        {
            ss_appendStage(sstr, STAGE_NAMES[1], 3, MODE_NAME_NA, 0);
            if (stages.Hard | stages.Expert | stages.ExpertPlus)
                sstr << ',';
        }
        
        if (stages.Hard)
        {
            ss_appendStage(sstr, STAGE_NAMES[2], 5, MODE_NAME_NA, 0);
            if (stages.Expert | stages.ExpertPlus)
                sstr << ',';
        }
        
        if (stages.Expert)
        {
            ss_appendStage(sstr, STAGE_NAMES[3], 7, MODE_NAME_NA, 0);
            if (stages.ExpertPlus)
                sstr << ',';
        }

        if (stages.ExpertPlus)
        {
            ss_appendStage(sstr, STAGE_NAMES[4], 9, MODE_NAME_NA, 1);
        }
        sstr << "]}";
    }
    sstr << "]}";
    sstr.flush();

    /*
    ofstream infoFile("Info.dat");  // TODO create map folder
    if (!infoFile.is_open())
    return;

    infoFile << sstr.rdbuf();
    infoFile.flush();
    infoFile.close();
    */
    return sstr.str();
}


