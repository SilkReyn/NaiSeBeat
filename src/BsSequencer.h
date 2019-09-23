#pragma once

#include "Sequencer.h"
namespace NaiSe {
struct MediaInfoT;
struct SettingT;
}


class CBsSequencer : public ISequencer
{
public:
    //enum bsModes_t { MODE_TWO_HAND, MODE_ONE_HAND, MODE_FREESTYLE, MODE_MAX };
    struct BsModeFlagsT
    {
        static const NaiSe::modeFlag_t SUPPORTED = 0x0u;
        static const NaiSe::modeFlag_t TWO_HAND = 0x1u;
        static const NaiSe::modeFlag_t ONE_HAND = 0x2u;
        static const NaiSe::modeFlag_t FREESTYLE = 0x4u;
    };
    struct BsStageFlagsT
    {
        bool Easy, Normal, Hard, Expert, ExpertPlus;
    };
    enum class Direction_t : uint8_t { midUp=0, midDown, lCenter, rCenter, lUp, rUp, lDown, rDown, midCenter, direction_size };
    enum class Cube_t : uint8_t { left=0, right, reserved, bomb };
    enum class Speed_t { reset=0, slo1, slo2, med1, med2, med3, fst1, fst2 };
    enum class Switch_t
    {
        s_Off=0,
        s1_on, s1_fl_on, s1_fl_off,
        reserved=4,
        s2_on, s2_fl_on, s2_fl_off
    };

    static const char* STAGE_NAMES[5];

public:
    void transformBeatset(NaiSe::BeatSetT& rInOut) const final override;
    std::vector<std::string> serializeBeatset(const NaiSe::BeatSetT& rIn) const final override;
    std::string createMapInfo(const NaiSe::MediaInfoT& rInMeta, const NaiSe::SettingT& rInConf, BsStageFlagsT stages) const;

    std::string getVersion() const final override { return "2.0.0"; }
    
    
};