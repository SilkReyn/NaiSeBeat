#pragma once

/*namespace std{
template <class T, class Alloc = allocator<T>> class vector;
}*/

namespace NaiSe{
struct BeatSetT;
using modeFlag_t = unsigned char;
//enum class GameMode_t;
}

#include <string>
#include <vector>



class ISequencer
{
protected:
    NaiSe::modeFlag_t mEnabledModes{};

public:
    virtual void transformBeatset(NaiSe::BeatSetT& rInOut) const = 0;
    virtual std::vector<std::string> serializeBeatset(const NaiSe::BeatSetT& rIn) const = 0;
    
    virtual std::string getVersion() const = 0;
    void setMode(NaiSe::modeFlag_t flags) { mEnabledModes = flags; }
};

