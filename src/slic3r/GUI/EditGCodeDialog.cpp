#include "EditGCodeDialog.hpp"

#include <vector>
#include <string>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/wupdlock.h>

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "format.hpp"
#include "Tab.hpp"
#include "wxExtensions.hpp"
#include "BitmapCache.hpp"
#include "ExtraRenderers.hpp"
#include "MsgDialog.hpp"
#include "Plater.hpp"

#include "libslic3r/PlaceholderParser.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/Print.hpp"

#define BTN_GAP  FromDIP(20)
#define BTN_SIZE wxSize(FromDIP(58), FromDIP(24))

namespace Slic3r {
namespace GUI {

//------------------------------------------
//          EditGCodeDialog
//------------------------------------------

EditGCodeDialog::EditGCodeDialog(wxWindow* parent, const std::string& key, const std::string& value) :
    DPIDialog(parent, wxID_ANY, format_wxstr(_L("Edit Custom G-code (%1%)"), key), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    SetFont(wxGetApp().normal_font());
    SetBackgroundColour(*wxWHITE);
    wxGetApp().UpdateDarkUI(this);
    wxGetApp().UpdateDlgDarkUI(this);

    int border = 10;
    int em = em_unit();

    wxStaticText* label_top = new wxStaticText(this, wxID_ANY, _L("Built-in placeholders (Double click item to add to G-code)") + ":");

    auto* grid_sizer = new wxFlexGridSizer(1, 3, 5, 15);
    grid_sizer->SetFlexibleDirection(wxBOTH);

    m_params_list = new ParamsViewCtrl(this, wxSize(em * 45, em * 70));
    m_params_list->SetFont(wxGetApp().code_font());
    wxGetApp().UpdateDarkUI(m_params_list);

    m_add_btn = new ScalableButton(this, wxID_ANY, "add_copies");
    m_add_btn->SetToolTip(_L("Add selected placeholder to G-code"));

    m_gcode_editor = new wxTextCtrl(this, wxID_ANY, value, wxDefaultPosition, wxSize(em * 75, em * 70), wxTE_MULTILINE
#ifdef _WIN32
    | wxBORDER_SIMPLE
#endif
    );
    m_gcode_editor->SetFont(wxGetApp().code_font());
    m_gcode_editor->SetInsertionPointEnd();
    wxGetApp().UpdateDarkUI(m_gcode_editor);

    grid_sizer->Add(m_params_list,  1, wxEXPAND);
    grid_sizer->Add(m_add_btn,      0, wxALIGN_CENTER_VERTICAL);
    grid_sizer->Add(m_gcode_editor, 2, wxEXPAND);

    grid_sizer->AddGrowableRow(0, 1);
    grid_sizer->AddGrowableCol(0, 1);
    grid_sizer->AddGrowableCol(2, 1);

    m_param_label = new wxStaticText(this, wxID_ANY, _L("Select placeholder"));
    m_param_label->SetFont(wxGetApp().bold_font());

    m_param_description = new wxStaticText(this, wxID_ANY, wxEmptyString);

    //Orca: use custom buttons
    auto btn_sizer = create_btn_sizer(wxOK | wxCANCEL);
    for(auto btn : m_button_list)
        wxGetApp().UpdateDarkUI(btn.second);

    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    topSizer->Add(label_top           , 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(grid_sizer          , 1, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(m_param_label       , 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(m_param_description , 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(btn_sizer                , 0, wxEXPAND | wxALL, border);

    SetSizer(topSizer);
    topSizer->SetSizeHints(this);

    this->Fit();
    this->Layout();

    this->CenterOnScreen();

    init_params_list(key);
    bind_list_and_button();
}

EditGCodeDialog::~EditGCodeDialog()
{
    // To avoid redundant process of wxEVT_DATAVIEW_SELECTION_CHANGED after dialog distroing (on Linux)
    // unbind this event from params_list
    m_params_list->Unbind(wxEVT_DATAVIEW_SELECTION_CHANGED, &EditGCodeDialog::selection_changed, this);
}

std::string EditGCodeDialog::get_edited_gcode() const
{
    return into_u8(m_gcode_editor->GetValue());
}

static ParamType get_type(const std::string& opt_key, const ConfigOptionDef& opt_def)
{
    return opt_def.is_scalar() ? ParamType::Scalar : ParamType::Vector;
}

void EditGCodeDialog::init_params_list(const std::string& custom_gcode_name)
{
    const auto& custom_gcode_placeholders = custom_gcode_specific_placeholders();
    const auto& specific_params = custom_gcode_placeholders.count(custom_gcode_name) > 0 ?
                                  custom_gcode_placeholders.at(custom_gcode_name) : t_config_option_keys({});

    // Add slicing states placeholders

    wxDataViewItem slicing_state = m_params_list->AppendGroup(_L("[Global] Slicing State"), "custom-gcode_slicing-state_global");
    if (!cgp_ro_slicing_states_config_def.empty()) {
        wxDataViewItem read_only = m_params_list->AppendSubGroup(slicing_state, _L("Read Only"), "lock_closed");
        for (const auto& [opt_key, def]: cgp_ro_slicing_states_config_def.options)
            m_params_list->AppendParam(read_only, get_type(opt_key, def), opt_key);
    }

    if (!cgp_rw_slicing_states_config_def.empty()) {
        wxDataViewItem read_write = m_params_list->AppendSubGroup(slicing_state, _L("Read Write"), "lock_open");
        for (const auto& [opt_key, def] : cgp_rw_slicing_states_config_def.options)
            m_params_list->AppendParam(read_write, get_type(opt_key, def), opt_key);
    }

    // add other universal params, which are related to slicing state
    if (!cgp_other_slicing_states_config_def.empty()) {
        slicing_state = m_params_list->AppendGroup(_L("Slicing State"), "custom-gcode_slicing-state");
        for (const auto& [opt_key, def] : cgp_other_slicing_states_config_def.options)
            m_params_list->AppendParam(slicing_state, get_type(opt_key, def), opt_key);
    }

    // Add universal placeholders

    {
        // Add print statistics subgroup

        if (!cgp_print_statistics_config_def.empty()) {
            wxDataViewItem statistics = m_params_list->AppendGroup(_L("Print Statistics"), "custom-gcode_stats");
            for (const auto& [opt_key, def] : cgp_print_statistics_config_def.options)
                m_params_list->AppendParam(statistics, get_type(opt_key, def), opt_key);
        }

        // Add objects info subgroup

        if (!cgp_objects_info_config_def.empty()) {
            wxDataViewItem objects_info = m_params_list->AppendGroup(_L("Objects Info"), "custom-gcode_object-info");
            for (const auto& [opt_key, def] : cgp_objects_info_config_def.options)
                m_params_list->AppendParam(objects_info, get_type(opt_key, def), opt_key);
        }

        // Add  dimensions subgroup

        if (!cgp_dimensions_config_def.empty()) {
            wxDataViewItem dimensions = m_params_list->AppendGroup(_L("Dimensions"), "custom-gcode_measure");
            for (const auto& [opt_key, def] : cgp_dimensions_config_def.options)
                m_params_list->AppendParam(dimensions, get_type(opt_key, def), opt_key);
        }

        // Add timestamp subgroup

        if (!cgp_timestamps_config_def.empty()) {
            wxDataViewItem dimensions = m_params_list->AppendGroup(_L("Timestamps"), "print-time");
            for (const auto& [opt_key, def] : cgp_timestamps_config_def.options)
                m_params_list->AppendParam(dimensions, get_type(opt_key, def), opt_key);
        }
    }

    // Add specific placeholders

    if (!specific_params.empty()) {
        wxDataViewItem group = m_params_list->AppendGroup(format_wxstr(_L("Specific for %1%"), custom_gcode_name), "custom-gcode_gcode");
        for (const auto& opt_key : specific_params)
            if (custom_gcode_specific_config_def.has(opt_key)) {
                auto def = custom_gcode_specific_config_def.get(opt_key);
                m_params_list->AppendParam(group, get_type(opt_key, *def), opt_key);
            }
        m_params_list->Expand(group);
    }

    // Add placeholders from presets

    wxDataViewItem presets = add_presets_placeholders();
    // add other params which are related to presets
    if (!cgp_other_presets_config_def.empty())
        for (const auto& [opt_key, def] : cgp_other_presets_config_def.options)
            m_params_list->AppendParam(presets, get_type(opt_key, def), opt_key);
}

wxDataViewItem EditGCodeDialog::add_presets_placeholders()
{
    auto get_set_from_vec = [](const std::vector<std::string>&vec) {
        return std::set<std::string>(vec.begin(), vec.end());
    };

    const bool                  is_fff           = wxGetApp().plater()->printer_technology() == ptFFF;
    const std::set<std::string> print_options    = get_set_from_vec(is_fff ? Preset::print_options()    : Preset::sla_print_options());
    const std::set<std::string> material_options = get_set_from_vec(is_fff ? Preset::filament_options() : Preset::sla_material_options());
    const std::set<std::string> printer_options  = get_set_from_vec(is_fff ? Preset::printer_options()  : Preset::sla_printer_options());

    const auto&full_config = wxGetApp().preset_bundle->full_config();

    wxDataViewItem group = m_params_list->AppendGroup(_L("Presets"), "cog");

    wxDataViewItem print = m_params_list->AppendSubGroup(group, _L("Print settings"), "cog");
    for (const auto&opt : print_options)
        if (const ConfigOption *optptr = full_config.optptr(opt))
            m_params_list->AppendParam(print, optptr->is_scalar() ? ParamType::Scalar : ParamType::Vector, opt);

    wxDataViewItem material = m_params_list->AppendSubGroup(group, _(is_fff ? L("Filament settings") : L("SLA Materials settings")), is_fff ? "filament" : "resin");
    for (const auto&opt : material_options)
        if (const ConfigOption *optptr = full_config.optptr(opt))
            m_params_list->AppendParam(material, optptr->is_scalar() ? ParamType::Scalar : ParamType::FilamentVector, opt);

    wxDataViewItem printer = m_params_list->AppendSubGroup(group, _L("Printer settings"), is_fff ? "printer" : "sla_printer");
    for (const auto&opt : printer_options)
        if (const ConfigOption *optptr = full_config.optptr(opt))
            m_params_list->AppendParam(printer, optptr->is_scalar() ? ParamType::Scalar : ParamType::Vector, opt);

    return group;
}

void EditGCodeDialog::add_selected_value_to_gcode()
{
    const wxString val = m_params_list->GetSelectedValue();
    if (val.IsEmpty())
        return;

    const long pos = m_gcode_editor->GetInsertionPoint();
    m_gcode_editor->WriteText(m_gcode_editor->GetInsertionPoint() == m_gcode_editor->GetLastPosition() ? "\n" + val : val);

    if (val.Last() == ']') {
        const long new_pos = m_gcode_editor->GetInsertionPoint();
        if (val[val.Len() - 2] == '[')
            m_gcode_editor->SetInsertionPoint(new_pos - 1);          // set cursor into brackets
        else
            m_gcode_editor->SetSelection(new_pos - 17, new_pos - 1); // select "current_extruder"
    }

    m_gcode_editor->SetFocus();
}

void EditGCodeDialog::selection_changed(wxDataViewEvent& evt)
{
    wxString label;
    wxString description;

    const std::string opt_key = m_params_list->GetSelectedParamKey();
    if (!opt_key.empty()) {
        const ConfigOptionDef*    def     { nullptr };

        const auto& full_config = wxGetApp().preset_bundle->full_config();
        if (const ConfigDef* config_def = full_config.def(); config_def && config_def->has(opt_key)) {
            def = config_def->get(opt_key);
        }
        else {
            for (const ConfigDef* config: std::initializer_list<const ConfigDef*> {
                    &custom_gcode_specific_config_def,
                    &cgp_ro_slicing_states_config_def,
                    &cgp_rw_slicing_states_config_def,
                    &cgp_other_slicing_states_config_def,
                    &cgp_print_statistics_config_def,
                    &cgp_objects_info_config_def,
                    &cgp_dimensions_config_def,
                    &cgp_timestamps_config_def,
                    &cgp_other_presets_config_def
            }) {
                if (config->has(opt_key)) {
                    def = config->get(opt_key);
                    break;
                }
            }
        }

            if (def) {
                const ConfigOptionType scalar_type = def->is_scalar() ? def->type : static_cast<ConfigOptionType>(def->type - coVectorType);
                wxString type_str = scalar_type == coNone           ? "none" :
                                                     scalar_type == coFloat          ? "float" :
                                                     scalar_type == coInt            ? "integer" :
                                                     scalar_type == coString         ? "string" :
                                                     scalar_type == coPercent        ? "percent" :
                                                     scalar_type == coFloatOrPercent ? "float or percent" :
                                                     scalar_type == coPoint          ? "point" :
                                                     scalar_type == coBool           ? "bool" :
                                                     scalar_type == coEnum           ? "enum" : "undef";
                if (!def->is_scalar())
                    type_str += "[]";

                label = (!def || (def->full_label.empty() && def->label.empty()) ) ? format_wxstr("%1%\n(%2%)", opt_key, type_str) :
                        (!def->full_label.empty() && !def->label.empty() ) ?
                                                                                    format_wxstr("%1% > %2%\n(%3%)", _(def->full_label), _(def->label), type_str) :
                                                                                    format_wxstr("%1%\n(%2%)", def->label.empty() ? _(def->full_label) : _(def->label), type_str);

                if (def)
                    description = get_wraped_wxString(_(def->tooltip), 120);
            }
            else
                label = "Undef optptr";
    }

    m_param_label->SetLabel(label);
    m_param_description->SetLabel(description);

    Layout();
}

void EditGCodeDialog::bind_list_and_button()
{
    m_params_list->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, &EditGCodeDialog::selection_changed, this);

    m_params_list->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, [this](wxDataViewEvent& ) {
        add_selected_value_to_gcode();
    });

    m_add_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        add_selected_value_to_gcode();
    });
}

void EditGCodeDialog::on_dpi_changed(const wxRect&suggested_rect)
{
    const int& em = em_unit();

    //Orca: use custom buttons
    for (auto button_item : m_button_list)
    {
        if (button_item.first == wxOK) {
            button_item.second->SetMinSize(BTN_SIZE);
            button_item.second->SetCornerRadius(FromDIP(12));
        }
        if (button_item.first == wxCANCEL) {
            button_item.second->SetMinSize(BTN_SIZE);
            button_item.second->SetCornerRadius(FromDIP(12));
        }
    }

    const wxSize& size = wxSize(45 * em, 35 * em);
    SetMinSize(size);

    Fit();
    Refresh();
}

void EditGCodeDialog::on_sys_color_changed()
{
    m_add_btn->sys_color_changed();
}

//Orca
wxBoxSizer* EditGCodeDialog::create_btn_sizer(long flags)
{
    auto btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    btn_sizer->AddStretchSpacer();

    StateColor ok_btn_bg(
        std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal)
    );

    StateColor ok_btn_bd(
        std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal)
    );

    StateColor ok_btn_text(
        std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Normal)
    );

    StateColor cancel_btn_bg(
        std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal)
    );

    StateColor cancel_btn_bd_(
        std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Normal)
    );

    StateColor cancel_btn_text(
        std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Normal)
    );


    StateColor calc_btn_bg(
        std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal)
    );

    StateColor calc_btn_bd(
        std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal)
    );

    StateColor calc_btn_text(
        std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Normal)
    );

    if (flags & wxOK) {
        Button* ok_btn = new Button(this, _L("OK"));
        ok_btn->SetMinSize(BTN_SIZE);
        ok_btn->SetCornerRadius(FromDIP(12));
        ok_btn->SetBackgroundColor(ok_btn_bg);
        ok_btn->SetBorderColor(ok_btn_bd);
        ok_btn->SetTextColor(ok_btn_text);
        ok_btn->SetFocus();
        ok_btn->SetId(wxID_OK);
        btn_sizer->Add(ok_btn, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, BTN_GAP);
        m_button_list[wxOK] = ok_btn;
    }
    if (flags & wxCANCEL) {
        Button* cancel_btn = new Button(this, _L("Cancel"));
        cancel_btn->SetMinSize(BTN_SIZE);
        cancel_btn->SetCornerRadius(FromDIP(12));
        cancel_btn->SetBackgroundColor(cancel_btn_bg);
        cancel_btn->SetBorderColor(cancel_btn_bd_);
        cancel_btn->SetTextColor(cancel_btn_text);
        cancel_btn->SetId(wxID_CANCEL);
        btn_sizer->Add(cancel_btn, 0, wxRIGHT | wxALIGN_CENTER_VERTICAL, BTN_GAP / 2);
        m_button_list[wxCANCEL] = cancel_btn;
    }

    return btn_sizer;
}


const std::map<ParamType, std::string> ParamsInfo {
//    Type                      BitmapName
    { ParamType::Scalar,        "custom-gcode_single"          },
    { ParamType::Vector,        "custom-gcode_vector"          },
    { ParamType::FilamentVector,"custom-gcode_vector-index" },
};

static void make_bold(wxString& str)
{
#if defined(SUPPORTS_MARKUP) && !defined(__APPLE__)
    str = format_wxstr("<b>%1%</b>", str);
#endif
}

// ----------------------------------------------------------------------------
//                  ParamsModelNode: a node inside ParamsModel
// ----------------------------------------------------------------------------

ParamsNode::ParamsNode(const wxString& group_name, const std::string& icon_name)
: icon_name(icon_name)
, text(group_name)
{
    make_bold(text);
}

ParamsNode::ParamsNode( ParamsNode *        parent,
                        const wxString&     sub_group_name,
                        const std::string&  icon_name)
    : m_parent(parent)
    , icon_name(icon_name)
    , text(sub_group_name)
{
    make_bold(text);
}

ParamsNode::ParamsNode( ParamsNode*         parent,
                        ParamType           param_type,
                        const std::string&  param_key)
    : m_parent(parent)
    , m_param_type(param_type)
    , m_container(false)
    , param_key(param_key)
{
    text = from_u8(param_key);
    if (param_type == ParamType::Vector)
        text += "[]";
    else if (param_type == ParamType::FilamentVector)
        text += "[current_extruder]";

    icon_name = ParamsInfo.at(param_type);
}


// ----------------------------------------------------------------------------
//                  ParamsModel
// ----------------------------------------------------------------------------

ParamsModel::ParamsModel()
{
}

wxDataViewItem ParamsModel::AppendGroup(const wxString&    group_name,
                                        const std::string& icon_name)
{
    m_group_nodes.emplace_back(std::make_unique<ParamsNode>(group_name, icon_name));

    wxDataViewItem parent(nullptr);
    wxDataViewItem child((void*)m_group_nodes.back().get());

    ItemAdded(parent, child);
    m_ctrl->Expand(parent);
    return child;
}

wxDataViewItem ParamsModel::AppendSubGroup(wxDataViewItem       parent,
                                           const wxString&      sub_group_name,
                                           const std::string&   icon_name)
{
    ParamsNode* parent_node = static_cast<ParamsNode*>(parent.GetID());
    if (!parent_node)
        return wxDataViewItem(0);

    parent_node->Append(std::make_unique<ParamsNode>(parent_node, sub_group_name, icon_name));
    const wxDataViewItem  sub_group_item((void*)parent_node->GetChildren().back().get());

    ItemAdded(parent, sub_group_item);
    return sub_group_item;
}

wxDataViewItem ParamsModel::AppendParam(wxDataViewItem      parent,
                                        ParamType           param_type,
                                        const std::string&  param_key)
{
    ParamsNode* parent_node = static_cast<ParamsNode*>(parent.GetID());
    if (!parent_node)
        return wxDataViewItem(0);

    parent_node->Append(std::make_unique<ParamsNode>(parent_node, param_type, param_key));

    const wxDataViewItem  child_item((void*)parent_node->GetChildren().back().get());

    ItemAdded(parent, child_item);
    return child_item;
}

wxString ParamsModel::GetParamName(wxDataViewItem item)
{
    if (item.IsOk()) {
        ParamsNode* node = static_cast<ParamsNode*>(item.GetID());
        if (node->IsParamNode())
            return node->text;
    }
    return wxEmptyString;
}

std::string ParamsModel::GetParamKey(wxDataViewItem item)
{
    if (item.IsOk()) {
        ParamsNode* node = static_cast<ParamsNode*>(item.GetID());
        return node->param_key;
    }
    return std::string();
}

wxDataViewItem ParamsModel::Delete(const wxDataViewItem& item)
{
    auto ret_item = wxDataViewItem(nullptr);
    ParamsNode* node = static_cast<ParamsNode*>(item.GetID());
    if (!node)      // happens if item.IsOk()==false
        return ret_item;

    // first remove the node from the parent's array of children;
    // NOTE: m_group_nodes is only a vector of _pointers_
    //       thus removing the node from it doesn't result in freeing it
    ParamsNodePtrArray& children = node->GetChildren();
    // Delete all children
    while (!children.empty())
        Delete(wxDataViewItem(children.back().get()));

    auto node_parent = node->GetParent();

    ParamsNodePtrArray& parents_children = node_parent ? node_parent->GetChildren() : m_group_nodes;
    auto it = find_if(parents_children.begin(), parents_children.end(),
                                                   [node](std::unique_ptr<ParamsNode>& child) { return child.get() == node; });
    assert(it != parents_children.end());
    it = parents_children.erase(it);

    if (it != parents_children.end())
        ret_item = wxDataViewItem(it->get());

    wxDataViewItem parent(node_parent);
    // set m_container to FALSE if parent has no child
    if (node_parent) {
#ifndef __WXGTK__
        if (node_parent->GetChildren().empty())
            node_parent->SetContainer(false);
#endif //__WXGTK__
        ret_item = parent;
    }

    // notify control
    ItemDeleted(parent, item);
    return ret_item;
}

void ParamsModel::Clear()
{
    while (!m_group_nodes.empty())
        Delete(wxDataViewItem(m_group_nodes.back().get()));
}

void ParamsModel::GetValue(wxVariant& variant, const wxDataViewItem& item, unsigned int col) const
{
    assert(item.IsOk());

    ParamsNode* node = static_cast<ParamsNode*>(item.GetID());
    if (col == (unsigned int)0)
#ifdef __linux__
        variant << wxDataViewIconText(node->text, get_bmp_bundle(node->icon_name)->GetIconFor(m_ctrl->GetParent()));
#else
        variant << DataViewBitmapText(node->text, get_bmp_bundle(node->icon_name)->GetBitmapFor(m_ctrl->GetParent()));
#endif //__linux__
    else
        wxLogError("DiffModel::GetValue: wrong column %d", col);
}

bool ParamsModel::SetValue(const wxVariant& variant, const wxDataViewItem& item, unsigned int col)
{
    assert(item.IsOk());

    ParamsNode* node = static_cast<ParamsNode*>(item.GetID());
    if (col == (unsigned int)0) {
#ifdef __linux__
        wxDataViewIconText data;
        data << variant;
        node->icon = data.GetIcon();
#else
        DataViewBitmapText data;
        data << variant;
        node->icon = data.GetBitmap();
#endif
        node->text = data.GetText();
        return true;
    }

    wxLogError("DiffModel::SetValue: wrong column");
    return false;
}

wxDataViewItem ParamsModel::GetParent(const wxDataViewItem&item) const
{
    // the invisible root node has no parent
    if (!item.IsOk())
        return wxDataViewItem(nullptr);

    ParamsNode* node = static_cast<ParamsNode*>(item.GetID());

    if (node->IsGroupNode())
        return wxDataViewItem(nullptr);

    return wxDataViewItem((void*)node->GetParent());
}

bool ParamsModel::IsContainer(const wxDataViewItem& item) const
{
    // the invisble root node can have children
    if (!item.IsOk())
        return true;

    ParamsNode* node = static_cast<ParamsNode*>(item.GetID());
    return node->IsContainer();
}
unsigned int ParamsModel::GetChildren(const wxDataViewItem& parent, wxDataViewItemArray& array) const
{
    ParamsNode* parent_node = (ParamsNode*)parent.GetID();

    if (parent_node == nullptr) {
        for (const auto& group : m_group_nodes)
            array.Add(wxDataViewItem((void*)group.get()));
    }
    else  {
        const ParamsNodePtrArray& children = parent_node->GetChildren();
        for (const std::unique_ptr<ParamsNode>& child : children)
            array.Add(wxDataViewItem((void*)child.get()));
    }

    return array.Count();
}


// ----------------------------------------------------------------------------
//                  ParamsViewCtrl
// ----------------------------------------------------------------------------

ParamsViewCtrl::ParamsViewCtrl(wxWindow *parent, wxSize size)
    : wxDataViewCtrl(parent, wxID_ANY, wxDefaultPosition, size, wxDV_SINGLE | wxDV_NO_HEADER// | wxDV_ROW_LINES
#ifdef _WIN32
        | wxBORDER_SIMPLE
#endif
    ),
    m_em_unit(em_unit(parent))
{
    wxGetApp().UpdateDVCDarkUI(this);

    model = new ParamsModel();
    this->AssociateModel(model);
    model->SetAssociatedControl(this);

#ifdef __linux__
    wxDataViewIconTextRenderer* rd = new wxDataViewIconTextRenderer();
#ifdef SUPPORTS_MARKUP
    rd->EnableMarkup(true);
#endif
    wxDataViewColumn* column = new wxDataViewColumn("", rd, 0, 20 * m_em_unit, wxALIGN_TOP, wxDATAVIEW_COL_RESIZABLE | wxDATAVIEW_CELL_INERT);
#else
    wxDataViewColumn* column = new wxDataViewColumn("", new BitmapTextRenderer(true, wxDATAVIEW_CELL_INERT), 0, 20 * m_em_unit, wxALIGN_TOP, wxDATAVIEW_COL_RESIZABLE);
#endif //__linux__
    this->AppendColumn(column);
    this->SetExpanderColumn(column);
}

wxDataViewItem ParamsViewCtrl::AppendGroup(const wxString& group_name, const std::string& icon_name)
{
    return model->AppendGroup(group_name, icon_name);
}

wxDataViewItem ParamsViewCtrl::AppendSubGroup(  wxDataViewItem      parent,
                                                const wxString&     sub_group_name,
                                                const std::string&  icon_name)
{
    return model->AppendSubGroup(parent, sub_group_name, icon_name);
}

wxDataViewItem ParamsViewCtrl::AppendParam( wxDataViewItem      parent,
                                            ParamType           param_type,
                                            const std::string&  param_key)
{
    return model->AppendParam(parent, param_type, param_key);
}

wxString ParamsViewCtrl::GetValue(wxDataViewItem item)
{
    return model->GetParamName(item);
}

wxString ParamsViewCtrl::GetSelectedValue()
{
    return model->GetParamName(this->GetSelection());
}

std::string ParamsViewCtrl::GetSelectedParamKey()
{
    return model->GetParamKey(this->GetSelection());
}

void ParamsViewCtrl::CheckAndDeleteIfEmpty(wxDataViewItem item)
{
    wxDataViewItemArray children;
    model->GetChildren(item, children);
    if (children.IsEmpty())
        model->Delete(item);
}

void ParamsViewCtrl::Clear()
{
    model->Clear();
}

void ParamsViewCtrl::Rescale(int em/* = 0*/)
{
//    model->Rescale();
    Refresh();
}
}}    // namespace Slic3r::GUI
