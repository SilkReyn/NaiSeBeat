# NaiSeBeat
A command line tool to convert Osu! beatmaps to custom Beat Saber maps. It's task is to read tagged textfiles, translate and adapt content into a BS map editor compatible JSON sequence.

Command line usage:
-------------------
[-e|n|h|x|s|r<0-4> path ] [...] *Providing no options will create a loose map*

Option/Verbatim | Argument | Description
---|---|---
'?'/"help" |  | Show command hints.
'e'/"easy" | "path" | Convert single beatmap and store as easy difficulty.
'n'/"normal" | "path" | Convert single beatmap and store as normal difficulty.
'h'/"hard" | "path" | Convert single beatmap and store as hard difficulty.
'x'/"extra" | "path" | Convert single beatmap and store as extra hard difficulty.
's'/"special" | "path" | Convert single beatmap and store as special difficulty.
'r'/"rank" | "level,path" | Convert beatmap as part of a beatset and store as rank 'level' difficulty. Note: Will create a new index file

> Example: `-r1 demoA.osu -r3 demoB.osu`
