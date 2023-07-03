#ifndef slic3r_GUI_CalibrationWizardPresetPage_hpp_
#define slic3r_GUI_CalibrationWizardPresetPage_hpp_

#include "CalibrationWizardPage.hpp"

namespace Slic3r { namespace GUI {

enum CaliPresetStage {
    CALI_MANULA_STAGE_NONE = 0,
    CALI_MANUAL_STAGE_1,
    CALI_MANUAL_STAGE_2,
};

class CaliPresetCaliStagePanel : public wxPanel
{
public:
    CaliPresetCaliStagePanel(wxWindow* parent,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL);
    void create_panel(wxWindow* parent);

    void set_cali_stage(CaliPresetStage stage, float value);
    void get_cali_stage(CaliPresetStage& stage, float& value);

    void set_flow_ratio_value(wxString flow_ratio);

protected:
    CaliPresetStage m_stage;
    wxBoxSizer*   m_top_sizer;
    wxRadioButton* m_complete_radioBox;
    wxRadioButton* m_fine_radioBox;
    TextInput *    flow_ratio_input;
    float m_flow_ratio_value;
};

class CaliPresetWarningPanel : public wxPanel
{
public:
    CaliPresetWarningPanel(wxWindow* parent,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL);

    void create_panel(wxWindow* parent);

    void set_warning(wxString text);
protected:
    wxBoxSizer*   m_top_sizer;
    wxStaticText* m_warning_text;
};

class CaliPresetTipsPanel : public wxPanel
{
public:
    CaliPresetTipsPanel(wxWindow* parent,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL);

    void create_panel(wxWindow* parent);

    void set_params(int nozzle_temp, int bed_temp, float max_volumetric);
    void get_params(int& nozzle_temp, int& bed_temp, float& max_volumetric);
protected:
    wxBoxSizer*     m_top_sizer;
    TextInput*      m_nozzle_temp;
    TextInput*      m_bed_temp;
    TextInput*      m_max_volumetric_speed;
};

class CaliPresetCustomRangePanel : public wxPanel
{
public:
    CaliPresetCustomRangePanel(wxWindow* parent,
        int input_value_nums = 3,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL);

    void create_panel(wxWindow* parent);

    void set_unit(wxString unit);
    void set_titles(wxArrayString titles);
    void set_values(wxArrayString values);
    wxArrayString get_values();

protected:
    wxBoxSizer*     m_top_sizer;
    int                       m_input_value_nums;
    std::vector<wxStaticText*> m_title_texts;
    std::vector<TextInput*>    m_value_inputs;
};

enum CaliPresetPageStatus
{
    CaliPresetStatusInit = 0,
    CaliPresetStatusNormal,
    CaliPresetStatusSending,
};

class CalibrationPresetPage : public CalibrationWizardPage
{
public:
    CalibrationPresetPage(wxWindow* parent,
        CalibMode cali_mode,
        bool custom_range = false,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL);

    void create_page(wxWindow* parent);

    void update(MachineObject* obj) override;

    void on_device_connected(MachineObject* obj) override;

    void update_print_error_info(int code, const std::string& msg, const std::string& extra);

    void show_send_failed_info(bool show, int code = 0, wxString description = wxEmptyString, wxString extra = wxEmptyString);

    void set_cali_filament_mode(CalibrationFilamentMode mode) override;

    void set_cali_method(CalibrationMethod method) override;

    void on_cali_start_job();

    void on_cali_finished_job();

    void init_with_machine(MachineObject* obj);

    void sync_ams_info(MachineObject* obj);

    void select_default_compatible_filament();

    std::vector<FilamentComboBox*> get_selected_filament_combobox();

    // key is tray_id
    std::map<int, Preset*> get_selected_filaments();

    void get_preset_info(
        float& nozzle_dia,
        BedType& plate_type);

    void get_cali_stage(CaliPresetStage& stage, float& value);

    std::shared_ptr<ProgressIndicator> get_sending_progress_bar() {
        return m_send_progress_bar;
    }

    Preset* get_printer_preset(MachineObject* obj, float nozzle_value);
    Preset* get_print_preset();
    std::string get_print_preset_name();

    wxArrayString get_custom_range_values();

    CaliPresetPageStatus get_page_status() { return m_page_status; }
protected:
    void create_selection_panel(wxWindow* parent);
    void create_filament_list_panel(wxWindow* parent);
    void create_ext_spool_panel(wxWindow* parent);
    void create_sending_panel(wxWindow* parent);

    void init_selection_values();
    void update_filament_combobox(std::string ams_id = "");

    void on_select_nozzle(wxCommandEvent& evt);
    void on_select_plate_type(wxCommandEvent& evt);

    void on_choose_ams(wxCommandEvent& event);
    void on_choose_ext_spool(wxCommandEvent& event);

    void on_select_tray(wxCommandEvent& event);

    void on_switch_ams(std::string ams_id = "");

    void on_recommend_input_value();

    void check_filament_compatible();
    bool is_filaments_compatiable(const std::vector<Preset*>& prests);
    bool is_filaments_compatiable(const std::vector<Preset*>& prests,
        int& bed_temp,
        std::string& incompatiable_filament_name,
        std::string& error_tips);

    void update_combobox_filaments(MachineObject* obj);

    float get_nozzle_value();

    void show_status(CaliPresetPageStatus status);

    CaliPageStepGuide*        m_step_panel { nullptr };
    CaliPresetCaliStagePanel* m_cali_stage_panel { nullptr };
    wxPanel*                  m_selection_panel { nullptr };
    wxPanel*                  m_filament_from_panel { nullptr };
    wxPanel*                  m_multi_ams_panel { nullptr };
    wxPanel*                  m_filament_list_panel { nullptr };
    wxPanel*                  m_ext_spool_panel { nullptr };
    CaliPresetWarningPanel*   m_warning_panel { nullptr };
    CaliPresetCustomRangePanel* m_custom_range_panel { nullptr };
    CaliPresetTipsPanel*      m_tips_panel { nullptr };
    wxPanel*                  m_sending_panel { nullptr };

    wxBoxSizer* m_top_sizer;

    // m_selection_panel widgets
    ComboBox*       m_comboBox_nozzle_dia;
    ComboBox*       m_comboBox_bed_type;
    ComboBox*       m_comboBox_process;
    
    wxRadioButton*      m_ams_radiobox;
    wxRadioButton*      m_ext_spool_radiobox;
    
    ScalableButton*      m_ams_sync_button;
    FilamentComboBoxList m_filament_comboBox_list;
    FilamentComboBox*    m_virtual_tray_comboBox;

    // m_sending panel widgets
    std::shared_ptr<BBLStatusBarSend> m_send_progress_bar;
    wxScrolledWindow*                 m_sw_print_failed_info { nullptr };
    Label*                            m_st_txt_error_code { nullptr };
    Label*                            m_st_txt_error_desc { nullptr };
    Label*                            m_st_txt_extra_info { nullptr };
    int                               m_print_error_code;
    std::string                       m_print_error_msg;
    std::string                       m_print_error_extra;
    
    std::vector<AMSItem*> m_ams_item_list;

    // for update filament combobox, key : tray_id
    std::map<int, DynamicPrintConfig> filament_ams_list;

    CaliPresetPageStatus    m_page_status { CaliPresetPageStatus::CaliPresetStatusInit };

    bool m_show_custom_range { false };

    MachineObject* curr_obj { nullptr };
};

class MaxVolumetricSpeedPresetPage : public CalibrationPresetPage
{
public:
    MaxVolumetricSpeedPresetPage(wxWindow *     parent,
                                 CalibMode      cali_mode,
                                 bool           custom_range = false,
                                 wxWindowID     id           = wxID_ANY,
                                 const wxPoint &pos          = wxDefaultPosition,
                                 const wxSize & size         = wxDefaultSize,
                                 long           style        = wxTAB_TRAVERSAL);
};

}} // namespace Slic3r::GUI

#endif