#ifndef _COMMON_HPP_
#define _COMMON_HPP_


#include <cstdint> // uint types
#include <utility>  // pair, move, swap
#include <cassert>
#include <exception>
#include <string>
#include <vector>
//#include <tuple>
//#include <functional>  hash


namespace NaiSe {
const uint16_t Os_Map_Width = 512;
const uint16_t Os_Map_Height = 384;
const uint8_t Bs_Map_Width = 4;
const uint8_t Bs_Map_Height = 3;

enum class GameTypes_t { unknown, osu, beatsaber };
enum class GameMode_t { undefined = -1, os_mania = 3, bs_1H=10, bs_2H, bs_2H_free };
enum class EventType_t {
    ignore = -1,

    sw_lightBg = 0,
    sw_lightSd,
    sw_laserLs,
    sw_laserRs,
    sw_lightLo,
    ringRot = 8,
    ringMov = 9,
    set_laserLsSpd = 12,
    set_laserRsSpd = 13,

    kiai,
    shift
};

union EntityTypeT
{
    uint8_t RawType;
    // Bit order platform specific
    struct //OsuTargetT
    {
        uint8_t IsCircle : 1,
            IsSlider : 1,
            IsComboStart : 1,
            IsSpin : 1,
            Unused : 3,
            IsContinous : 1;
    } OsuType;

    EntityTypeT() : RawType(0) {}
    EntityTypeT(uint8_t val) :RawType(val) {}
};

struct StringSequenceT
{
    std::vector<std::string>::const_iterator Begin;
    std::vector<std::string>::const_iterator End;  // points behind last element
    size_t Distance{};  // from fist to last inclusive, same as count or size

    StringSequenceT() = delete;

    bool isEmpty() const
    {
        return !Distance || (Begin == End);
    }

    StringSequenceT make_subsequence(size_t fromIndex, size_t toIndex) const
    {// This is pretty unsave, because the iterators could be uninitialized or outdated
        
        StringSequenceT ss(*this);
        if (isEmpty())
            throw std::length_error("StringSequenceT::make_subsequence(...) - Called on empty Sequence");
        
        if (End != Begin+Distance)
            throw std::range_error("StringSequenceT::make_subsequence(...) - Invalid range or bad iterator(s)");

        if (toIndex < fromIndex)
            std::swap(fromIndex, toIndex);

        ss.Distance = Distance - 1;  // max index
        if (fromIndex > ss.Distance)
            fromIndex = ss.Distance;
        
        if (toIndex > ss.Distance)
            toIndex = ss.Distance;

        ss.Distance = toIndex-fromIndex+1;  // dist to end
        ss.Begin = Begin + fromIndex;
        ss.End = ss.Begin + ss.Distance;

        return ss;
    }
};

struct SettingT
{
    std::string  MapName;
    uint16_t     LeadIn_ms{};  // ignore, cannot add silence to media-file
    GameMode_t   Mode{GameMode_t::undefined};
    uint8_t      SubgridSize{8};
    //uint16_t     ApproachTime_ms{1200};
    //Difficulty_t Stage{Difficulty_t::easy};
};

struct MediaInfoT
{
    std::string Filename;
    std::string Artist;
    std::string Title;
    std::string Author;
    uint32_t PreviewStart_ms{};
    float AverageRate_bpm{};
};

struct EventT
{
    EventType_t EventType{EventType_t::ignore};
    float Timestamp{};
    float Value{};
};

struct EntityT
{
    std::pair<uint16_t, uint16_t> Location;
    EntityTypeT Type;
    float       SpawnTime{};
    float       Value{};
};

struct BeatSetT
{
    GameTypes_t  Game{GameTypes_t::unknown};
    MediaInfoT   Media;
    SettingT     Setting;
    uint8_t      StageLevel{};
    std::vector<EventT> Events;
    std::vector<EntityT> Targets;
    std::vector<EntityT> Objects;
};

}//namespace NaiSe



#endif
