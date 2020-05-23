#include <iostream>

#include <NaiveSequencer.h>
#include "util/Options.hpp"


enum class argOpts_t : int
{
    OPT_HELP, OPT_FILE_EZ, OPT_FILE_NM, OPT_FILE_HD, OPT_FILE_EX, OPT_FILE_SP, OPT_FILE_XX,
    OPT_UNKNOWN, OPT_NOOPT, OPT_DASH, OPT_LDASH, OPT_DONE
};

static const char* const USAGE = "[-e|n|h|x|s|r<0-4> path ] [...]\nProviding no options will create a loose map";
static const nih::Parameter<argOpts_t> PARAM_DEF[]
{
    { argOpts_t::OPT_HELP,    '?', "help",    "", "Show command hints." },
    { argOpts_t::OPT_FILE_EZ, 'e', "easy",    "path", "Convert single beatmap and store as easy difficulty." },
    { argOpts_t::OPT_FILE_NM, 'n', "normal",  "path", "Convert single beatmap and store as normal difficulty." },
    { argOpts_t::OPT_FILE_HD, 'h', "hard",    "path", "Convert single beatmap and store as hard difficulty." },
    { argOpts_t::OPT_FILE_EX, 'x', "extra",   "path", "Convert single beatmap and store as extra hard difficulty." },
    { argOpts_t::OPT_FILE_SP, 's', "special", "path", "Convert single beatmap and store as special difficulty." },
    { argOpts_t::OPT_FILE_XX, 'r', "rank",    "level,path", "Convert beatmap as part of a beatset and store as rank 'level' difficulty.\nNote: Will create a new index file\nExample: -r1 demoA.osu -r3 demoB.osu" }
};


int main(int argc, char** argv)
{
    int iarg{};
    argOpts_t opt;
    NaiSe::CBeatTranslator bt;

    auto fArgs = nih::make_Options(argc, argv, USAGE, PARAM_DEF);
    do
    {
        switch (opt = fArgs())
        {
        case argOpts_t::OPT_HELP:
            std::cout << fArgs.usage();
            break;

        //--> without file index
        case argOpts_t::OPT_NOOPT:
            bt.convertFile(fArgs[1]);
            break;

        case argOpts_t::OPT_FILE_EZ:
            bt.convertFile(fArgs[1], 1u);
            break;

        case argOpts_t::OPT_FILE_NM:
            bt.convertFile(fArgs[1], 3u);
            break;

        case argOpts_t::OPT_FILE_HD:
            bt.convertFile(fArgs[1], 5u);
            break;

        case argOpts_t::OPT_FILE_EX:
            bt.convertFile(fArgs[1], 7u);
            break;

        case argOpts_t::OPT_FILE_SP:
            bt.convertFile(fArgs[1], 9u);
            break;
        //<-- without file index

        case argOpts_t::OPT_FILE_XX:
            try
            {
                iarg = std::stoi(fArgs[1]);
            } catch (std::exception ex) {
                iarg = 0;
            }
            switch (iarg)
            {
            case 0:
                bt.appendFile(fArgs[2], NaiSe::Difficulty_t::easy);
                break;

            case 1:
                bt.appendFile(fArgs[2], NaiSe::Difficulty_t::normal);
                break;

            case 2:
                bt.appendFile(fArgs[2], NaiSe::Difficulty_t::hard);
                break;

            case 3:
                bt.appendFile(fArgs[2], NaiSe::Difficulty_t::extra);
                break;

            case 4:
                bt.appendFile(fArgs[2], NaiSe::Difficulty_t::special);
                break;

            default:
                std::cerr << fArgs[1] << " does not refer to a known difficulty and has been ignored." << std::endl;
                break;
            }
            break;

        case argOpts_t::OPT_DONE:
            bt.translate();  // no effect if nothing in queue or already consumed
            break;

        default:
            std::cerr << "Arguments passed in wrong format. For help, press '?' or \"help\"" << std::endl;
            std::cerr << fArgs.show(0);
            return (int)opt;
        }
    } while (argOpts_t::OPT_DONE != opt);
    
    return (int)opt;
}

