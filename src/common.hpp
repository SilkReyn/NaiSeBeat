#ifndef _COMMON_HPP_
#define _COMMON_HPP_


#include <cstdint> // uint types
#include <utility>  // pair, move, swap
#include <cassert>
#include <exception>
#include <type_traits>  // underlying_type
#include <string>
#include <vector>


namespace NaiSe {
const uint16_t Os_Map_Width = 512;
const uint16_t Os_Map_Height = 384;
const uint8_t Bs_Map_Width = 4;
const uint8_t Bs_Map_Height = 3;

enum class GameTypes_t { unknown, osu, beatsaber };
enum class GameMode_t { undefined = -1, os_taiko=1, os_mania=3, bs_1H=10, bs_2H, bs_2H_free };
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
    
    //Cube_t BsType;

    EntityTypeT() : RawType(0) {}
    EntityTypeT(uint8_t val) :RawType(val) {}
    operator int() { return static_cast<int>(RawType); }
};

union HitTypeT
{
    uint8_t RawType;
    struct
    {
        uint8_t Normal : 1,
            Whistle : 1,
            Finish : 1,
            Clap : 1,
            Unused;
    } Sound;
    enum class area_t : uint8_t 
    {
        softCenter = 0,
        don = 1,
        katsu = 2,
        hardCenter = 4,
        dondon = 5,
        katatsu = 6,
        rim = 8,
        sides = 12
    } Area;

    HitTypeT() : RawType(0) {}
    HitTypeT(uint8_t val) : RawType(val) {}
    float getF() const { return static_cast<float>(RawType); }
    void setF(float val) { RawType = (uint8_t)(static_cast<int>(val) & 0xFF); }
    operator int() { return static_cast<int>(RawType); }
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
    uint16_t     LeadIn_ms{};
    GameMode_t   Mode{GameMode_t::undefined};
    uint8_t      SubgridSize{8};
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

template <typename TEnum, class... TArgs>
class xvec : public std::vector<TArgs...>
{
public:
    using std::vector<TArgs...>::vector;

    decltype(auto) operator[](TEnum const i)
    {
        return (*this)[static_cast<size_t>(i)];
    }

    const auto& operator[](TEnum const i) const
    {
        return (*this)[static_cast<size_t>(i)];
    }

    using std::vector<TArgs...>::operator [];
};

template<typename TEnum>
auto enum_cast(const TEnum& e) { return static_cast<typename std::underlying_type<TEnum>::type>(e); }

}//namespace NaiSe



#endif
