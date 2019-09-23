#pragma once

#include <vector>

namespace NaiSe{
struct BeatSetT;
struct StringSequenceT;
struct SettingT;
struct MediaInfoT;
struct EventT;
struct EntityT;
}
class CBeatmap;


class COsuParser
{
public:
    bool tryParse(const CBeatmap& rIn, NaiSe::BeatSetT& rOut) const;

protected:
    //bool assignFromSeqence(const NaiSe::StringSequenceT& rInSeq, NaiSe::SettingT& rOut) const;
    //bool assignFromSeqence(const NaiSe::StringSequenceT& rInSeq, NaiSe::MediaInfoT& rOut) const;
    bool assignFromSequence(const NaiSe::StringSequenceT& rInSeq, std::vector<NaiSe::EventT>& rOut) const;  //incomplete template ok?
    bool assignFromSequence(const NaiSe::StringSequenceT& rInSeq, std::vector<NaiSe::EntityT>& rOut) const;
};

