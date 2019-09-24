#pragma once

namespace NaiSe {
struct MediaInfoT;
struct SettingT;
//struct BeatSetT
}


#include "Sequencer.h"

#include <string>
//#include <vector>


class CBsSequencer : public ISequencer
{
public:
    struct BsModeFlagsT
    {
        static const modeFlag_t SUPPORTED = 0x0u;
        static const modeFlag_t TWO_HAND = 0x1u;
        static const modeFlag_t ONE_HAND = 0x2u;
        static const modeFlag_t FREESTYLE = 0x4u;
    };
    struct BsStageFlagsT
    {
        bool Easy, Normal, Hard, Expert, ExpertPlus;
    };

    void transformBeatset(NaiSe::BeatSetT& rInOut) const final override;
    std::vector<std::string> serializeBeatset(const NaiSe::BeatSetT& rIn) const final override;
    std::string createMapInfo(const NaiSe::MediaInfoT& rInMeta, const NaiSe::SettingT& rInConf, BsStageFlagsT stages) const;
    
    const char* getVersion() const final override { return "2.0.0"; }
};

