#ifndef slic3r_StatusPanel_hpp_
#define slic3r_StatusPanel_hpp_

#include "libslic3r/ProjectTask.hpp"
#include "DeviceManager.hpp"
#include "MonitorPage.hpp"
#include "SliceInfoPanel.hpp"
#include "CameraPopup.hpp"
#include "GUI.hpp"
#include <wx/panel.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/sizer.h>
#include <wx/gbsizer.h>
#include <wx/webrequest.h>
#include "wxMediaCtrl2.h"
#include "MediaPlayCtrl.h"
#include "AMSSetting.hpp"
#include "Calibration.hpp"
#include "PrintOptionsDialog.hpp"
#include "AMSMaterialsSetting.hpp"
#include "ExtrusionCalibration.hpp"
#include "ReleaseNote.hpp"
#include "Widgets/SwitchButton.hpp"
#include "Widgets/AxisCtrlButton.hpp"
#include "Widgets/TextInput.hpp"
#include "Widgets/TempInput.hpp"
#include "Widgets/StaticLine.hpp"
#include "Widgets/ProgressBar.hpp"
#include "Widgets/ImageSwitchButton.hpp"
#include "Widgets/AMSControl.hpp"
#include "Widgets/FanControl.hpp"
#include "HMS.hpp"

class StepIndicator;

#define COMMAND_TIMEOUT_U0      15
#define COMMAND_TIMEOUT         5

namespace Slic3r {
namespace GUI {

enum CameraRecordingStatus {
    RECORDING_NONE,
    RECORDING_OFF_NORMAL,
    RECORDING_OFF_HOVER,
    RECORDING_ON_NORMAL,
    RECORDING_ON_HOVER,
};

enum CameraTimelapseStatus {
    TIMELAPSE_NONE,
    TIMELAPSE_OFF_NORMAL,
    TIMELAPSE_OFF_HOVER,
    TIMELAPSE_ON_NORMAL,
    TIMELAPSE_ON_HOVER,
};

enum PrintingTaskType {
    PRINGINT,
    CALIBRATION,
};

class PrintingTaskPanel : public wxPanel
{
public:
    PrintingTaskPanel(wxWindow* parent, PrintingTaskType type);
    ~PrintingTaskPanel();
    void create_panel(wxWindow* parent);

private:
    MachineObject*  m_obj;
    ScalableBitmap  m_thumbnail_placeholder;
    ScalableBitmap  m_bitmap_use_time;
    ScalableBitmap  m_bitmap_use_weight;

    wxPanel *       m_panel_printing_title;
    wxPanel*        m_staticline;
    wxPanel*        m_panel_error_txt;

    wxBoxSizer*     m_printing_sizer;
    wxStaticText *  m_staticText_printing;
    wxStaticText*   m_staticText_subtask_value;
    wxStaticText*   m_staticText_consumption_of_time;
    wxStaticText*   m_staticText_consumption_of_weight;
    wxStaticText*   m_printing_stage_value;
    wxStaticText*   m_staticText_profile_value;
    wxStaticText*   m_staticText_progress_percent;
    wxStaticText*   m_staticText_progress_percent_icon;
    wxStaticText*   m_staticText_progress_left;
    wxStaticText*   m_staticText_layers;
    wxStaticBitmap* m_bitmap_thumbnail;
    wxStaticBitmap* m_bitmap_static_use_time;
    wxStaticBitmap* m_bitmap_static_use_weight;
    ScalableButton* m_button_pause_resume;
    ScalableButton* m_button_abort;
    Button*         m_button_market_scoring;
    Button*         m_button_clean;

    ProgressBar*    m_gauge_progress;
    Label* m_error_text;
    PrintingTaskType m_type;

public:
    void init_bitmaps();
    void init_scaled_buttons();
    void error_info_reset();
    void show_error_msg(wxString msg);
    void reset_printing_value();
    void msw_rescale();
    
public:
    void enable_pause_resume_button(bool enable, std::string type);
    void enable_abort_button(bool enable);
    void update_subtask_name(wxString name);
    void update_stage_value(wxString stage, int val);
    void update_progress_percent(wxString percent, wxString icon);
    void update_left_time(wxString time);
    void update_left_time(int mc_left_time);
    void update_layers_num(bool show, wxString num = wxEmptyString);
    void show_priting_use_info(bool show, wxString time = wxEmptyString, wxString weight = wxEmptyString);
    void show_profile_info(bool show, wxString profile = wxEmptyString);
    
public:
    ScalableButton* get_abort_button() {return m_button_abort;};
    ScalableButton* get_pause_resume_button() {return m_button_pause_resume;};
    Button* get_market_scoring_button() {return m_button_market_scoring;};
    Button* get_clean_button() {return m_button_clean;};
    wxStaticBitmap* get_bitmap_thumbnail() {return m_bitmap_thumbnail;};
};

class StatusBasePanel : public wxScrolledWindow
{
protected:
    wxBitmap m_item_placeholder;
    ScalableBitmap m_thumbnail_placeholder;
    ScalableBitmap m_thumbnail_brokenimg;
    ScalableBitmap m_thumbnail_sdcard;
    wxBitmap m_bitmap_item_prediction;
    wxBitmap m_bitmap_item_cost;
    wxBitmap m_bitmap_item_print;
    ScalableBitmap m_bitmap_speed;
    ScalableBitmap m_bitmap_speed_active;
    ScalableBitmap m_bitmap_axis_home;
    ScalableBitmap m_bitmap_lamp_on;
    ScalableBitmap m_bitmap_lamp_off;
    ScalableBitmap m_bitmap_fan_on;
    ScalableBitmap m_bitmap_fan_off;
    ScalableBitmap m_bitmap_use_time;
    ScalableBitmap m_bitmap_use_weight;
    wxBitmap m_bitmap_extruder_empty_load;
    wxBitmap m_bitmap_extruder_filled_load;
    wxBitmap m_bitmap_extruder_empty_unload;
    wxBitmap m_bitmap_extruder_filled_unload;

    CameraRecordingStatus m_state_recording{CameraRecordingStatus::RECORDING_NONE};
    CameraTimelapseStatus m_state_timelapse{CameraTimelapseStatus::TIMELAPSE_NONE};


    CameraItem *m_setting_button;

    wxBitmap m_bitmap_camera;
    ScalableBitmap m_bitmap_sdcard_state_normal;
    ScalableBitmap m_bitmap_sdcard_state_abnormal;
    ScalableBitmap m_bitmap_sdcard_state_no;
    ScalableBitmap m_bitmap_recording_on;
    ScalableBitmap m_bitmap_recording_off;
    ScalableBitmap m_bitmap_timelapse_on;
    ScalableBitmap m_bitmap_timelapse_off;
    ScalableBitmap m_bitmap_vcamera_on;
    ScalableBitmap m_bitmap_vcamera_off;

    /* title panel */
    wxPanel *       media_ctrl_panel;
    wxPanel *       m_panel_monitoring_title;
    wxPanel *       m_panel_printing_title;
    wxPanel *       m_panel_control_title;

    wxStaticText*   m_staticText_consumption_of_time;
    wxStaticText *  m_staticText_consumption_of_weight;
    Label *         m_staticText_monitoring;
    wxStaticText *  m_staticText_timelapse;
    SwitchButton *  m_bmToggleBtn_timelapse;

    wxStaticBitmap *m_bitmap_camera_img;
    wxStaticBitmap *m_bitmap_recording_img;
    wxStaticBitmap *m_bitmap_timelapse_img;
    wxStaticBitmap* m_bitmap_vcamera_img;
    wxStaticBitmap *m_bitmap_sdcard_img;
    wxStaticBitmap *m_bitmap_static_use_time;
    wxStaticBitmap *m_bitmap_static_use_weight;


    wxMediaCtrl2 *  m_media_ctrl;
    MediaPlayCtrl * m_media_play_ctrl;

    Label *         m_staticText_printing;
    wxStaticBitmap *m_bitmap_thumbnail;
    wxStaticText *  m_staticText_subtask_value;
    wxStaticText *  m_printing_stage_value;
    wxStaticText *  m_staticText_profile_value;
    ProgressBar*    m_gauge_progress;
    wxStaticText *  m_staticText_progress_percent;
    wxStaticText *  m_staticText_progress_percent_icon;
    wxStaticText *  m_staticText_progress_left;
    wxStaticText *  m_staticText_layers;
    Button *        m_button_report;
    ScalableButton *m_button_pause_resume;
    ScalableButton *m_button_abort;
    Button *        m_button_clean;
    Button *        m_button_market_scoring;

    wxStaticText *  m_text_tasklist_caption;

    Label *  m_staticText_control;
    ImageSwitchButton *m_switch_lamp;
    int               m_switch_lamp_timeout{0};
    ImageSwitchButton *m_switch_speed;

    /* TempInput */
    wxBoxSizer *    m_misc_ctrl_sizer;
    StaticBox*      m_fan_panel; 
    TempInput *     m_tempCtrl_nozzle;
    int             m_temp_nozzle_timeout {0};
    StaticLine *    m_line_nozzle;
    TempInput *     m_tempCtrl_bed;
    int             m_temp_bed_timeout {0};
    TempInput *     m_tempCtrl_frame;
    bool             m_current_support_cham_fan{true};
    FanSwitchButton *m_switch_nozzle_fan;
    int             m_switch_nozzle_fan_timeout{0};
    FanSwitchButton *m_switch_printing_fan;
    int             m_switch_printing_fan_timeout{0};
    FanSwitchButton *m_switch_cham_fan;
    int             m_switch_cham_fan_timeout{0};

    float           m_fixed_aspect_ratio{1.8};

    AxisCtrlButton *m_bpButton_xy;
    //wxStaticText *  m_staticText_xy;
    Button *        m_bpButton_z_10;
    Button *        m_bpButton_z_1;
    Button *        m_bpButton_z_down_1;
    Button *        m_bpButton_z_down_10;
    Button *        m_button_unload;
    wxStaticText *  m_staticText_z_tip;
    wxStaticText *  m_staticText_e;
    Button *        m_bpButton_e_10;
    Button *        m_bpButton_e_down_10;
    StaticLine *    m_temp_extruder_line;
    wxBoxSizer*     m_ams_list;
    wxStaticText *  m_ams_debug;
    bool            m_show_ams_group{false};
    AMSControl*     m_ams_control;
    StaticBox*      m_ams_control_box;
    wxStaticBitmap *m_ams_extruder_img;
    wxStaticBitmap* m_bitmap_extruder_img;
    wxPanel *       m_panel_separator_right;
    wxPanel *       m_panel_separotor_bottom;
    wxGridBagSizer *m_tasklist_info_sizer{nullptr};
    wxBoxSizer *    m_printing_sizer;
    wxBoxSizer *    m_tasklist_sizer;
    wxBoxSizer *    m_tasklist_caption_sizer;
    wxPanel*        m_panel_error_txt;
    wxPanel*        m_staticline;
    Label *         m_error_text;
    wxStaticText*   m_staticText_calibration_caption;
    wxStaticText*   m_staticText_calibration_caption_top;
    wxStaticText*   m_calibration_text;
    Button*         m_options_btn;
    Button*         m_calibration_btn;
    StepIndicator*  m_calibration_flow;

    wxPanel *       m_machine_ctrl_panel;
    PrintingTaskPanel *       m_project_task_panel;

    // Virtual event handlers, override them in your derived class
    virtual void on_subtask_pause_resume(wxCommandEvent &event) { event.Skip(); }
    virtual void on_subtask_abort(wxCommandEvent &event) { event.Skip(); }
    virtual void on_lamp_switch(wxCommandEvent &event) { event.Skip(); }
    virtual void on_bed_temp_kill_focus(wxFocusEvent &event) { event.Skip(); }
    virtual void on_bed_temp_set_focus(wxFocusEvent &event) { event.Skip(); }
    virtual void on_nozzle_temp_kill_focus(wxFocusEvent &event) { event.Skip(); }
    virtual void on_nozzle_temp_set_focus(wxFocusEvent &event) { event.Skip(); }    
    virtual void on_nozzle_fan_switch(wxCommandEvent &event) { event.Skip(); }
    virtual void on_printing_fan_switch(wxCommandEvent &event) { event.Skip(); }
    virtual void on_axis_ctrl_z_up_10(wxCommandEvent &event) { event.Skip(); }
    virtual void on_axis_ctrl_z_up_1(wxCommandEvent &event) { event.Skip(); }
    virtual void on_axis_ctrl_z_down_1(wxCommandEvent &event) { event.Skip(); }
    virtual void on_axis_ctrl_z_down_10(wxCommandEvent &event) { event.Skip(); }
    virtual void on_axis_ctrl_e_up_10(wxCommandEvent &event) { event.Skip(); }
    virtual void on_axis_ctrl_e_down_10(wxCommandEvent &event) { event.Skip(); }

public:
    StatusBasePanel(wxWindow *      parent,
                    wxWindowID      id    = wxID_ANY,
                    const wxPoint & pos   = wxDefaultPosition,
                    const wxSize &  size  = wxDefaultSize,
                    long            style = wxTAB_TRAVERSAL,
                    const wxString &name  = wxEmptyString);

    ~StatusBasePanel();

    void init_bitmaps();
    wxBoxSizer *create_monitoring_page();
    wxBoxSizer *create_machine_control_page(wxWindow *parent);

    wxBoxSizer *create_temp_axis_group(wxWindow *parent);
    wxBoxSizer *create_temp_control(wxWindow *parent);
    wxBoxSizer *create_misc_control(wxWindow *parent);
    wxBoxSizer *create_axis_control(wxWindow *parent);
    wxBoxSizer *create_bed_control(wxWindow *parent);
    wxBoxSizer *create_extruder_control(wxWindow *parent);

    void reset_temp_misc_control();
    int before_error_code = 0;
    int skip_print_error = 0;
    wxBoxSizer *create_ams_group(wxWindow *parent);
    wxBoxSizer *create_settings_group(wxWindow *parent);

    void show_ams_group(bool show = true, bool support_virtual_tray = true, bool support_extrustion_cali = true);
};


class StatusPanel : public StatusBasePanel
{
private:
    friend class MonitorPanel;

protected:
    std::shared_ptr<SliceInfoPopup> m_slice_info_popup;
    std::shared_ptr<ImageTransientPopup> m_image_popup;
    std::shared_ptr<CameraPopup> m_camera_popup;
    std::set<int> rated_model_id;
    AMSSetting *m_ams_setting_dlg{nullptr};
    PrintOptionsDialog*  print_options_dlg { nullptr };
    CalibrationDialog*   calibration_dlg {nullptr};
    AMSMaterialsSetting *m_filament_setting_dlg{nullptr};

    SecondaryCheckDialog* m_print_error_dlg = nullptr;
    SecondaryCheckDialog* abort_dlg = nullptr;
    SecondaryCheckDialog* con_load_dlg = nullptr;
    SecondaryCheckDialog* ctrl_e_hint_dlg = nullptr;
    SecondaryCheckDialog* sdcard_hint_dlg = nullptr;

    FanControlPopup* m_fan_control_popup{nullptr};

    ExtrusionCalibration *m_extrusion_cali_dlg{nullptr};

    wxString     m_request_url;
    bool         m_start_loading_thumbnail = false;
    bool         m_load_sdcard_thumbnail = false;
    int          m_last_sdcard    = -1;
    int          m_last_recording = -1;
    int          m_last_timelapse = -1;
    int          m_last_extrusion = -1;
    int          m_last_vcamera   = -1;
    bool         m_is_load_with_temp = false;
    bool         m_print_finish            = false;

    wxWebRequest web_request;
    bool bed_temp_input    = false;
    bool nozzle_temp_input = false;
    int speed_lvl = 1; // 0 - 3
    int speed_lvl_timeout {0};
    boost::posix_time::ptime speed_dismiss_time;
    bool m_showing_speed_popup = false;
    bool m_show_mode_changed = false;
    std::map<wxString, wxImage> img_list; // key: url, value: wxBitmap png Image
    std::map<std::string, std::string> m_print_connect_types;
    std::vector<Button *>       m_buttons;
    int last_status;
    void init_scaled_buttons();
    void create_tasklist_info();
    void show_task_list_info(bool show = true);
    void update_tasklist_info();

    void on_market_scoring(wxCommandEvent &event);
    void on_subtask_pause_resume(wxCommandEvent &event);
    void on_subtask_abort(wxCommandEvent &event);
    void on_print_error_clean(wxCommandEvent &event);
    void show_error_message(MachineObject* obj, wxString msg, std::string print_error_str = "");
    void error_info_reset();
    void show_recenter_dialog();
    void market_model_scoring_page(int design_id);

    /* axis control */
    bool check_axis_z_at_home(MachineObject* obj);
    void on_axis_ctrl_xy(wxCommandEvent &event);
    void on_axis_ctrl_z_up_10(wxCommandEvent &event);
    void on_axis_ctrl_z_up_1(wxCommandEvent &event);
    void on_axis_ctrl_z_down_1(wxCommandEvent &event);
    void on_axis_ctrl_z_down_10(wxCommandEvent &event);
    void on_axis_ctrl_e_up_10(wxCommandEvent &event);
    void on_axis_ctrl_e_down_10(wxCommandEvent &event);
    void axis_ctrl_e_hint(bool up_down);

	void on_start_unload(wxCommandEvent &event);
    /* temp control */
    void on_bed_temp_kill_focus(wxFocusEvent &event);
    void on_bed_temp_set_focus(wxFocusEvent &event);
    void on_set_bed_temp();
    void on_nozzle_temp_kill_focus(wxFocusEvent &event);
    void on_nozzle_temp_set_focus(wxFocusEvent &event);
    void on_set_nozzle_temp();

    /* extruder apis */
    void on_ams_load(SimpleEvent &event);
    void on_ams_load_curr();
    void on_ams_unload(SimpleEvent &event);
    void on_ams_filament_backup(SimpleEvent& event);
    void on_ams_setting_click(SimpleEvent& event);
    void on_filament_edit(wxCommandEvent &event);
    void on_ext_spool_edit(wxCommandEvent &event);
    void on_filament_extrusion_cali(wxCommandEvent &event);
    void on_ams_refresh_rfid(wxCommandEvent &event);
    void on_ams_selected(wxCommandEvent &event);
    void on_ams_guide(wxCommandEvent &event);
    void on_ams_retry(wxCommandEvent &event);
    void on_print_error_func(wxCommandEvent& event);

    void on_fan_changed(wxCommandEvent& event);
    void on_switch_speed(wxCommandEvent& event);
    void on_lamp_switch(wxCommandEvent &event);
    void on_printing_fan_switch(wxCommandEvent &event);
    void on_nozzle_fan_switch(wxCommandEvent &event);
    void on_thumbnail_enter(wxMouseEvent &event);
    void on_thumbnail_leave(wxMouseEvent &event);
    void refresh_thumbnail_webrequest(wxMouseEvent& event);
    void on_switch_vcamera(wxMouseEvent &event);
    void on_camera_enter(wxMouseEvent &event);
    void on_camera_leave(wxMouseEvent& event);
    void on_auto_leveling(wxCommandEvent &event);
    void on_xyz_abs(wxCommandEvent &event);

    /* print options */
    void on_show_print_options(wxCommandEvent &event);

    /* calibration */
    void on_start_calibration(wxCommandEvent &event);


    /* update apis */
    void update(MachineObject* obj);
    void show_printing_status(bool ctrl_area = true, bool temp_area = true);
    void update_left_time(int mc_left_time);
    void update_basic_print_data(bool def = false);
    void update_model_info();
    void update_subtask(MachineObject* obj);
    void update_cloud_subtask(MachineObject *obj);
    void update_sdcard_subtask(MachineObject *obj);
    void update_temp_ctrl(MachineObject *obj);
    void update_misc_ctrl(MachineObject *obj);
    void update_ams(MachineObject* obj);
    void update_extruder_status(MachineObject* obj);
    void update_ams_control_state(bool is_support_virtual_tray, bool is_curr_tray_selected);
    void update_cali(MachineObject* obj);

    void reset_printing_values();
    void on_webrequest_state(wxWebRequestEvent &evt);
    bool is_task_changed(MachineObject* obj);

    /* camera */
    void update_camera_state(MachineObject* obj);
    bool show_vcamera = false;

public:
    void update_error_message();

public:
    StatusPanel(wxWindow *      parent,
                wxWindowID      id    = wxID_ANY,
                const wxPoint & pos   = wxDefaultPosition,
                const wxSize  & size  = wxDefaultSize,
                long            style = wxTAB_TRAVERSAL,
                const wxString &name  = wxEmptyString);
    ~StatusPanel();

    enum ThumbnailState {
        PLACE_HOLDER = 0,
        BROKEN_IMG = 1,
        TASK_THUMBNAIL = 2,
        SDCARD_THUMBNAIL = 3,
        STATE_COUNT = 4
    };

    MachineObject *obj {nullptr};
    BBLSubTask *   last_subtask{nullptr};
    std::string    last_profile_id;
    std::string    last_task_id;
    long           last_tray_exist_bits { -1 };
    long           last_ams_exist_bits { -1 };
    long           last_tray_is_bbl_bits{ -1 };
    long           last_read_done_bits{ -1 };
    long           last_reading_bits { -1 };
    long           last_ams_version { -1 };
    int            last_cali_version{-1};

    enum ThumbnailState task_thumbnail_state {ThumbnailState::PLACE_HOLDER};
    std::vector<int> last_stage_list_info;

    bool is_stage_list_info_changed(MachineObject* obj);

    void set_default();
    void show_status(int status);
    void set_print_finish_status(bool is_finish);
    void set_hold_count(int& count);

    void rescale_camera_icons();
    void on_sys_color_changed();
    void msw_rescale();
};
}
}
#endif
