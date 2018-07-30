#ifndef slic3r_GUI_ObjectParts_hpp_
#define slic3r_GUI_ObjectParts_hpp_

class wxWindow;
class wxSizer;
class wxBoxSizer;
class wxString;
class wxArrayString;
class wxMenu;

namespace Slic3r {
class ModelObject;
class Model;

namespace GUI {

enum ogGroup{
	ogFrequentlyChangingParameters,
	ogFrequentlyObjectSettings,
	ogCurrentSettings
// 	ogObjectSettings,
// 	ogObjectMovers,
// 	ogPartSettings
};

enum LambdaTypeIDs{
	LambdaTypeBox,
	LambdaTypeCylinder,
	LambdaTypeSphere,
	LambdaTypeSlab
};

struct OBJECT_PARAMETERS
{
	LambdaTypeIDs	type = LambdaTypeBox;
	double			dim[3];// = { 1.0, 1.0, 1.0 };
	int				cyl_r = 1;
	int				cyl_h = 1;
	double			sph_rho = 1.0;
	double			slab_h = 1.0;
	double			slab_z = 0.0;
};

void add_collapsible_panes(wxWindow* parent, wxBoxSizer* sizer);
void add_objects_list(wxWindow* parent, wxBoxSizer* sizer);
void add_object_settings(wxWindow* parent, wxBoxSizer* sizer);
void show_collpane_settings(bool expert_mode);

wxMenu *create_add_settings_popupmenu(bool is_part);
wxMenu *create_add_part_popupmenu();

// Add object to the list
//void add_object(const std::string &name);
void add_object_to_list(const std::string &name, ModelObject* model_object);
// Delete object from the list
void delete_object_from_list();
// Delete all objects from the list
void delete_all_objects_from_list();
// Set count of object on c++ side
void set_object_count(int idx, int count);
// Set object scale on c++ side
void set_object_scale(int idx, int scale);
// Unselect all objects in the list on c++ side
void unselect_objects();
// Select current object in the list on c++ side
void select_current_object(int idx);
// Remove objects/sub-object from the list
void remove();

void object_ctrl_selection_changed();
void object_ctrl_context_menu();

void init_mesh_icons();
void set_event_object_selection_changed(const int& event);
void set_event_object_settings_changed(const int& event); 
void set_event_remove_object(const int& event);
void set_event_update_scene(const int& event);
void set_objects_from_model(Model &model);

bool is_parts_changed();
bool is_part_settings_changed();

void load_part(	wxWindow* parent, ModelObject* model_object, 
				wxArrayString& part_names, const bool is_modifier); 

void load_lambda(wxWindow* parent, ModelObject* model_object, 
				wxArrayString& part_names, const bool is_modifier);

void on_btn_load(wxWindow* parent, bool is_modifier = false, bool is_lambda = false);
void on_btn_del();
void on_btn_split();
void on_btn_move_up();
void on_btn_move_down();

void parts_changed(int obj_idx);
void part_selection_changed();

void update_settings_value();
// show/hide "Extruder" column for Objects List
void set_extruder_column_hidden(bool hide);
// update extruder in current config
void update_extruder_in_config(const wxString& selection);
// update scale values after scale unit changing or "gizmos"
void update_scale_values();
void update_scale_values(const Pointf3& size, float scale);
// update rotation values object selection changing
void update_rotation_values();
// update rotation value after "gizmos"
void update_rotation_value(const double angle, const std::string& axis);
} //namespace GUI
} //namespace Slic3r 
#endif  //slic3r_GUI_ObjectParts_hpp_