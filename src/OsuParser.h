#pragma once

namespace NaiSe{
struct BeatSetT;
}
class CBeatmap;


class COsuParser
{
public:
    static bool tryParse(const CBeatmap& rIn, NaiSe::BeatSetT& rOut);

};

