#include "BsSequencer.h"

#include <algorithm>
#include <functional>  //function
#include <forward_list>
#include <iterator> // distance
#include <sstream>

#include "common.hpp"


using namespace std;
using namespace NaiSe;


namespace NaiSe {
    enum class Direction_t : uint8_t { up=0, down, left, right, lUp, rUp, lDown, rDown, fwd, direction_size };
    enum class Cube_t : uint8_t { left=0, right, reserved, bomb };
    enum class Speed_t : uint8_t { reset=0, slo1, slo2, med1, med2, med3, fst1, fst2 };
    enum class Switch_t : uint8_t
    {
        s_Off = 0,
        s1_on, s1_fl_on, s1_fl_off,
        reserved = 4,
        s2_on, s2_fl_on, s2_fl_off
    };


    const char* const STAGE_NAMES[] = { "Easy", "Normal", "Hard", "Expert", "ExpertPlus" };
    const char MODE_NAME_NA[] = "NoArrows";
    const char MODE_NAME_NM[] = "Standard";
    const uint16_t LEAD_IN_TIME_MS = 3000;

}// NaiSe ns
ostream& operator<< (ostream& lhs, const EventType_t& rhs) { return (lhs << enum_cast(rhs)); }
float& operator<< (float& lhs, const Switch_t& rhs) { return (lhs = static_cast<uint8_t>(rhs)); }
uint8_t& operator<< (uint8_t& lhs, const Cube_t& rhs) { return (lhs = static_cast<uint8_t>(rhs)); }


namespace {

const uint8_t WALL_VERTICAL = 0u;
const uint8_t WALL_HORIZONTAL = 1u;
const float RING_MOV_TOGG_VAL = 0.f;
const float BLOCK_PLACEMENT_DOWNTIME_MS = 200.f;
const float BLOCK_PLACEMENT_DOWNTIME_NEIGHBOUR_MS = 125.f;
const auto OS_COL_SZ = (float)Os_Map_Width / 4;  
const auto OS_ROW_SZ = (float)Os_Map_Height / 3;
const uint16_t BS_MAX_BPM = 300u;

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
    rOut << "{\"_time\":" << ev.Timestamp << ",\"_type\":" << ev.EventType << ",\"_value\":" << (int)ev.Value << '}';
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

template<const char* TSetName>
void ss_appendSet(stringstream& sstr, CBsSequencer::BsStageFlagsT stages)
{
    sstr << R"({"_beatmapCharacteristicName":")" << TSetName << R"(","_difficultyBeatmaps":[)";
    
    //--> Difficulty Array
    static_assert(sizeof(STAGE_NAMES) >= 5, "Attempts to access 5th element, but has less");
    if (stages.Easy)
    {// TODO fill rank values from beatset StageLevel
        ss_appendStage(sstr, STAGE_NAMES[0], 1, TSetName, 0);
        if (stages.Normal | stages.Hard | stages.Expert | stages.ExpertPlus)
            sstr << ',';
    }

    if (stages.Normal)
    {
        ss_appendStage(sstr, STAGE_NAMES[1], 3, TSetName, 0);
        if (stages.Hard | stages.Expert | stages.ExpertPlus)
            sstr << ',';
    }

    if (stages.Hard)
    {
        ss_appendStage(sstr, STAGE_NAMES[2], 5, TSetName, 0);
        if (stages.Expert | stages.ExpertPlus)
            sstr << ',';
    }

    if (stages.Expert)
    {
        ss_appendStage(sstr, STAGE_NAMES[3], 7, TSetName, 0);
        if (stages.ExpertPlus)
            sstr << ',';
    }

    if (stages.ExpertPlus)
    {
        ss_appendStage(sstr, STAGE_NAMES[4], 9, TSetName, 1);
    }
    sstr << "]}";
}

}// anonymous ns


void processEvents(
    const vector<EventT>&  rInEvents,
    forward_list<EventT>&  rOutEvents,
    uint16_t               tLeadIn_ms,
    uint16_t               tStart_ms,
    function<float(float)> fSetSample)
{
    //using BsSw_t = CBsSequencer::Switch_t;

    assert(fSetSample);
    if (tStart_ms < tLeadIn_ms)
        swap(tStart_ms, tLeadIn_ms);

    EventT evn;
    float tVal{};

    // Switch off all lights at start
    // Turn-on events must follow at non-zero timestamps
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
        rOutEvents.push_front(evn);
    }

    // __ Events __
    // combo : env color change
    // beat shift : laser speed
    // Kiai : toggle floor light, lasers, extend inner rings, turn off backlight
    // other : rotate rings

    // Turn on background light after lead-in time
    evn.EventType = lights[0];
    evn.Timestamp = max(3.f, fSetSample(tLeadIn_ms));
    evn.Value << Switch_t::s1_on;
    rOutEvents.push_front(evn);

    // Turn on environment light at start time
    evn.Timestamp = max(3.f , fSetSample(tStart_ms));
    evn.Value << Switch_t::s1_on;
    evn.EventType = lights[1];
    rOutEvents.push_front(evn);
    
    // Create speed change and key events
    for (auto&& evi : rInEvents)
    {
        evn.Timestamp = fSetSample(evi.Timestamp);  // quantizeTimestamp(evi.Timestamp, baseTime_ms, rInOut.Setting.SubgridSize);
        switch (evi.EventType)
        {
        case EventType_t::shift:
            if (FLT_EPSILON < abs(tVal - evi.Value))  // bpm change
            {// TODO test this part on speed change
                evn.EventType = EventType_t::set_laserLsSpd;
                evn.Value = setSpeedByPeriod(evi.Value);
                rOutEvents.push_front(evn);

                evn.EventType = EventType_t::set_laserRsSpd;

                tVal = evi.Value;
            } else {  // kiai off
                evn.Value << Switch_t::s2_fl_off;
                evn.EventType = EventType_t::sw_laserLs;
                rOutEvents.push_front(evn);

                evn.EventType = EventType_t::sw_laserRs;
                rOutEvents.push_front(evn);

                evn.EventType = EventType_t::sw_lightLo;
                rOutEvents.push_front(evn);

                evn.EventType = EventType_t::sw_lightBg;
                evn.Value << Switch_t::s1_on;
                rOutEvents.push_front(evn);

                evn.EventType = EventType_t::ringMov;
                evn.Value = RING_MOV_TOGG_VAL;
            }
            break;

        case EventType_t::kiai:
            evn.EventType = EventType_t::sw_lightBg;
            evn.Value << Switch_t::s_Off;
            rOutEvents.push_front(evn);

            evn.Value << Switch_t::s2_on;
            evn.EventType = EventType_t::sw_laserLs;
            rOutEvents.push_front(evn);

            evn.EventType = EventType_t::sw_laserRs;
            rOutEvents.push_front(evn);

            evn.EventType = EventType_t::sw_lightLo;
            rOutEvents.push_front(evn);

            evn.EventType = EventType_t::ringMov;
            evn.Value = RING_MOV_TOGG_VAL;
            break;

        default:
            evn.EventType = EventType_t::ringRot;
            evn.Value = 0;
            break;
        }
        rOutEvents.push_front(evn);
    }
}


void transform_mania(
    BeatSetT&              rInOut,
    const double           baseTime_ms,
    forward_list<EventT>&  evList,
    function<float(float)> ftRelative,
    GameMode_t             mode=GameMode_t::bs_2H_free)
{
    assert(ftRelative);
    //if (GameTypes_t::beatsaber == rInOut.Game)
    //    return;

    //assert(GameTypes_t::osu == rInOut.Game);
    assert(GameMode_t::os_mania == rInOut.Setting.Mode);
    //assert(gameMode == CBsSequencer::BsModeFlagsT::FREESTYLE ||
    //    gameMode == CBsSequencer::BsModeFlagsT::SUPPORTED);

    //const auto colSz = (float)Os_Map_Width / 4;  
    //const auto rowSz = (float)Os_Map_Height / 3;
    //const double baseTime_ms = 60000.f / max(rInOut.Media.AverageRate_bpm, 1.f);

    /*
    EventT evn;
    forward_list<EventT> evList;
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

    // Turn on environment light (blue) on first target
    if (src!=tarEnd)
    {
        evn.Timestamp = quantizeTimestamp(src->SpawnTime, baseTime_ms, rInOut.Setting.SubgridSize);
        evn.Value = (float)Switch_t::s1_on;
        evn.EventType = EventType_t::sw_lightSd;
        evList.push_front(evn);
    }
    */
    auto dst = rInOut.Targets.begin();
    auto tarEnd = rInOut.Targets.cend();
    auto src = find_if(rInOut.Targets.cbegin(), tarEnd, [ ](EntityT en) {
            return en.SpawnTime > LEAD_IN_TIME_MS;
        });

    // Transform targets in-place
    // DO NOT ADD MORE TARGETS THAN CONTAINER SIZE
    EntityT obj;
    EntityT obs;
    bool isLeft = true;
    //bool fixedRow = GameMode_t::os_mania == rInOut.Setting.Mode;
    float timeSlots[4][3] = {};  // 4:x, 3:y
    float lastSample{};
    //size_t equalCnt{};

    //for (auto&& rows : timeSlots)
    //{// Init each slot to 2sec.
    //    fill(rows, &rows[3], 2000.f);
    //}

    rInOut.Objects.clear();
    float tSample{};
    for (; src!=tarEnd; ++src)
    {// src and dst CAN be same -> obj used as work copy
        obj.Location.first = (uint16_t)(src->Location.first / OS_COL_SZ);
        obj.Location.second = 0;
        obj.Value = enum_cast(Direction_t::fwd);  // TODO give some direction logic
        obj.SpawnTime = ftRelative(src->SpawnTime); //quantizeTimestamp(src->SpawnTime, baseTime_ms, rInOut.Setting.SubgridSize);
        //obj.Type.RawType == 0
        assert(obj.Location.first < Bs_Map_Width);
        assert(obj.Location.second < Bs_Map_Height);

        if (src->Type.OsuType.IsComboStart)
        {
            evList.emplace_front(
                EventT{
                    EventType_t::sw_lightSd,
                    obj.SpawnTime,
                    (float)enum_cast(isLeft ? Switch_t::s2_on : Switch_t::s1_on)
                });
            isLeft = !isLeft; // toggle color
        }

        if (src->Type.OsuType.IsCircle || 
            src->Type.OsuType.IsContinous)
        {
            // Add top row wall with target on front
            if (src->Type.OsuType.IsContinous){
                obs.Location.first = obj.Location.first;
                obs.Location.second = 1;  // used as wall width
                obs.SpawnTime = obj.SpawnTime;
                obs.Type.RawType = WALL_HORIZONTAL;
                obs.Value = ftRelative(src->Value - src->SpawnTime); //quantizeTimestamp(src->Value - src->SpawnTime, baseTime_ms, rInOut.Setting.SubgridSize);  // used as duration
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
                            timeSlots[1][0] = timeSlots[2][0] = timeSlots[3][0] =
                            tmpIt->SpawnTime + BLOCK_PLACEMENT_DOWNTIME_MS;
                    } else {
                        obs.Location.first = 0;
                        timeSlots[0][2] = timeSlots[1][2] = timeSlots[2][2] =
                            timeSlots[0][1] = timeSlots[1][1] = timeSlots[2][1] =
                            timeSlots[0][0] = timeSlots[1][0] = timeSlots[2][0] =
                            tmpIt->SpawnTime + BLOCK_PLACEMENT_DOWNTIME_MS;
                    }
                    obs.Type.RawType = WALL_VERTICAL;
                    obs.Location.second = 2;  // used as wall width
                    obs.SpawnTime = obj.SpawnTime;
                    obs.Value = ftRelative(tmpIt->SpawnTime - src->SpawnTime); //quantizeTimestamp(tmpIt->SpawnTime - src->SpawnTime, baseTime_ms, rInOut.Setting.SubgridSize);

                    if (obs.SpawnTime > tSample)
                    {// Avoid stacking or unevadable walls
                        rInOut.Objects.push_back(obs);
                        tSample = obs.SpawnTime + obs.Value + 1.f;
                    }
                    continue;
                }
            }

            // Minimum spacing of neighbour targets
            if (lastSample != obj.SpawnTime)  
            {// A new timeline (quantized timestamp!)
                if (BLOCK_PLACEMENT_DOWNTIME_NEIGHBOUR_MS > baseTime_ms * (obj.SpawnTime - lastSample))
                    continue;
                lastSample = obj.SpawnTime;
            }
             // Minimum spacing (200ms is fastest beat, 55.6ms is world record in keyboard typing)
             // Blocks are touching each other under about 120ms
            if (BLOCK_PLACEMENT_DOWNTIME_MS < (src->SpawnTime - timeSlots[obj.Location.first][obj.Location.second]))  // negative considered as blocked
            {// Also avoids targets within upper walls
                timeSlots[obj.Location.first][obj.Location.second] = src->SpawnTime;
                swap(obj, *dst);  // do not use values of obj or src after this line!
                ++dst;
            }
        }
    }
    if (dst != rInOut.Targets.end())
    {
        rInOut.Targets.erase(dst, rInOut.Targets.end());
    }
    /*
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
    */
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
                dst->Type.RawType = enum_cast((src->Location.first < 2) ?
                    Cube_t::left : Cube_t::right);
                ++dst; src++;
            }
            break;

            // twist assign
        case 3:
            // count adjacent targets
            sort(dst, lneEnd,
                [](const EntityT& en, const EntityT& other) {
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
                (dst++)->Type.RawType << Cube_t::left;
                (dst++)->Type.RawType << Cube_t::bomb;
                (dst++)->Type.RawType << Cube_t::bomb;
                break;

            case 2:
                (dst++)->Type.RawType << Cube_t::bomb;
                (dst++)->Type.RawType << Cube_t::bomb;
                (dst++)->Type.RawType << Cube_t::right;
                break;

            case 3:  // left side
                while (dst != lneEnd)
                {
                    dst->Type.RawType << Cube_t::right;
                    ++dst;
                }
                break;
            }
            break;

            // alternating assign
        case 4:
            while (dst != lneEnd)
            {
                dst->Type.RawType << (isLeft ? Cube_t::left : Cube_t::right);
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


void transform_taiko(
    vector<EntityT>&       rInOutTar,
    const size_t           fstIdx,
    const double           baseTime_ms,
    forward_list<EventT>&  rOutEvents,
    vector<EntityT>&       rOutObj,
    function<float(float)> ftRelative,
    GameMode_t             mode=GameMode_t::bs_2H_free)
{
    using HitArea_t = HitTypeT::area_t;
    assert(ftRelative);
    if (rInOutTar.empty())
        return;
    
    // TODO validate game mode when implemented

    bool isBlue = true;
    bool isLeft{};
    float timeSlots[2] = {};
    float nextTs{};
    const auto tarSz = rInOutTar.size();

    vector<EntityT> tars;
    EntityT out;
    HitTypeT ht;

    out.Value = enum_cast(Direction_t::fwd);
    for (auto i=fstIdx+1; i<=tarSz; ++i)
    {
        const auto& tar = rInOutTar[i - 1];
        if (tar.Type.OsuType.IsComboStart)
        {// toggle color
            rOutEvents.emplace_front(
                EventT{
                    EventType_t::sw_lightSd,
                    ftRelative(tar.SpawnTime),
                    (float)enum_cast(isBlue ? Switch_t::s2_on : Switch_t::s1_on)
                });
            isBlue = !isBlue; 
        }

        //--> Next target (if any)
        if (i < tarSz)
            nextTs = rInOutTar[i].SpawnTime;
        else
            nextTs += LEAD_IN_TIME_MS;
        //<--

        if (tar.Type.OsuType.IsCircle)
        {// Single action

            // Minimum spacing of same hand targets
            if (BLOCK_PLACEMENT_DOWNTIME_MS > (tar.SpawnTime - timeSlots[isLeft ? 0 : 1]))
                continue;

            timeSlots[isLeft ? 0 : 1] = tar.SpawnTime;
            out.SpawnTime = ftRelative(tar.SpawnTime);
            bool isFinisher = (2 * baseTime_ms) < (nextTs - tar.SpawnTime);
            ht.setF(tar.Value);
            switch (ht.Area)
            {
            case HitArea_t::don:
            case HitArea_t::softCenter:
            default:
                out.Location.first = isLeft ? 1 : 2;
                out.Location.second = isFinisher ? 1 : 0;
                out.Type.RawType << (isLeft ? Cube_t::left : Cube_t::right);
                tars.emplace_back(out);
                isLeft = !isLeft;
                break;

            case HitArea_t::katsu:
            case HitArea_t::rim:
                if (isFinisher)
                {
                    out.Location.first = isLeft ? 1 : 2;
                    out.Location.second = 2;
                } else {
                    out.Location.first = isLeft ? 0 : 3;
                    out.Location.second = 1;
                }
                out.Type.RawType << (isLeft ? Cube_t::left : Cube_t::right);
                tars.emplace_back(out);
                isLeft = !isLeft;
                break;

            case HitArea_t::dondon:
            case HitArea_t::hardCenter:
                out.Location.first = 1;
                out.Location.second = isFinisher ? 1 : 0;
                out.Type.RawType << Cube_t::left;
                tars.emplace_back(out);
                out.Location.first = 2;
                out.Type.RawType << Cube_t::right;
                tars.emplace_back(out);
                break;

            case HitArea_t::katatsu:
            case HitArea_t::sides:
                if (isFinisher)
                {
                    out.Location.first = 1;
                    out.Location.second = 2;
                }else {
                    out.Location.first = 0;
                    out.Location.second = 1;
                }
                out.Type.RawType << Cube_t::left;
                tars.emplace_back(out);
                out.Location.first = isFinisher ? 2 : 3;
                out.Type.RawType << Cube_t::right;
                tars.emplace_back(out);
                break;
            }
        } else if(tar.Type.OsuType.IsSlider) {  // duration limited multi action
            auto tMax = min((float)baseTime_ms / 140.f * tar.Value + tar.SpawnTime, nextTs);
            out.Location.first = isLeft ? 2 : 0;
            out.Location.second = 2;
            out.Type.RawType = WALL_VERTICAL;
            out.SpawnTime = ftRelative(tar.SpawnTime);
            out.Value = ftRelative(tMax - tar.SpawnTime);
            rOutObj.emplace_back(out);
            out.Location.second = 0;
            out.Value = enum_cast(Direction_t::fwd);
            bool isSideL = isLeft;
            for (auto ts=tar.SpawnTime; ts<tMax; ts+=250.f)
            {
                /*if (isSideL)
                    out.Location.first = isLeft ? 0 : 1;
                else
                    out.Location.first = isLeft ? 2 : 3;
                */
                if (isSideL)
                    out.Location.first = 0;
                else
                    out.Location.first = 3;
                out.Type.RawType << (isLeft ? Cube_t::left : Cube_t::right);
                out.SpawnTime = ftRelative(ts);
                tars.emplace_back(out);
                isLeft = !isLeft;
            }
        }else if(tar.Type.OsuType.IsSpin) {  // end limited multi action
            for (auto ts = tar.SpawnTime; (ts < tar.Value) && (ts < nextTs); ts += (float)baseTime_ms)
            {
                out.SpawnTime = ftRelative(ts);
                out.Type.RawType << Cube_t::bomb;
                out.Location.first = 0;
                out.Location.second = 1;
                tars.emplace_back(out);

                out.Location.first = 3;
                tars.emplace_back(out);

                out.Location.first = 0;
                out.Location.second = isLeft ? 0 : 2;
                tars.emplace_back(out);

                out.Location.first = 3;
                out.Location.second = isLeft ? 2 : 0;
                tars.emplace_back(out);

                out.Location.first = 2;
                //out.Location.second = isLeft ? 2 : 0;
                out.Type.RawType << Cube_t::right;
                tars.emplace_back(out);

                out.Location.first = 1;
                out.Location.second = isLeft ? 0 : 2;
                out.Type.RawType << Cube_t::left;
                tars.emplace_back(out);

                isLeft = !isLeft;
            }
            timeSlots[0] = timeSlots[1] = tar.Value;
        }// types of targets
        
    }//each target

    if (mode == GameMode_t::bs_2H)
    {
        // 2nd pass
        for (auto&& tn : tars)
        {
            if (tn.Type.RawType == enum_cast(Cube_t::bomb))
                continue;

            switch (tn.Location.second)
            {
            case 0:
                switch (tn.Location.first)
                {
                case 0:
                case 3:
                    tn.Value = (float)enum_cast((tn.Type.RawType == enum_cast(Cube_t::left)) ? Direction_t::rDown : Direction_t::lDown);
                    
                    break;
                case 1:
                case 2:
                    tn.Value = (float)enum_cast(Direction_t::down);
                    break;
                }
                break;
            case 1:
                switch (tn.Location.first)
                {
                case 0:
                    tn.Value = (float)enum_cast(Direction_t::left);
                    break;
                case 1:
                case 2:
                    tn.Value = (float)enum_cast(Direction_t::fwd);
                    break;
                case 3:
                    tn.Value = (float)enum_cast(Direction_t::right);
                    break;
                }
                break;
            case 2:
                tn.Value = (float)enum_cast(Direction_t::up);
                break;
            }
        }
    }
    rInOutTar = tars;
}


void CBsSequencer::transformBeatset(BeatSetT& rInOut)
{
    if (GameTypes_t::beatsaber == rInOut.Game)
        return;

    assert(GameTypes_t::osu == rInOut.Game);

    const double baseTime_ms = 60000.f / max(1.f, min(rInOut.Media.AverageRate_bpm, (float)BS_MAX_BPM));

    size_t i_fst{};
    float tFirst;
    auto tarEnd = rInOut.Targets.cend();
    auto fSample =
        [P=baseTime_ms, S=rInOut.Setting.SubgridSize](float t) {
        return quantizeTimestamp(t, P, S);
    };
    forward_list<EventT> evList;

    // Find index of first target after lead-in and create light events accordingly
    while (i_fst < rInOut.Targets.size())
    {
        if (LEAD_IN_TIME_MS < (tFirst = rInOut.Targets[i_fst].SpawnTime))
            break;
        ++i_fst;
    }
    processEvents(rInOut.Events, evList, rInOut.Setting.LeadIn_ms, (uint16_t)min<float>(tFirst, UINT16_MAX), fSample);
    if (!rInOut.Targets[i_fst].Type.OsuType.IsComboStart)
    {
        evList.emplace_front(
            EventT{
                EventType_t::sw_lightSd,
                quantizeTimestamp(tFirst, baseTime_ms, rInOut.Setting.SubgridSize),
                (float)enum_cast(Switch_t::s1_on)
            });
    }

    //assert(
    //    mEnabledModes == BsModeFlagsT::FREESTYLE ||
    //    mEnabledModes == BsModeFlagsT::SUPPORTED);
    switch (rInOut.Setting.Mode)
    {
    case GameMode_t::os_mania:
        transform_mania(rInOut, baseTime_ms, evList, fSample);  //TODO test after refactor
        rInOut.Setting.Mode = GameMode_t::bs_2H_free;  // TODO add other modes
        rInOut.Setting.MapName = MODE_NAME_NA;
        break;

    case GameMode_t::os_taiko:
        transform_taiko(
            rInOut.Targets,
            i_fst,
            baseTime_ms,
            evList,
            rInOut.Objects,
            fSample,
            GameMode_t::bs_2H);  // supported: free and 2H
        mEnabledModes |= BsModeFlagsT::TWO_HAND;
        rInOut.Setting.Mode = GameMode_t::bs_2H;
        rInOut.Setting.MapName = MODE_NAME_NM;
        break;

    default:
        throw logic_error("CBsSequencer::transformBeatset - Game mode unsupported");
    }

    // Sort added events by timestamp ascendingly and move into argument container
    auto evIt = rInOut.Events.begin();
    size_t sz = rInOut.Events.size();
    evList.sort(
        [ ](EventT evn, EventT other) {
        return evn.Timestamp < other.Timestamp;
    });
    while (!evList.empty())
    {
        if (sz)
        {
            *evIt = move(evList.front());
            ++evIt; --sz;
        } else {
            rInOut.Events.emplace_back(move(evList.front()));
        }
        evList.pop_front();
    }

    // Set Bs specific meta
    rInOut.Game = NaiSe::GameTypes_t::beatsaber;
    rInOut.Setting.MapName.append(STAGE_NAMES[rInOut.StageLevel>>1]);
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


string CBsSequencer::createMapInfo(const MediaInfoT& rInMeta, BsStageFlagsT stages) const
{
    stringstream sstr;
    //--> Beatmap container
    sstr << R"({"_version":")" << getVersion();
    sstr << R"(","_songName":")" << rInMeta.Title;
    sstr << R"(","_songSubName":"","_songAuthorName":")" << rInMeta.Artist;
    sstr << R"(","_levelAuthorName":")" << rInMeta.Author;
    sstr << R"(","_beatsPerMinute":)" << rInMeta.AverageRate_bpm;
    sstr << R"(,"_songTimeOffset":0.0,"_shuffle":0.0,"_shufflePeriod":1.0,"_previewStartTime":)" << (int)(rInMeta.PreviewStart_ms / 1000);
    sstr << R"(,"_previewDuration":10.0,"_songFilename":"Track.ogg","_coverImageFilename":"cover.png","_environmentName":"DefaultEnvironment","_difficultyBeatmapSets":[)";
    
    //--> Set array
    //--> Mode container
    if ((mEnabledModes == BsModeFlagsT::SUPPORTED) || (mEnabledModes & BsModeFlagsT::FREESTYLE))
        ss_appendSet<MODE_NAME_NA>(sstr, stages);
    if (mEnabledModes & BsModeFlagsT::TWO_HAND)
        ss_appendSet<MODE_NAME_NM>(sstr, stages);
    //<-- difficulty array, mode container

    sstr << "]}";
    //<-- set array, beatmap container
    sstr.flush();

    return sstr.str();
}


