#include "CalibUtils.hpp"

#include "../GUI/GUI_App.hpp"
#include "../GUI/DeviceManager.hpp"
#include "../GUI/Jobs/ProgressIndicator.hpp"
#include "../GUI/PartPlate.hpp"

#include "libslic3r/Model.hpp"


namespace Slic3r {
namespace GUI {
std::shared_ptr<PrintJob> CalibUtils::print_job;
static const std::string temp_dir = fs::path(fs::temp_directory_path() / "calib").string();
static const std::string temp_gcode_path = temp_dir + "/temp.gcode";
static const std::string path            = temp_dir + "/test.3mf";
static const std::string config_3mf_path = temp_dir + "/test_config.3mf";

static std::string MachineBedTypeString[5] = {
    "auto",
    "pc",
    "ep",
    "pei",
    "pte"
};

static void cut_model(Model &model, std::array<Vec3d, 4> plane_points, ModelObjectCutAttributes attributes)
{
    size_t obj_idx = 0;
    size_t instance_idx = 0;
    if (!attributes.has(ModelObjectCutAttribute::KeepUpper) && !attributes.has(ModelObjectCutAttribute::KeepLower))
        return;

    auto* object = model.objects[0];

    const auto new_objects = object->cut(instance_idx, plane_points, attributes);
    model.delete_object(obj_idx);

    for (ModelObject *model_object : new_objects) {
        auto *object = model.add_object(*model_object);
        object->sort_volumes(true);
        std::string object_name = object->name.empty() ? fs::path(object->input_file).filename().string() : object->name;
        object->ensure_on_bed();
    }
}

static void read_model_from_file(const std::string& input_file, Model& model)
{
    LoadStrategy              strategy = LoadStrategy::LoadModel;
    ConfigSubstitutionContext config_substitutions{ForwardCompatibilitySubstitutionRule::Enable};
    int                       plate_to_slice = 0;

    bool                  is_bbl_3mf;
    Semver                file_version;
    DynamicPrintConfig    config;
    PlateDataPtrs         plate_data_src;
    std::vector<Preset *> project_presets;

    model = Model::read_from_file(input_file, &config, &config_substitutions, strategy, &plate_data_src, &project_presets,
        &is_bbl_3mf, &file_version, nullptr, nullptr, nullptr, nullptr, nullptr, plate_to_slice);

    model.add_default_instances();
    for (auto object : model.objects)
        object->ensure_on_bed();
}

std::array<Vec3d, 4> get_cut_plane_points(const BoundingBoxf3 &bbox, const double &cut_height)
{
    std::array<Vec3d, 4> plane_pts;
    plane_pts[0] = Vec3d(bbox.min(0), bbox.min(1), cut_height);
    plane_pts[1] = Vec3d(bbox.max(0), bbox.min(1), cut_height);
    plane_pts[2] = Vec3d(bbox.max(0), bbox.max(1), cut_height);
    plane_pts[3] = Vec3d(bbox.min(0), bbox.max(1), cut_height);
    return plane_pts;
}

void CalibUtils::calib_PA(const X1CCalibInfos& calib_infos, std::string& error_message)
{
    DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return;

    MachineObject *obj_ = dev->get_selected_machine();
    if (obj_ == nullptr)
        return;

    if (calib_infos.calib_datas.size() > 0)
        obj_->command_start_pa_calibration(calib_infos);
}

void CalibUtils::emit_get_PA_calib_results()
{
    DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return;

    MachineObject *obj_ = dev->get_selected_machine();
    if (obj_ == nullptr)
        return;

    obj_->command_get_pa_calibration_result();
}

bool CalibUtils::get_PA_calib_results(std::vector<PACalibResult>& pa_calib_results)
{
    DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return false;

    MachineObject *obj_ = dev->get_selected_machine();
    if (obj_ == nullptr)
        return false;

    pa_calib_results = obj_->pa_calib_results;
    return pa_calib_results.size() > 0;
}

void CalibUtils::emit_get_PA_calib_infos()
{
    DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return;

    MachineObject *obj_ = dev->get_selected_machine();
    if (obj_ == nullptr)
        return;

    obj_->command_get_pa_calibration_tab();
}

bool CalibUtils::get_PA_calib_tab(std::vector<PACalibResult> &pa_calib_infos)
{
    DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return false;

    MachineObject *obj_ = dev->get_selected_machine();
    if (obj_ == nullptr)
        return false;

    pa_calib_infos = obj_->pa_calib_tab;
    return pa_calib_infos.size() > 0;
}

void CalibUtils::set_PA_calib_result(const std::vector<PACalibResult>& pa_calib_values)
{
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return;

    MachineObject* obj_ = dev->get_selected_machine();
    if (obj_ == nullptr)
        return;

    obj_->command_set_pa_calibration(pa_calib_values);
}

void CalibUtils::select_PA_calib_result(const PACalibIndexInfo& pa_calib_info)
{
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return;

    MachineObject* obj_ = dev->get_selected_machine();
    if (obj_ == nullptr)
        return;

    obj_->commnad_select_pa_calibration(pa_calib_info);
}

void CalibUtils::delete_PA_calib_result(const PACalibIndexInfo& pa_calib_info)
{
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return;

    MachineObject* obj_ = dev->get_selected_machine();
    if (obj_ == nullptr)
        return;

    obj_->command_delete_pa_calibration(pa_calib_info);
}

void CalibUtils::calib_flowrate_X1C(const X1CCalibInfos& calib_infos, std::string& error_message)
{
    DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return;

    MachineObject *obj_ = dev->get_selected_machine();
    if (obj_ == nullptr)
        return;

    if (calib_infos.calib_datas.size() > 0)
        obj_->command_start_flow_ratio_calibration(calib_infos);
}

void CalibUtils::emit_get_flow_ratio_calib_results()
{
    DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return;

    MachineObject *obj_ = dev->get_selected_machine();
    if (obj_ == nullptr)
        return;

    obj_->command_get_flow_ratio_calibration_result();
}

bool CalibUtils::get_flow_ratio_calib_results(std::vector<FlowRatioCalibResult>& flow_ratio_calib_results)
{
    DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return false;

    MachineObject *obj_ = dev->get_selected_machine();
    if (obj_ == nullptr)
        return false;

    flow_ratio_calib_results = obj_->flow_ratio_results;
    return flow_ratio_calib_results.size() > 0;
}

void CalibUtils::calib_flowrate(int pass, const CalibInfo& calib_info, std::string& error_message)
{
    if (pass != 1 && pass != 2)
        return;
    
    Model       model;
    std::string input_file;
    if (pass == 1)
        input_file = Slic3r::resources_dir() + "/calib/filament_flow/flowrate-test-pass1.3mf";
    else
        input_file = Slic3r::resources_dir() + "/calib/filament_flow/flowrate-test-pass2.3mf";

    read_model_from_file(input_file, model);

    DynamicConfig print_config    = calib_info.print_prest->config;
    DynamicConfig filament_config = calib_info.filament_prest->config;
    DynamicConfig printer_config  = calib_info.printer_prest->config;

    /// --- scale ---
    // model is created for a 0.4 nozzle, scale z with nozzle size.
    const ConfigOptionFloats *nozzle_diameter_config = printer_config.option<ConfigOptionFloats>("nozzle_diameter");
    assert(nozzle_diameter_config->values.size() > 0);
    float nozzle_diameter = nozzle_diameter_config->values[0];
    float xyScale         = nozzle_diameter / 0.6;
    // scale z to have 7 layers
    double first_layer_height = print_config.option<ConfigOptionFloat>("initial_layer_print_height")->value;
    double layer_height       = nozzle_diameter / 2.0; // prefer 0.2 layer height for 0.4 nozzle
    first_layer_height        = std::max(first_layer_height, layer_height);

    float zscale = (first_layer_height + 6 * layer_height) / 1.4;
    // only enlarge
    if (xyScale > 1.2) {
        for (auto _obj : model.objects) _obj->scale(xyScale, xyScale, zscale);
    } else {
        for (auto _obj : model.objects) _obj->scale(1, 1, zscale);
    }

    Flow   infill_flow                   = Flow(nozzle_diameter * 1.2f, layer_height, nozzle_diameter);
    double filament_max_volumetric_speed = filament_config.option<ConfigOptionFloats>("filament_max_volumetric_speed")->get_at(0);
    double max_infill_speed              = filament_max_volumetric_speed / (infill_flow.mm3_per_mm() * (pass == 1 ? 1.2 : 1));
    double internal_solid_speed          = std::floor(std::min(print_config.opt_float("internal_solid_infill_speed"), max_infill_speed));
    double top_surface_speed             = std::floor(std::min(print_config.opt_float("top_surface_speed"), max_infill_speed));

    // adjust parameters
    for (auto _obj : model.objects) {
        _obj->ensure_on_bed();
        _obj->config.set_key_value("wall_loops", new ConfigOptionInt(3));
        _obj->config.set_key_value("only_one_wall_top", new ConfigOptionBool(true));
        _obj->config.set_key_value("sparse_infill_density", new ConfigOptionPercent(35));
        _obj->config.set_key_value("bottom_shell_layers", new ConfigOptionInt(1));
        _obj->config.set_key_value("top_shell_layers", new ConfigOptionInt(5));
        _obj->config.set_key_value("detect_thin_wall", new ConfigOptionBool(true));
        _obj->config.set_key_value("filter_out_gap_fill", new ConfigOptionFloat(0));  // SoftFever parameter
        _obj->config.set_key_value("sparse_infill_pattern", new ConfigOptionEnum<InfillPattern>(ipRectilinear));
        _obj->config.set_key_value("top_surface_line_width", new ConfigOptionFloat(nozzle_diameter * 1.2f));
        _obj->config.set_key_value("internal_solid_infill_line_width", new ConfigOptionFloat(nozzle_diameter * 1.2f));
        _obj->config.set_key_value("top_surface_pattern", new ConfigOptionEnum<InfillPattern>(ipMonotonic));
        _obj->config.set_key_value("top_solid_infill_flow_ratio", new ConfigOptionFloat(1.0f));
        _obj->config.set_key_value("infill_direction", new ConfigOptionFloat(45));
        _obj->config.set_key_value("ironing_type", new ConfigOptionEnum<IroningType>(IroningType::NoIroning));
        _obj->config.set_key_value("internal_solid_infill_speed", new ConfigOptionFloat(internal_solid_speed));
        _obj->config.set_key_value("top_surface_speed", new ConfigOptionFloat(top_surface_speed));

        // extract flowrate from name, filename format: flowrate_xxx
        std::string obj_name = _obj->name;
        assert(obj_name.length() > 9);
        obj_name = obj_name.substr(9);
        if (obj_name[0] == 'm') obj_name[0] = '-';
        auto modifier = stof(obj_name);
        _obj->config.set_key_value("print_flow_ratio", new ConfigOptionFloat(1.0f + modifier / 100.f));
    }

    print_config.set_key_value("layer_height", new ConfigOptionFloat(layer_height));
    print_config.set_key_value("initial_layer_print_height", new ConfigOptionFloat(first_layer_height));
    print_config.set_key_value("reduce_crossing_wall", new ConfigOptionBool(true));

    // apply preset
    DynamicPrintConfig full_config;
    full_config.apply(FullPrintConfig::defaults());
    full_config.apply(print_config);
    full_config.apply(filament_config);
    full_config.apply(printer_config);

    Calib_Params params;
    params.mode = CalibMode::Calib_Flow_Rate;
    process_and_store_3mf(&model, full_config, params, error_message);
    if (!error_message.empty())
        return;

    send_to_print(calib_info.dev_id, calib_info.select_ams, calib_info.process_bar, calib_info.bed_type, error_message);
}

void CalibUtils::calib_generic_PA(const CalibInfo &calib_info, std::string &error_message)
{
    const Calib_Params &params = calib_info.params;
    if (params.mode != CalibMode::Calib_PA_Line)
        return;

    Model model;
    std::string input_file = Slic3r::resources_dir() + "/calib/pressure_advance/pressure_advance_test.stl";
    read_model_from_file(input_file, model);

    DynamicPrintConfig print_config    = calib_info.print_prest->config;
    DynamicPrintConfig filament_config = calib_info.filament_prest->config;
    DynamicPrintConfig printer_config  = calib_info.printer_prest->config;

    DynamicPrintConfig full_config;
    full_config.apply(FullPrintConfig::defaults());
    full_config.apply(print_config);
    full_config.apply(filament_config);
    full_config.apply(printer_config);

    process_and_store_3mf(&model, full_config, params, error_message);
    if (!error_message.empty())
        return;

    send_to_print(calib_info.dev_id, calib_info.select_ams, calib_info.process_bar, calib_info.bed_type, error_message);
}

void CalibUtils::calib_temptue(const CalibInfo& calib_info, std::string& error_message)
{
    const Calib_Params &params = calib_info.params;
    if (params.mode != CalibMode::Calib_Temp_Tower)
        return;

    Model                     model;
    std::string               input_file = Slic3r::resources_dir() + "/calib/temperature_tower/temperature_tower.stl";
    read_model_from_file(input_file, model);

    // cut upper
    auto obj_bb      = model.objects[0]->bounding_box();
    auto block_count = lround((350 - params.start) / 5 + 1);
    if (block_count > 0) {
        // add EPSILON offset to avoid cutting at the exact location where the flat surface is
        auto new_height = block_count * 10.0 + EPSILON;
        if (new_height < obj_bb.size().z()) {
            std::array<Vec3d, 4> plane_pts;
            plane_pts[0] = Vec3d(obj_bb.min(0), obj_bb.min(1), new_height);
            plane_pts[1] = Vec3d(obj_bb.max(0), obj_bb.min(1), new_height);
            plane_pts[2] = Vec3d(obj_bb.max(0), obj_bb.max(1), new_height);
            plane_pts[3] = Vec3d(obj_bb.min(0), obj_bb.max(1), new_height);
            cut_model(model, plane_pts, ModelObjectCutAttribute::KeepLower);
        }
    }

    // cut bottom
    obj_bb      = model.objects[0]->bounding_box();
    block_count = lround((350 - params.end) / 5);
    if (block_count > 0) {
        auto new_height = block_count * 10.0 + EPSILON;
        if (new_height < obj_bb.size().z()) {
            std::array<Vec3d, 4> plane_pts;
            plane_pts[0] = Vec3d(obj_bb.min(0), obj_bb.min(1), new_height);
            plane_pts[1] = Vec3d(obj_bb.max(0), obj_bb.min(1), new_height);
            plane_pts[2] = Vec3d(obj_bb.max(0), obj_bb.max(1), new_height);
            plane_pts[3] = Vec3d(obj_bb.min(0), obj_bb.max(1), new_height);
            cut_model(model, plane_pts, ModelObjectCutAttribute::KeepUpper);
        }
    }

    // edit preset
    DynamicPrintConfig print_config    = calib_info.print_prest->config;
    DynamicPrintConfig filament_config = calib_info.filament_prest->config;
    DynamicPrintConfig printer_config  = calib_info.printer_prest->config;

    auto start_temp      = lround(params.start);
    filament_config.set_key_value("nozzle_temperature_initial_layer", new ConfigOptionInts(1, (int) start_temp));
    filament_config.set_key_value("nozzle_temperature", new ConfigOptionInts(1, (int) start_temp));

    model.objects[0]->config.set_key_value("brim_type", new ConfigOptionEnum<BrimType>(btOuterOnly));
    model.objects[0]->config.set_key_value("brim_width", new ConfigOptionFloat(5.0));
    model.objects[0]->config.set_key_value("brim_object_gap", new ConfigOptionFloat(0.0));
    model.objects[0]->config.set_key_value("enable_support", new ConfigOptionBool(false));

    // apply preset
    DynamicPrintConfig full_config;
    full_config.apply(FullPrintConfig::defaults());
    full_config.apply(print_config);
    full_config.apply(filament_config);
    full_config.apply(printer_config);

    process_and_store_3mf(&model, full_config, params, error_message);
    if (!error_message.empty())
        return;

    send_to_print(calib_info.dev_id, calib_info.select_ams, calib_info.process_bar, calib_info.bed_type, error_message);
}

void CalibUtils::calib_max_vol_speed(const CalibInfo& calib_info, std::string& error_message)
{
    const Calib_Params &params = calib_info.params;
    if (params.mode != CalibMode::Calib_Vol_speed_Tower)
        return;

    Model       model;
    std::string input_file = Slic3r::resources_dir() + "/calib/volumetric_speed/SpeedTestStructure.step";
    read_model_from_file(input_file, model);

    DynamicPrintConfig print_config    = calib_info.print_prest->config;
    DynamicPrintConfig filament_config = calib_info.filament_prest->config;
    DynamicPrintConfig printer_config  = calib_info.printer_prest->config;

    auto obj             = model.objects[0];
    auto         bed_shape = printer_config.option<ConfigOptionPoints>("printable_area")->values;
    BoundingBoxf bed_ext   = get_extents(bed_shape);
    auto         scale_obj = (bed_ext.size().x() - 10) / obj->bounding_box().size().x();
    if (scale_obj < 1.0)
        obj->scale(scale_obj, 1, 1);

    const ConfigOptionFloats *nozzle_diameter_config = printer_config.option<ConfigOptionFloats>("nozzle_diameter");
    assert(nozzle_diameter_config->values.size() > 0);
    double nozzle_diameter = nozzle_diameter_config->values[0];
    double line_width      = nozzle_diameter * 1.75;
    double layer_height    = nozzle_diameter * 0.8;

    auto max_lh = printer_config.option<ConfigOptionFloats>("max_layer_height");
    if (max_lh->values[0] < layer_height) max_lh->values[0] = {layer_height};

    filament_config.set_key_value("filament_max_volumetric_speed", new ConfigOptionFloats{50});
    filament_config.set_key_value("slow_down_layer_time", new ConfigOptionInts{0});

    print_config.set_key_value("enable_overhang_speed", new ConfigOptionBool{false});
    print_config.set_key_value("timelapse_type", new ConfigOptionEnum<TimelapseType>(tlTraditional));
    print_config.set_key_value("wall_loops", new ConfigOptionInt(1));
    print_config.set_key_value("top_shell_layers", new ConfigOptionInt(0));
    print_config.set_key_value("bottom_shell_layers", new ConfigOptionInt(1));
    print_config.set_key_value("sparse_infill_density", new ConfigOptionPercent(0));
    print_config.set_key_value("spiral_mode", new ConfigOptionBool(true));
    print_config.set_key_value("outer_wall_line_width", new ConfigOptionFloat(line_width));
    print_config.set_key_value("initial_layer_print_height", new ConfigOptionFloat(layer_height));
    print_config.set_key_value("layer_height", new ConfigOptionFloat(layer_height));
    obj->config.set_key_value("brim_type", new ConfigOptionEnum<BrimType>(btOuterAndInner));
    obj->config.set_key_value("brim_width", new ConfigOptionFloat(3.0));
    obj->config.set_key_value("brim_object_gap", new ConfigOptionFloat(0.0));

    //  cut upper
    auto obj_bb = obj->bounding_box();
    double height = (params.end - params.start + 1) / params.step;
    if (height < obj_bb.size().z()) {
        std::array<Vec3d, 4> plane_pts;
        plane_pts[0] = Vec3d(obj_bb.min(0), obj_bb.min(1), height);
        plane_pts[1] = Vec3d(obj_bb.max(0), obj_bb.min(1), height);
        plane_pts[2] = Vec3d(obj_bb.max(0), obj_bb.max(1), height);
        plane_pts[3] = Vec3d(obj_bb.min(0), obj_bb.max(1), height);
        cut_model(model, plane_pts, ModelObjectCutAttribute::KeepLower);
    }

    auto new_params  = params;
    auto mm3_per_mm  = Flow(line_width, layer_height, nozzle_diameter).mm3_per_mm() * filament_config.option<ConfigOptionFloats>("filament_flow_ratio")->get_at(0);
    new_params.end   = params.end / mm3_per_mm;
    new_params.start = params.start / mm3_per_mm;
    new_params.step  = params.step / mm3_per_mm;

    DynamicPrintConfig full_config;
    full_config.apply(FullPrintConfig::defaults());
    full_config.apply(print_config);
    full_config.apply(filament_config);
    full_config.apply(printer_config);

    process_and_store_3mf(&model, full_config, new_params, error_message);
    if (!error_message.empty())
        return;

    send_to_print(calib_info.dev_id, calib_info.select_ams, calib_info.process_bar, calib_info.bed_type, error_message);
}

void CalibUtils::calib_VFA(const CalibInfo& calib_info, std::string& error_message)
{
    const Calib_Params &params = calib_info.params;
    if (params.mode != CalibMode::Calib_VFA_Tower)
        return;

    Model model;
    std::string input_file = Slic3r::resources_dir() + "/calib/vfa/VFA.stl";
    read_model_from_file(input_file, model);

    DynamicPrintConfig print_config    = calib_info.print_prest->config;
    DynamicPrintConfig filament_config = calib_info.filament_prest->config;
    DynamicPrintConfig printer_config  = calib_info.printer_prest->config;

    filament_config.set_key_value("slow_down_layer_time", new ConfigOptionInts{0});
    filament_config.set_key_value("filament_max_volumetric_speed", new ConfigOptionFloats{200});
    print_config.set_key_value("enable_overhang_speed", new ConfigOptionBool{false});
    print_config.set_key_value("timelapse_type", new ConfigOptionEnum<TimelapseType>(tlTraditional));
    print_config.set_key_value("wall_loops", new ConfigOptionInt(1));
    print_config.set_key_value("top_shell_layers", new ConfigOptionInt(0));
    print_config.set_key_value("bottom_shell_layers", new ConfigOptionInt(1));
    print_config.set_key_value("sparse_infill_density", new ConfigOptionPercent(0));
    print_config.set_key_value("spiral_mode", new ConfigOptionBool(true));
    model.objects[0]->config.set_key_value("brim_type", new ConfigOptionEnum<BrimType>(btOuterOnly));
    model.objects[0]->config.set_key_value("brim_width", new ConfigOptionFloat(3.0));
    model.objects[0]->config.set_key_value("brim_object_gap", new ConfigOptionFloat(0.0));

    // cut upper
    auto obj_bb = model.objects[0]->bounding_box();
    auto height = 5 * ((params.end - params.start) / params.step + 1);
    if (height < obj_bb.size().z()) {
        std::array<Vec3d, 4> plane_pts;
        plane_pts[0] = Vec3d(obj_bb.min(0), obj_bb.min(1), height);
        plane_pts[1] = Vec3d(obj_bb.max(0), obj_bb.min(1), height);
        plane_pts[2] = Vec3d(obj_bb.max(0), obj_bb.max(1), height);
        plane_pts[3] = Vec3d(obj_bb.min(0), obj_bb.max(1), height);
        cut_model(model, plane_pts, ModelObjectCutAttribute::KeepLower);
    }
    else {
        error_message = "The start, end or step is not valid value.";
        return;
    }

    DynamicPrintConfig full_config;
    full_config.apply(FullPrintConfig::defaults());
    full_config.apply(print_config);
    full_config.apply(filament_config);
    full_config.apply(printer_config);

    process_and_store_3mf(&model, full_config, params, error_message);
    if (!error_message.empty())
        return;

    send_to_print(calib_info.dev_id, calib_info.select_ams, calib_info.process_bar, calib_info.bed_type, error_message);
}

void CalibUtils::calib_retraction(const CalibInfo &calib_info, std::string &error_message)
{
    const Calib_Params &params = calib_info.params;
    if (params.mode != CalibMode::Calib_Retraction_tower)
        return;

    Model model;
    std::string input_file = Slic3r::resources_dir() + "/calib/retraction/retraction_tower.stl";
    read_model_from_file(input_file, model);

    DynamicPrintConfig print_config    = calib_info.print_prest->config;
    DynamicPrintConfig filament_config = calib_info.filament_prest->config;
    DynamicPrintConfig printer_config  = calib_info.printer_prest->config;

    auto obj = model.objects[0];

    double layer_height = 0.2;

    auto max_lh = printer_config.option<ConfigOptionFloats>("max_layer_height");
    if (max_lh->values[0] < layer_height) max_lh->values[0] = {layer_height};

    obj->config.set_key_value("wall_loops", new ConfigOptionInt(2));
    obj->config.set_key_value("top_shell_layers", new ConfigOptionInt(0));
    obj->config.set_key_value("bottom_shell_layers", new ConfigOptionInt(3));
    obj->config.set_key_value("sparse_infill_density", new ConfigOptionPercent(0));
    obj->config.set_key_value("initial_layer_print_height", new ConfigOptionFloat(layer_height));
    obj->config.set_key_value("layer_height", new ConfigOptionFloat(layer_height));

    //  cut upper
    auto obj_bb = obj->bounding_box();
    auto height = 1.0 + 0.4 + ((params.end - params.start)) / params.step;
    if (height < obj_bb.size().z()) {
        std::array<Vec3d, 4> plane_pts = get_cut_plane_points(obj_bb, height);
        cut_model(model, plane_pts, ModelObjectCutAttribute::KeepLower);
    }

    DynamicPrintConfig full_config;
    full_config.apply(FullPrintConfig::defaults());
    full_config.apply(print_config);
    full_config.apply(filament_config);
    full_config.apply(printer_config);

    process_and_store_3mf(&model, full_config, params, error_message);
    if (!error_message.empty())
        return;

    send_to_print(calib_info.dev_id, calib_info.select_ams, calib_info.process_bar, calib_info.bed_type, error_message);
}

void CalibUtils::process_and_store_3mf(Model* model, const DynamicPrintConfig& full_config, const Calib_Params& params, std::string& error_message)
{
    Pointfs bedfs         = full_config.opt<ConfigOptionPoints>("printable_area")->values;
    double  print_height  = full_config.opt_float("printable_height");
    double  current_width = bedfs[2].x() - bedfs[0].x();
    double  current_depth = bedfs[2].y() - bedfs[0].y();
    Vec3i   plate_size;
    plate_size[0] = bedfs[2].x() - bedfs[0].x();
    plate_size[1] = bedfs[2].y() - bedfs[0].y();
    plate_size[2] = print_height;

    // todo: adjust the objects position
    if (model->objects.size() == 1) {
        ModelInstance *instance = model->objects[0]->instances[0];
        instance->set_offset(instance->get_offset() + Vec3d(current_width / 2, current_depth / 2, 0));
    } else {
        for (auto object : model->objects) {
            ModelInstance *instance = object->instances[0];
            instance->set_offset(instance->get_offset() + Vec3d(100, 100, 0));
        }
    }

    Slic3r::GUI::PartPlateList partplate_list(nullptr, model, PrinterTechnology::ptFFF);
    partplate_list.reset_size(plate_size.x(), plate_size.y(), plate_size.z(), false);

    Slic3r::GUI::PartPlate *part_plate = partplate_list.get_plate(0);

    PrintBase *               print        = NULL;
    Slic3r::GUI::GCodeResult *gcode_result = NULL;
    int                       print_index;
    part_plate->get_print(&print, &gcode_result, &print_index);

    BuildVolume build_volume(bedfs, print_height);
    unsigned int count = model->update_print_volume_state(build_volume);
    if (count == 0) {
        error_message = "Unable to calibrate: maybe because the set calibration value range is too large, or the step is too small";
        return;
    }

    // apply the new print config
    DynamicPrintConfig new_print_config = std::move(full_config);
    print->apply(*model, new_print_config);

    Print *fff_print = dynamic_cast<Print *>(print);
    fff_print->set_calib_params(params);

    //StringObjectException warning;
    //auto err = print->validate(&warning);
    //if (!err.string.empty()) {
    //    error_message = "slice validate: " + err.string;
    //    return;
    //}

    fff_print->process();
    part_plate->update_slice_result_valid_state(true);

    gcode_result->reset();
    fff_print->export_gcode(temp_gcode_path, gcode_result, nullptr);

    PlateDataPtrs plate_data_list;
    partplate_list.store_to_3mf_structure(plate_data_list, true, 0);

    for (auto plate_data : plate_data_list) {
        plate_data->gcode_file      = temp_gcode_path;
        plate_data->is_sliced_valid = true;
        plate_data->slice_filaments_info;
    }

    StoreParams store_params;
    store_params.path            = path.c_str();
    store_params.model           = model;
    store_params.plate_data_list = plate_data_list;
    store_params.config = &new_print_config;

    store_params.strategy = SaveStrategy::Silence | SaveStrategy::WithGcode | SaveStrategy::SplitModel | SaveStrategy::SkipModel;

    bool success = Slic3r::store_bbs_3mf(store_params);

    store_params.strategy = SaveStrategy::Silence | SaveStrategy::SplitModel | SaveStrategy::WithSliceInfo | SaveStrategy::SkipAuxiliary;
    store_params.path = config_3mf_path.c_str();
    success           = Slic3r::store_bbs_3mf(store_params);
}

void CalibUtils::send_to_print(const std::string& dev_id, const std::string& select_ams, std::shared_ptr<ProgressIndicator> process_bar, BedType bed_type, std::string& error_message)
{
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) {
        error_message = "Need select printer";
        return;
    }

    MachineObject* obj_ = dev->get_selected_machine();
    if (obj_ == nullptr) {
        error_message = "Need select printer";
        return;
    }

    if (obj_->is_in_printing()) {
        error_message = "Cannot send the print job when the printer is updating firmware";
        return;
    }
    else if (obj_->is_system_printing()) {
        error_message = "The printer is executing instructions. Please restart printing after it ends";
        return;
    }
    else if (obj_->is_in_printing()) {
        error_message = "The printer is busy on other print job";
        return;
    }
    else if (!obj_->is_function_supported(PrinterFunction::FUNC_PRINT_WITHOUT_SD) && (obj_->get_sdcard_state() == MachineObject::SdcardState::NO_SDCARD)) {
        error_message = "An SD card needs to be inserted before printing.";
        return;
    }
    if (obj_->is_lan_mode_printer()) {
        if (obj_->get_sdcard_state() == MachineObject::SdcardState::NO_SDCARD) {
            error_message = "An SD card needs to be inserted before printing via LAN.";
            return;
        }
    }

    print_job                   = std::make_shared<PrintJob>(std::move(process_bar), wxGetApp().plater(), dev_id);
    print_job->m_dev_ip         = obj_->dev_ip;
    print_job->m_ftp_folder     = obj_->get_ftp_folder();
    print_job->m_access_code    = obj_->get_access_code();


#if !BBL_RELEASE_TO_PUBLIC
    print_job->m_local_use_ssl_for_ftp = wxGetApp().app_config->get("enable_ssl_for_ftp") == "true" ? true : false;
    print_job->m_local_use_ssl_for_mqtt = wxGetApp().app_config->get("enable_ssl_for_mqtt") == "true" ? true : false;
#else
    print_job->m_local_use_ssl_for_ftp = obj_->local_use_ssl_for_ftp;
    print_job->m_local_use_ssl_for_mqtt = obj_->local_use_ssl_for_mqtt;
#endif

    print_job->connection_type  = obj_->connection_type();
    print_job->cloud_print_only = obj_->is_cloud_print_only;
    print_job->set_print_job_finished_event(wxGetApp().plater()->get_send_calibration_finished_event());

    PrintPrepareData job_data;
    job_data.is_from_plater = false;
    job_data.plate_idx = 0;
    job_data._3mf_config_path = config_3mf_path;
    job_data._3mf_path = path;
    job_data._temp_path = temp_dir;

    PlateListData plate_data;
    plate_data.is_valid = true;
    plate_data.plate_count = 1;
    plate_data.cur_plate_index = 0;
    plate_data.bed_type = bed_type;

    print_job->job_data = job_data;
    print_job->plate_data = plate_data;
    print_job->m_print_type = "from_normal";

    //if (!obj_->is_support_ams_mapping()) {
    //    error_message = "It is not support ams mapping.";
    //    return;
    //}

    //if (!obj_->has_ams()) {
    //    error_message = "There is no ams.";
    //    return;
    //}

    print_job->task_ams_mapping = select_ams;
    print_job->task_ams_mapping_info = "";
    print_job->task_use_ams = select_ams == "[254]" ? false : true;

    print_job->has_sdcard = obj_->has_sdcard();
    print_job->set_print_config(MachineBedTypeString[bed_type], true, false, false, false, true);

    print_job->start();
}

}
}

