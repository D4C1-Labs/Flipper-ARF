#pragma once

#include "SelectCategoryScreen.hpp"
#include "app/AppConfig.hpp"
#include "app/pager/PagerReceiver.hpp"
#include "lib/String.hpp"
#include "lib/hardware/subghz/SubGhzModule.hpp"
#include "lib/hardware/subghz/ModulationManager.hpp"
#include "lib/hardware/subghz/HopBandManager.hpp"
#include "lib/ui/UiManager.hpp"
#include "lib/ui/view/VariableItemListUiView.hpp"

class SettingsScreen {
private:
    AppConfig* config;
    SubGhzModule* subghz;
    PagerReceiver* receiver;
    VariableItemListUiView* varItemList;

    UiVariableItem* currentCategoryItem;
    UiVariableItem* scanModeItem;
    UiVariableItem* frequencyItem;
    UiVariableItem* hopBandItem;
    UiVariableItem* hopDwellItem;
    UiVariableItem* modulationItem;
    UiVariableItem* maxPagerItem;
    UiVariableItem* signalRepeatItem;
    UiVariableItem* ignoreSavedItem;
    UiVariableItem* autosaveFoundItem;
    UiVariableItem* debugModeItem = NULL;

    String frequencyStr;
    String hopDwellStr;
    String maxPagerStr;
    String signalRepeatStr;
    bool updateUserCategory;
    uint32_t categoryItemIndex;

    // Dwell time options (ms) offered for hopping.
    static const uint32_t* GetDwellOptions(uint8_t* countOut) {
        static const uint32_t dwellOptions[] = {100, 200, 300, 500, 750, 1000};
        *countOut = 6;
        return dwellOptions;
    }

public:
    SettingsScreen(AppConfig* config, PagerReceiver* receiver, SubGhzModule* subghz, bool updateUserCategory) {
        this->config = config;
        this->receiver = receiver;
        this->subghz = subghz;
        this->updateUserCategory = updateUserCategory;

        varItemList = new VariableItemListUiView();
        varItemList->SetOnDestroyHandler(HANDLER(&SettingsScreen::destroy));
        varItemList->SetEnterPressHandler(HANDLER_1ARG(&SettingsScreen::enterPressHandler));

        categoryItemIndex = varItemList->AddItem(
            currentCategoryItem = new UiVariableItem("Category", HANDLER_1ARG(&SettingsScreen::categoryChangedHandler))
        );

        varItemList->AddItem(
            scanModeItem = new UiVariableItem(
                "Scan mode",
                config->AutoHop ? 1 : 0,
                2,
                [this](uint8_t val) {
                    this->config->AutoHop = (val != 0);
                    return this->config->AutoHop ? "Auto hop" : "Manual";
                }
            )
        );

        varItemList->AddItem(
            frequencyItem = new UiVariableItem(
                "Scan frequency",
                FrequencyManager::GetInstance()->GetFrequencyIndex(config->Frequency),
                FrequencyManager::GetInstance()->GetFrequencyCount(),
                [this](uint8_t val) {
                    uint32_t freq = this->config->Frequency = FrequencyManager::GetInstance()->GetFrequency(val);
                    this->subghz->SetReceiveFrequency(this->config->Frequency);
                    return frequencyStr.format("%lu.%02lu", freq / 1000000, (freq % 1000000) / 10000);
                }
            )
        );

        varItemList->AddItem(
            hopBandItem = new UiVariableItem(
                "Hop band",
                config->HopBandMode,
                HopBandManager::GetBandCount(),
                [this](uint8_t val) {
                    this->config->HopBandMode = val;
                    return HopBandManager::GetBandName((HopBand)val);
                }
            )
        );

        varItemList->AddItem(
            hopDwellItem = new UiVariableItem(
                "Hop dwell (ms)",
                dwellIndexForValue(config->HopDwellMs),
                dwellOptionCount(),
                [this](uint8_t val) {
                    uint8_t count = 0;
                    const uint32_t* opts = GetDwellOptions(&count);
                    if(val >= count) val = count - 1;
                    this->config->HopDwellMs = opts[val];
                    return hopDwellStr.fromInt(this->config->HopDwellMs);
                }
            )
        );

        varItemList->AddItem(
            modulationItem = new UiVariableItem(
                "Modulation",
                config->ModulationIndex,
                ModulationManager::GetCount(),
                [this](uint8_t val) {
                    this->config->ModulationIndex = val;
                    this->subghz->SetPreset(ModulationManager::GetPreset(val));
                    return ModulationManager::GetName(val);
                }
            )
        );

        varItemList->AddItem(
            maxPagerItem = new UiVariableItem(
                "Max pager value",
                config->MaxPagerForBatchOrDetection - 1,
                UINT8_MAX,
                [this](uint8_t val) {
                    this->config->MaxPagerForBatchOrDetection = val + 1;
                    return maxPagerStr.fromInt(this->config->MaxPagerForBatchOrDetection);
                }
            )
        );

        varItemList->AddItem(
            signalRepeatItem = new UiVariableItem(
                "Times to repeat signal",
                config->SignalRepeats - 1,
                UINT8_MAX,
                [this](uint8_t val) {
                    this->config->SignalRepeats = val + 1;
                    return signalRepeatStr.fromInt(this->config->SignalRepeats);
                }
            )
        );

        varItemList->AddItem(
            ignoreSavedItem = new UiVariableItem(
                "Saved stations",
                config->SavedStrategy,
                SavedStationStrategyValuesCount,
                [this](uint8_t val) {
                    this->config->SavedStrategy = static_cast<enum SavedStationStrategy>(val);
                    return savedStationsStrategy(this->config->SavedStrategy);
                }
            )
        );

        varItemList->AddItem(
            autosaveFoundItem = new UiVariableItem(
                "Autosave found signals",
                config->AutosaveFoundSignals,
                2,
                [this](uint8_t val) {
                    this->config->AutosaveFoundSignals = val;
                    return boolOption(val);
                }
            )
        );
    }

    UiView* GetView() {
        return varItemList;
    }

private:
    void enterPressHandler(uint32_t index) {
        if(index != categoryItemIndex) {
            return;
        }
        UiManager::GetInstance()->PushView(
            (new SelectCategoryScreen(false, User, HANDLER_2ARG(&SettingsScreen::categorySelected)))->GetView()
        );
    }

    void categorySelected(CategoryType, const char* category) {
        if(config->CurrentUserCategory != NULL) {
            delete config->CurrentUserCategory;
        }
        config->CurrentUserCategory = category != NULL ? new String("%s", category) : NULL;
        UiManager::GetInstance()->PopView(false);
        currentCategoryItem->Refresh();
    }

    const char* categoryChangedHandler(uint8_t) {
        const char* category = config->GetCurrentUserCategoryCstr();
        if(category == NULL) {
            category = "Default";
        }
        return category;
    }

    const char* boolOption(uint8_t value) {
        return value ? "ON" : "OFF";
    }

    uint8_t dwellOptionCount() {
        uint8_t count = 0;
        GetDwellOptions(&count);
        return count;
    }

    uint8_t dwellIndexForValue(uint32_t value) {
        uint8_t count = 0;
        const uint32_t* opts = GetDwellOptions(&count);
        for(uint8_t i = 0; i < count; i++) {
            if(opts[i] == value) {
                return i;
            }
        }
        return 2; // default to 300ms
    }

    const char* savedStationsStrategy(SavedStationStrategy value) {
        switch(value) {
        case IGNORE:
            return "Ignore";

        case SHOW_NAME:
            return "Show name";

        case HIDE:
            return "Hide";

        default:
            return NULL;
        }
    }

    void destroy() {
        config->Save();
        if(updateUserCategory) {
            receiver->SetUserCategory(config->CurrentUserCategory);
            receiver->ReloadKnownStations();
        }

        delete currentCategoryItem;
        delete scanModeItem;
        delete frequencyItem;
        delete hopBandItem;
        delete hopDwellItem;
        delete modulationItem;
        delete maxPagerItem;
        delete signalRepeatItem;
        delete ignoreSavedItem;
        delete autosaveFoundItem;
        delete debugModeItem;

        delete this;
    }
};
