#pragma once

namespace NaiSe{
struct BeatSetT;
}


#include <string>
#include <vector>


class ISequencer
{
public:
    using modeFlag_t = unsigned char;

protected:
    modeFlag_t mEnabledModes{};

public:
    virtual void transformBeatset(NaiSe::BeatSetT& rInOut) = 0;
    virtual std::vector<std::string> serializeBeatset(const NaiSe::BeatSetT& rIn) const = 0;
    
    virtual const char* getVersion() const = 0;
    void setMode(modeFlag_t flags) { mEnabledModes = flags; }  // TODO Implement handling
};

