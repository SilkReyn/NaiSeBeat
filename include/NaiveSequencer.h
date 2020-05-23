#pragma once


namespace NaiSe {

struct BeatSetT;


enum class Difficulty_t { easy, normal, hard, extra, special };


class CBeatTranslator
{
    bool mAvailableStages[5]{};

    BeatSetT loadFile(const char* fullpath) const;

public:
    // Generic Single-pass translation
    void convertFile(const char* fullpath, uint8_t stage=0u) const;

    bool appendFile(const char* fullpath, Difficulty_t stage);
    void translate();
    void clear();
};

} // namespace