#pragma once

void subghz_scene_start_launch_and_exit(SubGhz* subghz, const char* fap_path, const char* args);

enum SubmenuIndex {
    SubmenuIndexRead = 10,
    SubmenuIndexSaved = 11,
    SubmenuIndexFrequencyAnalyzer = 13,
    SubmenuIndexModulationAnalyzer = 14,
    SubmenuIndexReadRAW = 15,
    SubmenuIndexProtocolList = 17,
};
