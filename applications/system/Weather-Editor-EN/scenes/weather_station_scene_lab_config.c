#include "../weather_station_app_i.h"
#include <stdio.h>
enum { LabConfigTemp = 0, LabConfigHum, LabConfigMax, LabConfigCycle, LabConfigStart };
static void lab_temp_changed(VariableItem* item) {
    WeatherStationApp* app = variable_item_get_context(item); const int32_t c=(int32_t)variable_item_get_current_value_index(item)-50;
    app->lab_target_temperature_tenths=c*10; char text[16]; snprintf(text,sizeof(text),"%ld C",(long)c); variable_item_set_current_value_text(item,text);
}
static void lab_hum_changed(VariableItem* item) {
    WeatherStationApp* app=variable_item_get_context(item); const uint8_t h=variable_item_get_current_value_index(item); app->lab_target_humidity=h;
    char text[16]; snprintf(text,sizeof(text),"%u %%",h); variable_item_set_current_value_text(item,text);
}
static void lab_enter(void* context,uint32_t index) { WeatherStationApp* app=context; if(app && index==LabConfigStart) scene_manager_next_scene(app->scene_manager,WeatherStationSceneLabRun); }
void weather_station_scene_lab_config_on_enter(void* context) {
    WeatherStationApp* app=context; if(!app || !app->lab_unlocked || !weather_station_ensure_variable_item_list(app)) { if(app) scene_manager_previous_scene(app->scene_manager); return; }
    variable_item_list_reset(app->variable_item_list);
    VariableItem* item=variable_item_list_add(app->variable_item_list,"TX temperature",121,lab_temp_changed,app); uint8_t ti=(uint8_t)((app->lab_target_temperature_tenths/10)+50); if(ti>120U)ti=50U; variable_item_set_current_value_index(item,ti); lab_temp_changed(item);
    item=variable_item_list_add(app->variable_item_list,"TX humidity",101,lab_hum_changed,app); uint8_t hi=(app->lab_target_humidity>=0&&app->lab_target_humidity<=100)?(uint8_t)app->lab_target_humidity:0U; variable_item_set_current_value_index(item,hi); lab_hum_changed(item);
    item=variable_item_list_add(app->variable_item_list,"Max sensors",1,NULL,app); variable_item_set_current_value_text(item,"10");
    item=variable_item_list_add(app->variable_item_list,"Cycle",1,NULL,app); variable_item_set_current_value_text(item,"RX 5s / quiet 1s");
    item=variable_item_list_add(app->variable_item_list,"START",1,NULL,app); variable_item_set_current_value_text(item,"OK");
    variable_item_list_set_enter_callback(app->variable_item_list,lab_enter,app); view_dispatcher_switch_to_view(app->view_dispatcher,WeatherStationViewVariableItemList); weather_station_release_inactive_gui_views(app,WeatherStationViewVariableItemList);
}
bool weather_station_scene_lab_config_on_event(void* context,SceneManagerEvent event){UNUSED(context);UNUSED(event);return false;}
void weather_station_scene_lab_config_on_exit(void* context){WeatherStationApp* app=context;if(app&&app->variable_item_list)variable_item_list_reset(app->variable_item_list);}
