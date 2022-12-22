#ifndef slic3r_Plater_hpp_
#define slic3r_Plater_hpp_

#include <memory>
#include <vector>
#include <boost/filesystem/path.hpp>

#include <wx/panel.h>
// BBS
#include <wx/notebook.h>

#include "Selection.hpp"

#include "libslic3r/enum_bitmask.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "Jobs/Job.hpp"
#include "Search.hpp"
#include "PartPlate.hpp"
#include "GUI_App.hpp"
#include "Jobs/PrintJob.hpp"
#include "Jobs/SendJob.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PrintBase.hpp"

#define FILAMENT_SYSTEM_COLORS_NUM      16

class wxButton;
class ScalableButton;
class wxScrolledWindow;
class wxString;
class ComboBox;
class Button;

namespace Slic3r {

class BuildVolume;
class Model;
class ModelObject;
enum class ModelObjectCutAttribute : int;
using ModelObjectCutAttributes = enum_bitmask<ModelObjectCutAttribute>;
class ModelInstance;
class Print;
class SLAPrint;
//BBS: add partplatelist and SlicingStatusEvent
class PartPlateList;
class SlicingStatusEvent;
enum SLAPrintObjectStep : unsigned int;
enum class ConversionType : int;
class Ams;

using ModelInstancePtrs = std::vector<ModelInstance*>;

namespace UndoRedo {
    class Stack;
    enum class SnapshotType : unsigned char;
    struct Snapshot;
}

namespace GUI {

class MainFrame;
class ConfigOptionsGroup;
class ObjectSettings;
class ObjectList;
class GLCanvas3D;
class Mouse3DController;
class NotificationManager;
struct Camera;
class GLToolbar;
class PlaterPresetComboBox;
class PartPlateList;

using t_optgroups = std::vector <std::shared_ptr<ConfigOptionsGroup>>;

class Plater;
enum class ActionButtonType : int;

#define EVT_PUBLISHING_START        1
#define EVT_PUBLISHING_STOP         2

//BBS: add EVT_SLICING_UPDATE declare here
wxDECLARE_EVENT(EVT_SLICING_UPDATE, Slic3r::SlicingStatusEvent);
wxDECLARE_EVENT(EVT_PUBLISH,        wxCommandEvent);
wxDECLARE_EVENT(EVT_REPAIR_MODEL,        wxCommandEvent);
wxDECLARE_EVENT(EVT_FILAMENT_COLOR_CHANGED,        wxCommandEvent);
wxDECLARE_EVENT(EVT_INSTALL_PLUGIN_NETWORKING,        wxCommandEvent);
wxDECLARE_EVENT(EVT_INSTALL_PLUGIN_HINT,        wxCommandEvent);
wxDECLARE_EVENT(EVT_UPDATE_PLUGINS_WHEN_LAUNCH,        wxCommandEvent);
wxDECLARE_EVENT(EVT_PREVIEW_ONLY_MODE_HINT,        wxCommandEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_COLOR_MODE_CHANGED,   SimpleEvent);


const wxString DEFAULT_PROJECT_NAME = "Untitled";

class Sidebar : public wxPanel
{
    ConfigOptionMode    m_mode;
public:
    Sidebar(Plater *parent);
    Sidebar(Sidebar &&) = delete;
    Sidebar(const Sidebar &) = delete;
    Sidebar &operator=(Sidebar &&) = delete;
    Sidebar &operator=(const Sidebar &) = delete;
    ~Sidebar();

    void init_filament_combo(PlaterPresetComboBox **combo, const int filament_idx);
    void remove_unused_filament_combos(const size_t current_extruder_count);
    void update_all_preset_comboboxes();
    //void update_partplate(PartPlateList& list);
    void update_presets(Slic3r::Preset::Type preset_type);
    //BBS
    void update_presets_from_to(Slic3r::Preset::Type preset_type, std::string from, std::string to);

    void change_top_border_for_mode_sizer(bool increase_border);
    void msw_rescale();
    void sys_color_changed();
    void search();
    void jump_to_option(size_t selected);
    void jump_to_option(const std::string& opt_key, Preset::Type type, const std::wstring& category);
    // BBS. Add on_filaments_change() method.
    void on_filaments_change(size_t num_filaments);
    // BBS
    void on_bed_type_change(BedType bed_type);
    void load_ams_list(std::map<std::string, Ams *> const &list);
    void sync_ams_list();

    ObjectList*             obj_list();
    ObjectSettings*         obj_settings();
    wxPanel*                scrolled_panel();
    wxPanel* print_panel();
    wxPanel* filament_panel();

    ConfigOptionsGroup*     og_freq_chng_params(const bool is_fff);
    wxButton*               get_wiping_dialog_button();

    // BBS
    void                    enable_buttons(bool enable);
    void                    set_btn_label(const ActionButtonType btn_type, const wxString& label) const;
    bool                    show_reslice(bool show) const;
	bool                    show_export(bool show) const;
	bool                    show_send(bool show) const;
    bool                    show_eject(bool show)const;
	bool                    show_export_removable(bool show) const;
	bool                    get_eject_shown() const;
    bool                    is_multifilament();
    void                    update_mode();
    bool                    is_collapsed();
    void                    collapse(bool collapse);
    void                    update_searcher();
    void                    update_ui_from_settings();
	bool                    show_object_list(bool show) const;
    void                    finish_param_edit();

#ifdef _MSW_DARK_MODE
    void                    show_mode_sizer(bool show);
#endif

    std::vector<PlaterPresetComboBox*>&   combos_filament();
    Search::OptionsSearcher&        get_searcher();
    std::string&                    get_search_line();

private:
    struct priv;
    std::unique_ptr<priv> p;

    wxBoxSizer* m_scrolled_sizer = nullptr;
    ComboBox* m_bed_type_list = nullptr;
    ScalableButton* connection_btn = nullptr;
    ScalableButton* ams_btn = nullptr;
};

class Plater: public wxPanel
{
public:
    using fs_path = boost::filesystem::path;

    Plater(wxWindow *parent, MainFrame *main_frame);
    Plater(Plater &&) = delete;
    Plater(const Plater &) = delete;
    Plater &operator=(Plater &&) = delete;
    Plater &operator=(const Plater &) = delete;
    ~Plater() = default;

    bool Show(bool show = true);

    bool is_project_dirty() const;
    bool is_presets_dirty() const;
    void update_project_dirty_from_presets();
    int  save_project_if_dirty(const wxString& reason);
    void reset_project_dirty_after_save();
    void reset_project_dirty_initial_presets();
#if ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW
    void render_project_state_debug_window() const;
#endif // ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW

    Sidebar& sidebar();
    const Model& model() const;
    Model& model();
    const Print& fff_print() const;
    Print& fff_print();
    const SLAPrint& sla_print() const;
    SLAPrint& sla_print();

    int new_project(bool skip_confirm = false, bool silent = false);
    // BBS: save & backup
    void load_project(wxString const & filename = "", wxString const & originfile = "-");
    int save_project(bool saveAs = false);
    //BBS download project by project id
    void import_model_id(const std::string& download_info);
    void download_project(const wxString& project_id);
    void request_model_download(std::string url, std::string filename);
    void request_download_project(std::string project_id);
    // BBS: check snapshot
    bool up_to_date(bool saved, bool backup);

    bool open_3mf_file(const fs::path &file_path);
    int  get_3mf_file_count(std::vector<fs::path> paths);
    void add_file();
    void add_model(bool imperial_units = false);
    void import_sl1_archive();
    void extract_config_from_project();
    void load_gcode();
    void load_gcode(const wxString& filename);
    void reload_gcode_from_disk();
    void refresh_print();

    //BBS: add only gcode mode
    bool only_gcode_mode() { return m_only_gcode; }
    void set_only_gcode(bool only_gcode) { m_only_gcode = only_gcode; }

    //BBS: add only gcode mode
    bool using_exported_file() { return m_exported_file; }
    void set_using_exported_file(bool exported_file) {
        m_exported_file = exported_file;
    }

    // BBS
    wxString get_project_name();
    void update_all_plate_thumbnails(bool force_update = false);
    void invalid_all_plate_thumbnails();
    void force_update_all_plate_thumbnails();
    //BBS static functions that update extruder params and speed table
    static void setPrintSpeedTable(Slic3r::GlobalSpeedMap& printSpeedMap);
    static void setExtruderParams(std::map<size_t, Slic3r::ExtruderParams>& extParas);
    static wxColour get_next_color_for_filament();
    static wxString get_slice_warning_string(GCodeProcessorResult::SliceWarning& warning);

    // BBS: restore
    std::vector<size_t> load_files(const std::vector<boost::filesystem::path>& input_files, LoadStrategy strategy = LoadStrategy::LoadModel | LoadStrategy::LoadConfig,  bool ask_multi = false);
    // To be called when providing a list of files to the GUI slic3r on command line.
    std::vector<size_t> load_files(const std::vector<std::string>& input_files, LoadStrategy strategy = LoadStrategy::LoadModel | LoadStrategy::LoadConfig,  bool ask_multi = false);
    // to be called on drag and drop
    bool load_files(const wxArrayString& filenames);

    const wxString& get_last_loaded_gcode() const { return m_last_loaded_gcode; }

    void update();
    //BBS
    void object_list_changed();
    void stop_jobs();
    bool is_any_job_running() const;
    void select_view(const std::string& direction);
    //BBS: add no_slice logic
    void select_view_3D(const std::string& name, bool no_slice = true);

    bool is_preview_shown() const;
    bool is_preview_loaded() const;
    bool is_view3D_shown() const;

    bool are_view3D_labels_shown() const;
    void show_view3D_labels(bool show);

    bool is_sidebar_collapsed() const;
    void collapse_sidebar(bool show);

    // Called after the Preferences dialog is closed and the program settings are saved.
    // Update the UI based on the current preferences.
    void update_ui_from_settings();

    //BBS
    void select_curr_plate_all();
    void remove_curr_plate_all();

    void select_all();
    void deselect_all();
    void remove(size_t obj_idx);
    void reset(bool apply_presets_change = false);
    void reset_with_confirm();
    //BBS: return int for various result
    int close_with_confirm(std::function<bool(bool yes_or_no)> second_check = nullptr); // BBS close project
    //BBS: trigger a restore project event
    void trigger_restore_project(int skip_confirm = 0);
    void delete_object_from_model(size_t obj_idx, bool refresh_immediately = true); // BBS support refresh immediately
    void delete_all_objects_from_model(); //BBS delete all objects from model
    void remove_selected();
    void increase_instances(size_t num = 1);
    void decrease_instances(size_t num = 1);
    void set_number_of_copies(/*size_t num*/);
    void fill_bed_with_instances();
    bool is_selection_empty() const;
    void scale_selection_to_fit_print_volume();
    void convert_unit(ConversionType conv_type);

    // BBS: replace z with plane_points
    void cut(size_t obj_idx, size_t instance_idx, std::array<Vec3d, 4> plane_points, ModelObjectCutAttributes attributes);

    // BBS: segment model with CGAL
    void segment(size_t obj_idx, size_t instance_idx, double smoothing_alpha=0.5, int segment_number=5);
    void merge(size_t obj_idx, std::vector<int>& vol_indeces);

    void send_to_printer(bool isall = false);
    void export_gcode(bool prefer_removable);
    void export_gcode_3mf(bool export_all = false);
    void send_gcode_finish(wxString name);
    void export_core_3mf();
    void export_stl(bool extended = false, bool selection_only = false);
    //BBS: remove amf
    //void export_amf();
    //BBS add extra param for exporting 3mf silence
    // BBS: backup
    int export_3mf(const boost::filesystem::path& output_path = boost::filesystem::path(), SaveStrategy strategy = SaveStrategy::Default, int export_plate_idx = -1, Export3mfProgressFn proFn = nullptr);

    //BBS
    void publish_project();

    void reload_from_disk();
    void replace_with_stl();
    void reload_all_from_disk();
    bool has_toolpaths_to_export() const;
    void export_toolpaths_to_obj() const;
    void reslice();
    void reslice_SLA_supports(const ModelObject &object, bool postpone_error_messages = false);
    void reslice_SLA_hollowing(const ModelObject &object, bool postpone_error_messages = false);
    void reslice_SLA_until_step(SLAPrintObjectStep step, const ModelObject &object, bool postpone_error_messages = false);

    void clear_before_change_mesh(int obj_idx);
    void changed_mesh(int obj_idx);

    void changed_object(int obj_idx);
    void changed_objects(const std::vector<size_t>& object_idxs);
    void schedule_background_process(bool schedule = true);
    bool is_background_process_update_scheduled() const;
    void suppress_background_process(const bool stop_background_process) ;
    /* -1: send current gcode if not specified
     * -2: send all gcode to target machine */
    int send_gcode(int plate_idx = -1, Export3mfProgressFn proFn = nullptr);
    void send_gcode_legacy(int plate_idx = -1, Export3mfProgressFn proFn = nullptr, bool upload_only = false);
    int export_config_3mf(int plate_idx = -1, Export3mfProgressFn proFn = nullptr);
    //BBS jump to nonitor after print job finished
    void print_job_finished(wxCommandEvent &evt);
    void send_job_finished(wxCommandEvent& evt);
    void publish_job_finished(wxCommandEvent& evt);
    void on_change_color_mode(SimpleEvent& evt);
	void eject_drive();

    void take_snapshot(const std::string &snapshot_name);
    //void take_snapshot(const wxString &snapshot_name);
    void take_snapshot(const std::string &snapshot_name, UndoRedo::SnapshotType snapshot_type);
    //void take_snapshot(const wxString &snapshot_name, UndoRedo::SnapshotType snapshot_type);

    void undo();
    void redo();
    void undo_to(int selection);
    void redo_to(int selection);
    bool undo_redo_string_getter(const bool is_undo, int idx, const char** out_text);
    void undo_redo_topmost_string_getter(const bool is_undo, std::string& out_text);
    bool search_string_getter(int idx, const char** label, const char** tooltip);
    // For the memory statistics.
    const Slic3r::UndoRedo::Stack& undo_redo_stack_main() const;
    void clear_undo_redo_stack_main();
    // Enter / leave the Gizmos specific Undo / Redo stack. To be used by the SLA support point editing gizmo.
    void enter_gizmos_stack();
    // BBS: return false if not changed
    bool leave_gizmos_stack();

    void on_filaments_change(size_t extruders_count);
    // BBS
    void on_bed_type_change(BedType bed_type);
    bool update_filament_colors_in_full_config();
    void on_config_change(const DynamicPrintConfig &config);
    void force_filament_colors_update();
    void force_print_bed_update();
    // On activating the parent window.
    void on_activate();
    std::vector<std::string> get_extruder_colors_from_plater_config(const GCodeProcessorResult* const result = nullptr) const;
    std::vector<std::string> get_colors_for_color_print(const GCodeProcessorResult* const result = nullptr) const;

    void update_menus();
    // BBS
    //void show_action_buttons(const bool is_ready_to_slice) const;

    wxString get_project_filename(const wxString& extension = wxEmptyString) const;
    wxString get_export_gcode_filename(const wxString& extension = wxEmptyString, bool only_filename = false, bool export_all = false) const;
    void set_project_filename(const wxString& filename);

    bool is_export_gcode_scheduled() const;

    const Selection& get_selection() const;
    int get_selected_object_idx();
    bool is_single_full_object_selection() const;
    GLCanvas3D* canvas3D();
    const GLCanvas3D * canvas3D() const;
    GLCanvas3D* get_current_canvas3D();
    GLCanvas3D* get_view3D_canvas3D();
    GLCanvas3D* get_preview_canvas3D();
    GLCanvas3D* get_assmeble_canvas3D();

    void arrange();
    void orient();
    void find_new_position(const ModelInstancePtrs  &instances);
    //BBS: add job state related functions
    void set_prepare_state(int state);
    int get_prepare_state();
    //BBS: add print job releated functions
    void get_print_job_data(PrintPrepareData* data);
    int get_print_finished_event();
    int get_send_finished_event();
    int get_publish_finished_event();

    void set_current_canvas_as_dirty();
    void unbind_canvas_event_handlers();
    void reset_canvas_volumes();

    PrinterTechnology   printer_technology() const;
    const DynamicPrintConfig * config() const;
    bool                set_printer_technology(PrinterTechnology printer_technology);

    //BBS
    void cut_selection_to_clipboard();

    void copy_selection_to_clipboard();
    void paste_from_clipboard();
    //BBS: add clone logic
    void clone_selection();
    void center_selection();
    void search(bool plater_is_active, Preset::Type  type, wxWindow *tag, TextInput *etag, wxWindow *stag);
    void mirror(Axis axis);
    void split_object();
    void split_volume();
    void optimize_rotation();

    //BBS:
    void fill_color(int extruder_id);

    bool can_delete() const;
    bool can_delete_all() const;
    bool can_add_model() const;
    bool can_add_plate() const;
    bool can_delete_plate() const;
    bool can_increase_instances() const;
    bool can_decrease_instances() const;
    bool can_set_instance_to_object() const;
    bool can_fix_through_netfabb() const;
    bool can_simplify() const;
    bool can_split_to_objects() const;
    bool can_split_to_volumes() const;
    bool can_arrange() const;
    //BBS
    bool can_cut_to_clipboard() const;
    bool can_layers_editing() const;
    bool can_paste_from_clipboard() const;
    bool can_copy_to_clipboard() const;
    bool can_undo() const;
    bool can_redo() const;
    bool can_reload_from_disk() const;
    bool can_replace_with_stl() const;
    bool can_mirror() const;
    bool can_split(bool to_objects) const;
#if ENABLE_ENHANCED_PRINT_VOLUME_FIT
    bool can_scale_to_print_volume() const;
#endif // ENABLE_ENHANCED_PRINT_VOLUME_FIT

    //BBS:
    bool can_fillcolor() const;
    bool has_assmeble_view() const;

    void msw_rescale();
    void sys_color_changed();

    // BBS
#if 0
    bool init_view_toolbar();
    void enable_view_toolbar(bool enable);
#endif

    bool init_collapse_toolbar();
    void enable_collapse_toolbar(bool enable);

    const Camera& get_camera() const;
    Camera& get_camera();

    //BBS: partplate list related functions
    PartPlateList& get_partplate_list();
    void validate_current_plate(bool& model_fits, bool& validate_error);
    //BBS: select the plate by index
    int select_plate(int plate_index, bool need_slice = false);
    //BBS: update progress result
    void apply_background_progress();
    //BBS: select the plate by hover_id
    int select_plate_by_hover_id(int hover_id, bool right_click = false);
    //BBS: delete the plate, index= -1 means the current plate
    int delete_plate(int plate_index = -1);
    //BBS: select the sliced plate by index
    int select_sliced_plate(int plate_index);
    //BBS: set bed positions
    void set_bed_position(Vec2d& pos);
    //BBS: is the background process slicing currently
    bool is_background_process_slicing() const;
    //BBS: update slicing context
    void update_slicing_context_to_current_partplate();
    //BBS: show object info
    void show_object_info();
    //BBS
    bool show_publish_dialog(bool show = true);
    //BBS: post process string object exception strings by warning types
    void post_process_string_object_exception(StringObjectException &err);

#if ENABLE_ENVIRONMENT_MAP
    void init_environment_texture();
    unsigned int get_environment_texture_id() const;
#endif // ENABLE_ENVIRONMENT_MAP

    const BuildVolume& build_volume() const;

    // BBS
    //const GLToolbar& get_view_toolbar() const;
    //GLToolbar& get_view_toolbar();

    const GLToolbar& get_collapse_toolbar() const;
    GLToolbar& get_collapse_toolbar();

    void update_preview_bottom_toolbar();
    void update_preview_moves_slider();
    void enable_preview_moves_slider(bool enable);

#if 0
    void update_partplate();
#endif

    void reset_gcode_toolpaths();
    void reset_last_loaded_gcode() { m_last_loaded_gcode = ""; }

    const Mouse3DController& get_mouse3d_controller() const;
    Mouse3DController& get_mouse3d_controller();

    //BBS: add bed exclude area
	void set_bed_shape() const;
    void set_bed_shape(const Pointfs& shape, const Pointfs& exclude_area, const double printable_height, const std::string& custom_texture, const std::string& custom_model, bool force_as_custom = false) const;

	const NotificationManager* get_notification_manager() const;
	NotificationManager* get_notification_manager();
    //BBS: show message in status bar
    void show_status_message(std::string s);

    void init_notification_manager();

    void bring_instance_forward();

    // ROII wrapper for suppressing the Undo / Redo snapshot to be taken.
	class SuppressSnapshots
	{
	public:
		SuppressSnapshots(Plater *plater) : m_plater(plater)
		{
			m_plater->suppress_snapshots();
		}
		~SuppressSnapshots()
		{
			m_plater->allow_snapshots();
		}
	private:
		Plater *m_plater;
	};

    // RAII wrapper for taking an Undo / Redo snapshot while disabling the snapshot taking by the methods called from inside this snapshot.
	class TakeSnapshot
	{
	public:
        TakeSnapshot(Plater *plater, const std::string &snapshot_name) : m_plater(plater)
        {
			m_plater->take_snapshot(snapshot_name);
			m_plater->suppress_snapshots();
		}
		/*TakeSnapshot(Plater *plater, const wxString &snapshot_name) : m_plater(plater)
		{
			m_plater->take_snapshot(snapshot_name);
			m_plater->suppress_snapshots();
		}*/
        TakeSnapshot(Plater* plater, const std::string& snapshot_name, UndoRedo::SnapshotType snapshot_type) : m_plater(plater)
        {
            m_plater->take_snapshot(snapshot_name, snapshot_type);
            m_plater->suppress_snapshots();
        }
        /*TakeSnapshot(Plater *plater, const wxString &snapshot_name, UndoRedo::SnapshotType snapshot_type) : m_plater(plater)
        {
            m_plater->take_snapshot(snapshot_name, snapshot_type);
            m_plater->suppress_snapshots();
        }*/

		~TakeSnapshot()
		{
			m_plater->allow_snapshots();
		}
	private:
		Plater *m_plater;
	};

    // BBS: limit to single snapshot taking by the methods called from inside
    // this snapshot.
    class SingleSnapshot
    {
    public:
        SingleSnapshot(Plater *plater) : m_plater(plater)
        {
            m_plater->single_snapshots_enter(this);
        }

        ~SingleSnapshot() { m_plater->single_snapshots_leave(this); }

        bool check(bool modify)
        {
            if (token && (this->modify || !modify)) return false;
            token = true;
            this->modify = modify;
            return true;
        }

    private:
        Plater *m_plater;
        bool    token = false;
        bool    modify = false;
    };

    bool inside_snapshot_capture();

    void toggle_render_statistic_dialog();
    bool is_render_statistic_dialog_visible() const;

    void toggle_show_wireframe();
    bool is_show_wireframe() const;
    void enable_wireframe(bool status);
    bool is_wireframe_enabled() const;

	// Wrapper around wxWindow::PopupMenu to suppress error messages popping out while tracking the popup menu.
	bool PopupMenu(wxMenu *menu, const wxPoint& pos = wxDefaultPosition);
    bool PopupMenu(wxMenu *menu, int x, int y) { return this->PopupMenu(menu, wxPoint(x, y)); }

    //BBS: add popup logic for table object
    bool PopupObjectTable(int object_id, int volume_id, const wxPoint& position);
    //BBS: popup selection at default position
    bool PopupObjectTableBySelection();

    // get same Plater/ObjectList menus
    wxMenu* plate_menu();
    wxMenu* object_menu();
    wxMenu* part_menu();
    wxMenu* sla_object_menu();
    wxMenu* default_menu();
    wxMenu* instance_menu();
    wxMenu* layer_menu();
    wxMenu* multi_selection_menu();

    static bool has_illegal_filename_characters(const wxString& name);
    static bool has_illegal_filename_characters(const std::string& name);
    static void show_illegal_characters_warning(wxWindow* parent);

    std::string get_preview_only_filename() { return m_preview_only_filename; };

private:
    struct priv;
    std::unique_ptr<priv> p;

    // Set true during PopupMenu() tracking to suppress immediate error message boxes.
    // The error messages are collected to m_tracking_popup_menu_error_message instead and these error messages
    // are shown after the pop-up dialog closes.
    bool 	 m_tracking_popup_menu = false;
    wxString m_tracking_popup_menu_error_message;

    wxString m_last_loaded_gcode;
    //BBS: add only gcode mode
    bool m_only_gcode { false };
    bool m_exported_file { false };
    bool skip_thumbnail_invalid { false };
    std::string m_preview_only_filename;
    int m_valid_plates_count { 0 };

    void suppress_snapshots();
    void allow_snapshots();
    // BBS: single snapshot
    void single_snapshots_enter(SingleSnapshot *single);
    void single_snapshots_leave(SingleSnapshot *single);
    // BBS: add project slice related functions
    int start_next_slice();

    friend class SuppressBackgroundProcessingUpdate;
};

class SuppressBackgroundProcessingUpdate
{
public:
    SuppressBackgroundProcessingUpdate();
    ~SuppressBackgroundProcessingUpdate();
private:
    bool m_was_scheduled;
};

} // namespace GUI
} // namespace Slic3r

#endif
