#include "Model.hpp"
#include "libslic3r.h"
#include "BuildVolume.hpp"
#include "Exception.hpp"
#include "Model.hpp"
#include "ModelArrange.hpp"
#include "Arrange.hpp"
#include "Geometry.hpp"
#include "MTUtils.hpp"
#include "TriangleMeshSlicer.hpp"
#include "TriangleSelector.hpp"

#include "Format/AMF.hpp"
#include "Format/OBJ.hpp"
#include "Format/STL.hpp"
#include "Format/STEP.hpp"
// BBS
#include "FaceDetector.hpp"

#include "libslic3r/Geometry/ConvexHull.hpp"

#include <float.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/iostream.hpp>

#include "SVG.hpp"
#include <Eigen/Dense>
#include "GCodeWriter.hpp"

// BBS: for segment
#include "MeshBoolean.hpp"
#include "Format/3mf.hpp"

namespace Slic3r {
    // BBS initialization of static variables
    std::map<size_t, ExtruderParams> Model::extruderParamsMap = { {0,{"",0,0}}};
    GlobalSpeedMap Model::printSpeedMap{};
Model& Model::assign_copy(const Model &rhs)
{
    this->copy_id(rhs);
    // copy materials
    this->clear_materials();
    this->materials = rhs.materials;
    for (std::pair<const t_model_material_id, ModelMaterial*> &m : this->materials) {
        // Copy including the ID and m_model.
        m.second = new ModelMaterial(*m.second);
        m.second->set_model(this);
    }
    // copy objects
    this->clear_objects();
    this->objects.reserve(rhs.objects.size());
	for (const ModelObject *model_object : rhs.objects) {
        // Copy including the ID, leave ID set to invalid (zero).
        auto mo = ModelObject::new_copy(*model_object);
        mo->set_model(this);
		this->objects.emplace_back(mo);
    }

    // copy custom code per height
    this->custom_gcode_per_print_z = rhs.custom_gcode_per_print_z;

    // BBS: for design info
    this->design_info = rhs.design_info;
    this->model_info = rhs.model_info;

    return *this;
}

Model& Model::assign_copy(Model &&rhs)
{
    this->copy_id(rhs);
	// Move materials, adjust the parent pointer.
    this->clear_materials();
    this->materials = std::move(rhs.materials);
    for (std::pair<const t_model_material_id, ModelMaterial*> &m : this->materials)
        m.second->set_model(this);
    rhs.materials.clear();
    // Move objects, adjust the parent pointer.
    this->clear_objects();
	this->objects = std::move(rhs.objects);
    for (ModelObject *model_object : this->objects)
        model_object->set_model(this);
    rhs.objects.clear();

    // copy custom code per height
    this->custom_gcode_per_print_z = std::move(rhs.custom_gcode_per_print_z);

    //BBS: add auxiliary path logic
    // BBS: backup, all in one temp dir
    this->backup_path = std::move(rhs.backup_path);
    this->object_backup_id_map = std::move(rhs.object_backup_id_map);
    this->next_object_backup_id = rhs.next_object_backup_id;
    this->design_info = rhs.design_info;
    rhs.design_info.reset();
    this->model_info = rhs.model_info;
    rhs.model_info.reset();
    return *this;
}

void Model::assign_new_unique_ids_recursive()
{
    this->set_new_unique_id();
    for (std::pair<const t_model_material_id, ModelMaterial*> &m : this->materials)
        m.second->assign_new_unique_ids_recursive();
    for (ModelObject *model_object : this->objects)
        model_object->assign_new_unique_ids_recursive();
}

void Model::update_links_bottom_up_recursive()
{
	for (std::pair<const t_model_material_id, ModelMaterial*> &kvp : this->materials)
		kvp.second->set_model(this);
	for (ModelObject *model_object : this->objects) {
		model_object->set_model(this);
		for (ModelInstance *model_instance : model_object->instances)
			model_instance->set_model_object(model_object);
		for (ModelVolume *model_volume : model_object->volumes)
			model_volume->set_model_object(model_object);
	}
}

Model::~Model()
{
    this->clear_objects();
    this->clear_materials();
    // BBS: clear backup dir of temparary model
    if (!backup_path.empty())
        Slic3r::remove_backup(*this, true);
}

// BBS: add part plate related logic
// BBS: backup & restore
// Loading model from a file, it may be a simple geometry file as STL or OBJ, however it may be a project file as well.
Model Model::read_from_file(const std::string& input_file, DynamicPrintConfig* config, ConfigSubstitutionContext* config_substitutions,
                            LoadStrategy options, PlateDataPtrs* plate_data, std::vector<Preset*>* project_presets, bool *is_xxx, Semver* file_version, Import3mfProgressFn proFn,
                            ImportStepProgressFn stepFn, StepIsUtf8Fn stepIsUtf8Fn, BBLProject* project)
{
    Model model;

    DynamicPrintConfig temp_config;
    ConfigSubstitutionContext temp_config_substitutions_context(ForwardCompatibilitySubstitutionRule::EnableSilent);
    if (config == nullptr)
        config = &temp_config;
    if (config_substitutions == nullptr)
        config_substitutions = &temp_config_substitutions_context;
    //BBS: plate_data
    PlateDataPtrs temp_plate_data;
    bool temp_is_xxx;
    Semver temp_version;
    if (plate_data == nullptr)
        plate_data = &temp_plate_data;
    if (is_xxx == nullptr)
        is_xxx = &temp_is_xxx;
    if (file_version == nullptr)
        file_version = &temp_version;

    bool result = false;
    if (boost::algorithm::iends_with(input_file, ".stp") ||
        boost::algorithm::iends_with(input_file, ".step"))
        result = load_step(input_file.c_str(), &model, stepFn, stepIsUtf8Fn);
    else if (boost::algorithm::iends_with(input_file, ".stl"))
        result = load_stl(input_file.c_str(), &model);
    else if (boost::algorithm::iends_with(input_file, ".obj"))
        result = load_obj(input_file.c_str(), &model);
    //BBS: remove the old .amf.xml files
    //else if (boost::algorithm::iends_with(input_file, ".amf") || boost::algorithm::iends_with(input_file, ".amf.xml"))
    else if (boost::algorithm::iends_with(input_file, ".amf"))
        //BBS: is_xxx is used for is_inches when load amf
        result = load_amf(input_file.c_str(), config, config_substitutions, &model, is_xxx);
    else if (boost::algorithm::iends_with(input_file, ".3mf"))
        //BBS: add part plate related logic
        // BBS: backup & restore
        //FIXME options & LoadStrategy::CheckVersion ?
        //BBS: is_xxx is used for is_bbs_3mf when load 3mf
        result = load_bbs_3mf(input_file.c_str(), config, config_substitutions, &model, plate_data, project_presets, is_xxx, file_version, proFn, options, project);
    else
        throw Slic3r::RuntimeError("Unknown file format. Input file must have .stl, .obj, .amf(.xml) extension.");

    if (! result)
        throw Slic3r::RuntimeError("Loading of a model file failed.");

    if (model.objects.empty())
        throw Slic3r::RuntimeError("The supplied file couldn't be read because it's empty");
    
    for (ModelObject *o : model.objects)
        o->input_file = input_file;
    
    if (options & LoadStrategy::AddDefaultInstances)
        model.add_default_instances();

    //BBS
    //CustomGCode::update_custom_gcode_per_print_z_from_config(model.custom_gcode_per_print_z, config);
    CustomGCode::check_mode_for_custom_gcode_per_print_z(model.custom_gcode_per_print_z);

    sort_remove_duplicates(config_substitutions->substitutions);
    return model;
}

//BBS: add part plate related logic
// BBS: backup & restore
// Loading model from a file (3MF or AMF), not from a simple geometry file (STL or OBJ).
Model Model::read_from_archive(const std::string& input_file, DynamicPrintConfig* config, ConfigSubstitutionContext* config_substitutions, En3mfType& out_file_type, LoadStrategy options, PlateDataPtrs* plate_data, std::vector<Preset*>* project_presets, Semver* file_version, Import3mfProgressFn proFn, BBLProject *project)
{
    assert(config != nullptr);
    assert(config_substitutions != nullptr);

    Model model;

    bool result = false;
    bool is_bbl_3mf;
    if (boost::algorithm::iends_with(input_file, ".3mf")) {
        PrusaFileParser prusa_file_parser;
        if (prusa_file_parser.check_3mf_from_prusa(input_file)) {
            // for Prusa 3mf
            result = load_3mf(input_file.c_str(), *config, *config_substitutions, &model, true);
            out_file_type = En3mfType::From_Prusa;
        } else {
            // BBS: add part plate related logic
            // BBS: backup & restore
            result = load_bbs_3mf(input_file.c_str(), config, config_substitutions, &model, plate_data, project_presets, &is_bbl_3mf, file_version, proFn, options, project);
        }
    }
    else if (boost::algorithm::iends_with(input_file, ".zip.amf"))
        result = load_amf(input_file.c_str(), config, config_substitutions, &model, &is_bbl_3mf);
    else
        throw Slic3r::RuntimeError("Unknown file format. Input file must have .3mf or .zip.amf extension.");

    if (out_file_type != En3mfType::From_Prusa) {
        out_file_type = is_bbl_3mf ? En3mfType::From_BBS : En3mfType::From_Other;
    }

    if (!result)
        throw Slic3r::RuntimeError("Loading of a model file failed.");

    for (ModelObject *o : model.objects) {
//        if (boost::algorithm::iends_with(input_file, ".zip.amf"))
//        {
//            // we remove the .zip part of the extension to avoid it be added to filenames when exporting
//            o->input_file = boost::ireplace_last_copy(input_file, ".zip.", ".");
//        }
//        else
            o->input_file = input_file;
    }

    bool cb_cancel;
    if (options & LoadStrategy::AddDefaultInstances) {
        model.add_default_instances();
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" <<__LINE__ << boost::format("import 3mf IMPORT_STAGE_ADD_INSTANCE\n");
        if (proFn) {
            proFn(IMPORT_STAGE_ADD_INSTANCE, 0, 1, cb_cancel);
            if (cb_cancel)
                throw Slic3r::RuntimeError("Canceled");
        }
    }

    //BBS
    //CustomGCode::update_custom_gcode_per_print_z_from_config(model.custom_gcode_per_print_z, config);
    
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" << __LINE__ << boost::format("import 3mf IMPORT_STAGE_UPDATE_GCODE\n");
    if (proFn) {
        proFn(IMPORT_STAGE_UPDATE_GCODE, 0, 1, cb_cancel);
        if (cb_cancel)
            throw Slic3r::RuntimeError("Canceled");
    }
    
    CustomGCode::check_mode_for_custom_gcode_per_print_z(model.custom_gcode_per_print_z);

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" << __LINE__ << boost::format("import 3mf IMPORT_STAGE_CHECK_MODE_GCODE\n");
    if (proFn) {
        proFn(IMPORT_STAGE_CHECK_MODE_GCODE, 0, 1, cb_cancel);
        if (cb_cancel)
            throw Slic3r::RuntimeError("Canceled");
    }

    handle_legacy_sla(*config);

    return model;
}

ModelObject* Model::add_object()
{
    this->objects.emplace_back(new ModelObject(this));
    return this->objects.back();
}

ModelObject* Model::add_object(const char *name, const char *path, const TriangleMesh &mesh)
{
    ModelObject* new_object = new ModelObject(this);
    this->objects.push_back(new_object);
    new_object->name = name;
    new_object->input_file = path;
    ModelVolume *new_volume = new_object->add_volume(mesh);
    new_volume->name = name;
    new_volume->source.input_file = path;
    new_volume->source.object_idx = (int)this->objects.size() - 1;
    new_volume->source.volume_idx = (int)new_object->volumes.size() - 1;
    // BBS: set extruder id to 1
    if (!new_object->config.has("extruder") || new_object->config.extruder() == 0)
        new_object->config.set_key_value("extruder", new ConfigOptionInt(1));
    new_object->invalidate_bounding_box();
    return new_object;
}

ModelObject* Model::add_object(const char *name, const char *path, TriangleMesh &&mesh)
{
    ModelObject* new_object = new ModelObject(this);
    this->objects.push_back(new_object);
    new_object->name = name;
    new_object->input_file = path;
    ModelVolume *new_volume = new_object->add_volume(std::move(mesh));
    new_volume->name = name;
    new_volume->source.input_file = path;
    new_volume->source.object_idx = (int)this->objects.size() - 1;
    new_volume->source.volume_idx = (int)new_object->volumes.size() - 1;
    // BBS: set default extruder id to 1
    if (!new_object->config.has("extruder") || new_object->config.extruder() == 0)
        new_object->config.set_key_value("extruder", new ConfigOptionInt(1));
    new_object->invalidate_bounding_box();
    return new_object;
}

ModelObject* Model::add_object(const ModelObject &other)
{
	ModelObject* new_object = ModelObject::new_clone(other);
    new_object->set_model(this);
    // BBS: set default extruder id to 1
    if (!new_object->config.has("extruder") || new_object->config.extruder() == 0)
        new_object->config.set_key_value("extruder", new ConfigOptionInt(1));
    this->objects.push_back(new_object);
    // BBS: backup
    if (need_backup) {
        if (auto model = other.get_model()) {
            auto iter = object_backup_id_map.find(other.id().id);
            if (iter != object_backup_id_map.end()) {
                object_backup_id_map.emplace(new_object->id().id, iter->second);
                object_backup_id_map.erase(iter);
                return new_object;
            }
        }
        Slic3r::save_object_mesh(*new_object);
    }
    return new_object;
}

void Model::delete_object(size_t idx)
{
    ModelObjectPtrs::iterator i = this->objects.begin() + idx;
    // BBS: backup
    Slic3r::delete_object_mesh(**i);
    delete *i;
    this->objects.erase(i);
}

bool Model::delete_object(ModelObject* object)
{
    if (object != nullptr) {
        size_t idx = 0;
        for (ModelObject *model_object : objects) {
            if (model_object == object) {
                // BBS: backup
                Slic3r::delete_object_mesh(*model_object);
                delete model_object;
                objects.erase(objects.begin() + idx);
                return true;
            }
            ++ idx;
        }
    }
    return false;
}

bool Model::delete_object(ObjectID id)
{
    if (id.id != 0) {
        size_t idx = 0;
        for (ModelObject *model_object : objects) {
            if (model_object->id() == id) {
                // BBS: backup
                Slic3r::delete_object_mesh(*model_object);
                delete model_object;
                objects.erase(objects.begin() + idx);
                return true;
            }
            ++ idx;
        }
    }
    return false;
}

void Model::clear_objects()
{
    for (ModelObject* o : this->objects) {
        // BBS: backup
        Slic3r::delete_object_mesh(*o);
        delete o;
    }
    this->objects.clear();
    object_backup_id_map.clear();
    next_object_backup_id = 1;
}

// BBS: backup, reuse objects
void Model::collect_reusable_objects(std::vector<ObjectBase*>& objects)
{
    for (ModelObject* model_object : this->objects) {
        objects.push_back(model_object);
        for (ModelVolume* model_volume : model_object->volumes)
            objects.push_back(model_volume);
        std::transform(model_object->volumes.begin(),
                       model_object->volumes.end(),
                       std::back_inserter(model_object->volume_ids),
                       std::mem_fn(&ObjectBase::id));
        model_object->volumes.clear();
    }
    // we never own these objects 
    this->objects.clear();
}

void Model::set_object_backup_id(ModelObject const& object, int uuid)
{
    object_backup_id_map[object.id().id] = uuid;
    if (uuid >= next_object_backup_id) next_object_backup_id = uuid + 1;
}

int Model::get_object_backup_id(ModelObject const& object)
{
    auto i = object_backup_id_map.find(object.id().id);
    if (i == object_backup_id_map.end()) {
        i = object_backup_id_map.insert(std::make_pair(object.id().id, next_object_backup_id++)).first;
    }
    return i->second;
}

int Model::get_object_backup_id(ModelObject const& object) const
{
    return object_backup_id_map.find(object.id().id)->second;
}

void Model::delete_material(t_model_material_id material_id)
{
    ModelMaterialMap::iterator i = this->materials.find(material_id);
    if (i != this->materials.end()) {
        delete i->second;
        this->materials.erase(i);
    }
}

void Model::clear_materials()
{
    for (auto &m : this->materials)
        delete m.second;
    this->materials.clear();
}

ModelMaterial* Model::add_material(t_model_material_id material_id)
{
    assert(! material_id.empty());
    ModelMaterial* material = this->get_material(material_id);
    if (material == nullptr)
        material = this->materials[material_id] = new ModelMaterial(this);
    return material;
}

ModelMaterial* Model::add_material(t_model_material_id material_id, const ModelMaterial &other)
{
    assert(! material_id.empty());
    // delete existing material if any
    ModelMaterial* material = this->get_material(material_id);
    delete material;
    // set new material
	material = new ModelMaterial(other);
	material->set_model(this);
    this->materials[material_id] = material;
    return material;
}

// makes sure all objects have at least one instance
bool Model::add_default_instances()
{
    // apply a default position to all objects not having one
    for (ModelObject *o : this->objects)
        if (o->instances.empty())
            o->add_instance();
    return true;
}

// this returns the bounding box of the *transformed* instances
BoundingBoxf3 Model::bounding_box() const
{
    BoundingBoxf3 bb;
    for (ModelObject *o : this->objects)
        bb.merge(o->bounding_box());
    return bb;
}

unsigned int Model::update_print_volume_state(const BuildVolume &build_volume)
{
    unsigned int num_printable = 0;
    for (ModelObject* model_object : this->objects)
        num_printable += model_object->update_instances_print_volume_state(build_volume);
    //BBS: add logs for build_volume
    const BoundingBoxf3& print_volume = build_volume.bounding_volume();
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", print_volume {%1%, %2%, %3%} to {%4%, %5%, %6%}, got %7% printable istances")\
        %print_volume.min.x() %print_volume.min.y() %print_volume.min.z()%print_volume.max.x() %print_volume.max.y() %print_volume.max.z() %num_printable;
    return num_printable;
}

bool Model::center_instances_around_point(const Vec2d &point)
{
    BoundingBoxf3 bb;
    for (ModelObject *o : this->objects)
        for (size_t i = 0; i < o->instances.size(); ++ i)
            bb.merge(o->instance_bounding_box(i, false));

    Vec2d shift2 = point - to_2d(bb.center());
	if (std::abs(shift2(0)) < EPSILON && std::abs(shift2(1)) < EPSILON)
		// No significant shift, don't do anything.
		return false;

	Vec3d shift3 = Vec3d(shift2(0), shift2(1), 0.0);
	for (ModelObject *o : this->objects) {
		for (ModelInstance *i : o->instances)
			i->set_offset(i->get_offset() + shift3);
		o->invalidate_bounding_box();
	}
	return true;
}

// flattens everything to a single mesh
TriangleMesh Model::mesh() const
{
    TriangleMesh mesh;
    for (const ModelObject *o : this->objects)
        mesh.merge(o->mesh());
    return mesh;
}

void Model::duplicate_objects_grid(size_t x, size_t y, coordf_t dist)
{
    if (this->objects.size() > 1) throw "Grid duplication is not supported with multiple objects";
    if (this->objects.empty()) throw "No objects!";

    ModelObject* object = this->objects.front();
    object->clear_instances();

    Vec3d ext_size = object->bounding_box().size() + dist * Vec3d::Ones();

    for (size_t x_copy = 1; x_copy <= x; ++x_copy) {
        for (size_t y_copy = 1; y_copy <= y; ++y_copy) {
            ModelInstance* instance = object->add_instance();
            instance->set_offset(Vec3d(ext_size(0) * (double)(x_copy - 1), ext_size(1) * (double)(y_copy - 1), 0.0));
        }
    }
}

bool Model::looks_like_multipart_object() const
{
    if (this->objects.size() <= 1)
        return false;
    double zmin = std::numeric_limits<double>::max();
    for (const ModelObject *obj : this->objects) {
        if (obj->volumes.size() > 1 || obj->config.keys().size() > 1)
            return false;
        for (const ModelVolume *vol : obj->volumes) {
            double zmin_this = vol->mesh().bounding_box().min(2);
            if (zmin == std::numeric_limits<double>::max())
                zmin = zmin_this;
            else if (std::abs(zmin - zmin_this) > EPSILON)
                // The volumes don't share zmin.
                return true;
        }
    }
    return false;
}

// Generate next extruder ID string, in the range of (1, max_extruders).
static inline int auto_extruder_id(unsigned int max_extruders, unsigned int &cntr)
{
    int out = ++ cntr;
    if (cntr == max_extruders)
    	cntr = 0;
    return out;
}

void Model::convert_multipart_object(unsigned int max_extruders)
{
    assert(this->objects.size() >= 2);
    if (this->objects.size() < 2)
        return;
    
    ModelObject* object = new ModelObject(this);
    object->input_file = this->objects.front()->input_file;
    object->name = boost::filesystem::path(this->objects.front()->input_file).stem().string();
    //FIXME copy the config etc?

    unsigned int extruder_counter = 0;

	for (const ModelObject* o : this->objects)
    	for (const ModelVolume* v : o->volumes) {
            // If there are more than one object, put all volumes together 
            // Each object may contain any number of volumes and instances
            // The volumes transformations are relative to the object containing them...
            Geometry::Transformation trafo_volume = v->get_transformation();
            // Revert the centering operation.
            trafo_volume.set_offset(trafo_volume.get_offset() - o->origin_translation);
            int counter = 1;
            auto copy_volume = [o, max_extruders, &counter, &extruder_counter](ModelVolume *new_v) {
                assert(new_v != nullptr);
                new_v->name = (counter > 1) ? o->name + "_" + std::to_string(counter++) : o->name;
                //BBS: use default extruder id
                //new_v->config.set("extruder", auto_extruder_id(max_extruders, extruder_counter));
                return new_v;
            };
            if (o->instances.empty()) {
                copy_volume(object->add_volume(*v))->set_transformation(trafo_volume);
            } else {
                for (const ModelInstance* i : o->instances)
                    // ...so, transform everything to a common reference system (world)
                	copy_volume(object->add_volume(*v))->set_transformation(i->get_transformation() * trafo_volume);                    
            }
        }

    // commented-out to fix #2868
//    object->add_instance();
//    object->instances[0]->set_offset(object->raw_mesh_bounding_box().center());

    this->clear_objects();
    this->objects.push_back(object);
}

static constexpr const double volume_threshold_inches = 8.0; // 9 = 2*2*2;

bool Model::looks_like_imperial_units() const
{
    if (this->objects.size() == 0)
        return false;

    for (ModelObject* obj : this->objects)
        if (obj->get_object_stl_stats().volume < volume_threshold_inches)
            return true;

    return false;
}

void Model::convert_from_imperial_units(bool only_small_volumes)
{
    static constexpr const float in_to_mm = 25.4f;
    for (ModelObject* obj : this->objects)
        if (! only_small_volumes || obj->get_object_stl_stats().volume < volume_threshold_inches) {
            obj->scale_mesh_after_creation(in_to_mm);
            for (ModelVolume* v : obj->volumes) {
                assert(! v->source.is_converted_from_meters);
                v->source.is_converted_from_inches = true;
            }
        }
}

static constexpr const double volume_threshold_meters = 0.008; // 0.008 = 0.2*0.2*0.2

bool Model::looks_like_saved_in_meters() const
{
    if (this->objects.size() == 0)
        return false;

    for (ModelObject* obj : this->objects)
        if (obj->get_object_stl_stats().volume < volume_threshold_meters)
            return true;

    return false;
}

void Model::convert_from_meters(bool only_small_volumes)
{
    static constexpr const double m_to_mm = 1000;
    for (ModelObject* obj : this->objects)
        if (! only_small_volumes || obj->get_object_stl_stats().volume < volume_threshold_meters) {
            obj->scale_mesh_after_creation(m_to_mm);
            for (ModelVolume* v : obj->volumes) {
                assert(! v->source.is_converted_from_inches);
                v->source.is_converted_from_meters = true;
            }
        }
}

static constexpr const double zero_volume = 0.0000000001;

int Model::removed_objects_with_zero_volume()
{
    if (objects.size() == 0)
        return 0;

    int removed = 0;
    for (int i = int(objects.size()) - 1; i >= 0; i--)
        if (objects[i]->get_object_stl_stats().volume < zero_volume) {
            delete_object(size_t(i));
            removed++;
        }
    return removed;
}

void Model::adjust_min_z()
{
    if (objects.empty())
        return;

    if (bounding_box().min(2) < 0.0)
    {
        for (ModelObject* obj : objects)
        {
            if (obj != nullptr)
            {
                coordf_t obj_min_z = obj->bounding_box().min(2);
                if (obj_min_z < 0.0)
                    obj->translate_instances(Vec3d(0.0, 0.0, -obj_min_z));
            }
        }
    }
}

// Propose a filename including path derived from the ModelObject's input path.
// If object's name is filled in, use the object name, otherwise use the input name.
std::string Model::propose_export_file_name_and_path() const
{
    std::string input_file;
    for (const ModelObject *model_object : this->objects)
        for (ModelInstance *model_instance : model_object->instances)
            if (model_instance->is_printable()) {
                input_file = model_object->get_export_filename();

                if (!input_file.empty())
                    goto end;
                // Other instances will produce the same name, skip them.
                break;
            }
end:
    return input_file;
}

//BBS: add auxiliary files temp path
// BBS: backup all in one dir
std::string Model::get_auxiliary_file_temp_path()
{
    return get_backup_path("/Auxiliaries");
}

// BBS: backup dir
std::string Model::get_backup_path()
{
    if (backup_path.empty())
    {
        boost::filesystem::path parent_path(temporary_dir());
        std::time_t t = std::time(0);
        std::tm* now_time = std::localtime(&t);
        std::stringstream buf;
        buf << "/bamboo_model/";
        buf << std::put_time(now_time, "%a_%b_%d/%H_%M_%S#");
        buf << this->id().id;

        backup_path = parent_path.string() + buf.str();
        boost::filesystem::path temp_path(backup_path);
        if (boost::filesystem::exists(temp_path))
        {
            boost::filesystem::remove_all(temp_path);
        }
    }
    boost::filesystem::path temp_path(backup_path);
    try {
        if (!boost::filesystem::exists(temp_path))
        {
            BOOST_LOG_TRIVIAL(info) << "create /3D/Objects in " << temp_path;
            boost::filesystem::create_directories(backup_path + "/3D/Objects");
            BOOST_LOG_TRIVIAL(info) << "create /Metadata in " << temp_path;
            boost::filesystem::create_directories(backup_path + "/Metadata");
            BOOST_LOG_TRIVIAL(info) << "create /lock.txt in " << temp_path;
            boost::filesystem::save_string_file(backup_path + "/lock.txt",
                boost::lexical_cast<std::string>(get_current_pid()));
        }
    } catch (std::exception &ex) {
        BOOST_LOG_TRIVIAL(error) << "Failed to create backup path" << temp_path << ": " << ex.what();
    }

    return backup_path;
}

std::string Model::get_backup_path(const std::string &sub_path)
{
    auto path = get_backup_path() + "/" + sub_path;
    try {
        if (!boost::filesystem::exists(path)) {
            BOOST_LOG_TRIVIAL(info) << "create missing sub_path" << path;
            boost::filesystem::create_directories(path);
        }
    } catch (std::exception &ex) {
        BOOST_LOG_TRIVIAL(error) << "Failed to create missing sub_path" << path << ": " << ex.what();
    }
    return path;
}

void Model::set_backup_path(std::string const& path)
{
    if (backup_path == path)
        return;
    if ("detach" == path) {
        backup_path.clear();
        return;
    }
    if (!backup_path.empty())
        Slic3r::remove_backup(*this, true);
    backup_path = path;
}

void Model::load_from(Model& model)
{
    set_backup_path(model.get_backup_path());
    model.backup_path.clear();
    object_backup_id_map = model.object_backup_id_map;
    next_object_backup_id = model.next_object_backup_id;
    design_info = model.design_info;
    model_info  = model.model_info;
    model.design_info.reset();
    model.model_info.reset();
}

// BBS: backup
void Model::set_need_backup()
{
    need_backup = true;
}

std::string Model::propose_export_file_name_and_path(const std::string &new_extension) const
{
    return boost::filesystem::path(this->propose_export_file_name_and_path()).replace_extension(new_extension).string();
}

bool Model::is_fdm_support_painted() const
{
    return std::any_of(this->objects.cbegin(), this->objects.cend(), [](const ModelObject *mo) { return mo->is_fdm_support_painted(); });
}

bool Model::is_seam_painted() const
{
    return std::any_of(this->objects.cbegin(), this->objects.cend(), [](const ModelObject *mo) { return mo->is_seam_painted(); });
}

bool Model::is_mm_painted() const
{
    return std::any_of(this->objects.cbegin(), this->objects.cend(), [](const ModelObject *mo) { return mo->is_mm_painted(); });
}

ModelObject::~ModelObject()
{
    this->clear_volumes();
    this->clear_instances();
}

// maintains the m_model pointer
ModelObject& ModelObject::assign_copy(const ModelObject &rhs)
{
	assert(this->id().invalid() || this->id() == rhs.id());
	assert(this->config.id().invalid() || this->config.id() == rhs.config.id());
	this->copy_id(rhs);

    this->name                        = rhs.name;
    //BBS: add module name
    this->module_name                 = rhs.module_name;
    this->input_file                  = rhs.input_file;
    // Copies the config's ID
    this->config                      = rhs.config;
    assert(this->config.id() == rhs.config.id());
    this->sla_support_points          = rhs.sla_support_points;
    this->sla_points_status           = rhs.sla_points_status;
    this->sla_drain_holes             = rhs.sla_drain_holes;
    this->layer_config_ranges         = rhs.layer_config_ranges;
    this->layer_height_profile        = rhs.layer_height_profile;
    this->printable                   = rhs.printable;
    this->origin_translation          = rhs.origin_translation;
    m_bounding_box                    = rhs.m_bounding_box;
    m_bounding_box_valid              = rhs.m_bounding_box_valid;
    m_raw_bounding_box                = rhs.m_raw_bounding_box;
    m_raw_bounding_box_valid          = rhs.m_raw_bounding_box_valid;
    m_raw_mesh_bounding_box           = rhs.m_raw_mesh_bounding_box;
    m_raw_mesh_bounding_box_valid     = rhs.m_raw_mesh_bounding_box_valid;

    this->clear_volumes();
    this->volumes.reserve(rhs.volumes.size());
    for (ModelVolume *model_volume : rhs.volumes) {
        this->volumes.emplace_back(new ModelVolume(*model_volume));
        this->volumes.back()->set_model_object(this);
    }
    this->clear_instances();
	this->instances.reserve(rhs.instances.size());
    for (const ModelInstance *model_instance : rhs.instances) {
        this->instances.emplace_back(new ModelInstance(*model_instance));
        this->instances.back()->set_model_object(this);
    }

    return *this;
}

// maintains the m_model pointer
ModelObject& ModelObject::assign_copy(ModelObject &&rhs)
{
	assert(this->id().invalid());
    this->copy_id(rhs);

    this->name                        = std::move(rhs.name);
    //BBS: add module name
    this->module_name                 = std::move(rhs.module_name);
    this->input_file                  = std::move(rhs.input_file);
    // Moves the config's ID
    this->config                      = std::move(rhs.config);
    assert(this->config.id() == rhs.config.id());
    this->sla_support_points          = std::move(rhs.sla_support_points);
    this->sla_points_status           = std::move(rhs.sla_points_status);
    this->sla_drain_holes             = std::move(rhs.sla_drain_holes);
    this->layer_config_ranges         = std::move(rhs.layer_config_ranges);
    this->layer_height_profile        = std::move(rhs.layer_height_profile);
    this->printable                   = std::move(rhs.printable);
    this->origin_translation          = std::move(rhs.origin_translation);
    m_bounding_box                    = std::move(rhs.m_bounding_box);
    m_bounding_box_valid              = std::move(rhs.m_bounding_box_valid);
    m_raw_bounding_box                = rhs.m_raw_bounding_box;
    m_raw_bounding_box_valid          = rhs.m_raw_bounding_box_valid;
    m_raw_mesh_bounding_box           = rhs.m_raw_mesh_bounding_box;
    m_raw_mesh_bounding_box_valid     = rhs.m_raw_mesh_bounding_box_valid;

    this->clear_volumes();
	this->volumes = std::move(rhs.volumes);
	rhs.volumes.clear();
    for (ModelVolume *model_volume : this->volumes)
        model_volume->set_model_object(this);
    this->clear_instances();
	this->instances = std::move(rhs.instances);
	rhs.instances.clear();
    for (ModelInstance *model_instance : this->instances)
        model_instance->set_model_object(this);

    return *this;
}

void ModelObject::assign_new_unique_ids_recursive()
{
    this->set_new_unique_id();
    for (ModelVolume *model_volume : this->volumes)
        model_volume->assign_new_unique_ids_recursive();
    for (ModelInstance *model_instance : this->instances)
        model_instance->assign_new_unique_ids_recursive();
    this->layer_height_profile.set_new_unique_id();
}

// Clone this ModelObject including its volumes and instances, keep the IDs of the copies equal to the original.
// Called by Print::apply() to clone the Model / ModelObject hierarchy to the back end for background processing.
//ModelObject* ModelObject::clone(Model *parent)
//{
//    return new ModelObject(parent, *this, true);
//}


// BBS: production extension
int ModelObject::get_backup_id() const { return m_model ? get_model()->get_object_backup_id(*this) : -1; }

ModelVolume* ModelObject::add_volume(const TriangleMesh &mesh)
{
    ModelVolume* v = new ModelVolume(this, mesh);
    this->volumes.push_back(v);
    v->center_geometry_after_creation();
    this->invalidate_bounding_box();
    // BBS: backup
    Slic3r::save_object_mesh(*this);
    return v;
}

ModelVolume* ModelObject::add_volume(TriangleMesh &&mesh, ModelVolumeType type /*= ModelVolumeType::MODEL_PART*/)
{
    ModelVolume* v = new ModelVolume(this, std::move(mesh), type);
    this->volumes.push_back(v);
    v->center_geometry_after_creation();
    this->invalidate_bounding_box();
    // BBS: backup
    Slic3r::save_object_mesh(*this);
    return v;
}

ModelVolume* ModelObject::add_volume(const ModelVolume &other, ModelVolumeType type /*= ModelVolumeType::INVALID*/)
{
    ModelVolume* v = new ModelVolume(this, other);
    if (type != ModelVolumeType::INVALID && v->type() != type)
        v->set_type(type);
    this->volumes.push_back(v);
	// The volume should already be centered at this point of time when copying shared pointers of the triangle mesh and convex hull.
//	v->center_geometry_after_creation();
//    this->invalidate_bounding_box();
    // BBS: backup
    Slic3r::save_object_mesh(*this);
    return v;
}

ModelVolume* ModelObject::add_volume(const ModelVolume &other, TriangleMesh &&mesh)
{
    ModelVolume* v = new ModelVolume(this, other, std::move(mesh));
    this->volumes.push_back(v);
    v->center_geometry_after_creation();
    this->invalidate_bounding_box();
    // BBS: backup
    Slic3r::save_object_mesh(*this);
    return v;
}

void ModelObject::delete_volume(size_t idx)
{
    ModelVolumePtrs::iterator i = this->volumes.begin() + idx;
    delete *i;
    this->volumes.erase(i);

    if (this->volumes.size() == 1)
    {
        // only one volume left
        // we need to collapse the volume transform into the instances transforms because now when selecting this volume
        // it will be seen as a single full instance ans so its volume transform may be ignored
        ModelVolume* v = this->volumes.front();
        Transform3d v_t = v->get_transformation().get_matrix();
        for (ModelInstance* inst : this->instances)
        {
            inst->set_transformation(Geometry::Transformation(inst->get_transformation().get_matrix() * v_t));
        }
        Geometry::Transformation t;
        v->set_transformation(t);
        v->set_new_unique_id();
    }

    this->invalidate_bounding_box();
    // BBS: backup
    Slic3r::save_object_mesh(*this);
}

void ModelObject::clear_volumes()
{
    for (ModelVolume *v : this->volumes)
        delete v;
    this->volumes.clear();
    this->invalidate_bounding_box();
    // BBS: backup: do not save
    // Slic3r::save_object_mesh(*this);
}

bool ModelObject::is_fdm_support_painted() const
{
    return std::any_of(this->volumes.cbegin(), this->volumes.cend(), [](const ModelVolume *mv) { return mv->is_fdm_support_painted(); });
}

bool ModelObject::is_seam_painted() const
{
    return std::any_of(this->volumes.cbegin(), this->volumes.cend(), [](const ModelVolume *mv) { return mv->is_seam_painted(); });
}

bool ModelObject::is_mm_painted() const
{
    return std::any_of(this->volumes.cbegin(), this->volumes.cend(), [](const ModelVolume *mv) { return mv->is_mm_painted(); });
}

void ModelObject::sort_volumes(bool full_sort)
{
    // sort volumes inside the object to order "Model Part, Negative Volume, Modifier, Support Blocker and Support Enforcer. "
    if (full_sort)
        std::stable_sort(volumes.begin(), volumes.end(), [](ModelVolume* vl, ModelVolume* vr) {
            return vl->type() < vr->type();
        });
    // sort have to controll "place" of the support blockers/enforcers. But one of the model parts have to be on the first place.
    else
        std::stable_sort(volumes.begin(), volumes.end(), [](ModelVolume* vl, ModelVolume* vr) {
            ModelVolumeType vl_type = vl->type() > ModelVolumeType::PARAMETER_MODIFIER ? vl->type() : ModelVolumeType::PARAMETER_MODIFIER;
            ModelVolumeType vr_type = vr->type() > ModelVolumeType::PARAMETER_MODIFIER ? vr->type() : ModelVolumeType::PARAMETER_MODIFIER;
            return vl_type < vr_type;
        });
}

ModelInstance* ModelObject::add_instance()
{
    ModelInstance* i = new ModelInstance(this);
    this->instances.push_back(i);
    this->invalidate_bounding_box();
    // BBS: backup: do not save
    if (this->instances.size() == 1)
        Slic3r::save_object_mesh(*this);
    return i;
}

ModelInstance* ModelObject::add_instance(const ModelInstance &other)
{
    ModelInstance* i = new ModelInstance(this, other);
    this->instances.push_back(i);
    this->invalidate_bounding_box();
    return i;
}

ModelInstance* ModelObject::add_instance(const Vec3d &offset, const Vec3d &scaling_factor, const Vec3d &rotation, const Vec3d &mirror)
{
    auto *instance = add_instance();
    instance->set_offset(offset);
    instance->set_scaling_factor(scaling_factor);
    instance->set_rotation(rotation);
    instance->set_mirror(mirror);
    return instance;
}

void ModelObject::delete_instance(size_t idx)
{
    ModelInstancePtrs::iterator i = this->instances.begin() + idx;
    delete *i;
    this->instances.erase(i);
    this->invalidate_bounding_box();
}

void ModelObject::delete_last_instance()
{
    this->delete_instance(this->instances.size() - 1);
}

void ModelObject::clear_instances()
{
    for (ModelInstance *i : this->instances)
        delete i;
    this->instances.clear();
    this->invalidate_bounding_box();
}

// Returns the bounding box of the transformed instances.
// This bounding box is approximate and not snug.
const BoundingBoxf3& ModelObject::bounding_box() const
{
    if (! m_bounding_box_valid) {
        m_bounding_box_valid = true;
        BoundingBoxf3 raw_bbox = this->raw_mesh_bounding_box();
        m_bounding_box.reset();
        for (const ModelInstance *i : this->instances)
            m_bounding_box.merge(i->transform_bounding_box(raw_bbox));
    }
    return m_bounding_box;
}

// A mesh containing all transformed instances of this object.
TriangleMesh ModelObject::mesh() const
{
    TriangleMesh mesh;
    TriangleMesh raw_mesh = this->raw_mesh();
    for (const ModelInstance *i : this->instances) {
        TriangleMesh m = raw_mesh;
        i->transform_mesh(&m);
        mesh.merge(m);
    }
    return mesh;
}

// Non-transformed (non-rotated, non-scaled, non-translated) sum of non-modifier object volumes.
// Currently used by ModelObject::mesh(), to calculate the 2D envelope for 2D plater
// and to display the object statistics at ModelObject::print_info().
TriangleMesh ModelObject::raw_mesh() const
{
    TriangleMesh mesh;
    for (const ModelVolume *v : this->volumes)
        if (v->is_model_part())
        {
            TriangleMesh vol_mesh(v->mesh());
            vol_mesh.transform(v->get_matrix());
            mesh.merge(vol_mesh);
        }
    return mesh;
}

// Non-transformed (non-rotated, non-scaled, non-translated) sum of non-modifier object volumes.
// Currently used by ModelObject::mesh(), to calculate the 2D envelope for 2D plater
// and to display the object statistics at ModelObject::print_info().
indexed_triangle_set ModelObject::raw_indexed_triangle_set() const
{
    size_t num_vertices = 0;
    size_t num_faces    = 0;
    for (const ModelVolume *v : this->volumes)
        if (v->is_model_part()) {
            num_vertices += v->mesh().its.vertices.size();
            num_faces    += v->mesh().its.indices.size();
        }
    indexed_triangle_set out;
    out.vertices.reserve(num_vertices);
    out.indices.reserve(num_faces);
    for (const ModelVolume *v : this->volumes)
        if (v->is_model_part()) {
            size_t i = out.vertices.size();
            size_t j = out.indices.size();
            append(out.vertices, v->mesh().its.vertices);
            append(out.indices,  v->mesh().its.indices);
            auto m = v->get_matrix();
            for (; i < out.vertices.size(); ++ i)
                out.vertices[i] = (m * out.vertices[i].cast<double>()).cast<float>().eval();
            if (v->is_left_handed()) {
                for (; j < out.indices.size(); ++ j)
                    std::swap(out.indices[j][0], out.indices[j][1]);
            }
        }
    return out;
}


const BoundingBoxf3& ModelObject::raw_mesh_bounding_box() const
{
    if (! m_raw_mesh_bounding_box_valid) {
        m_raw_mesh_bounding_box_valid = true;
        m_raw_mesh_bounding_box.reset();
        for (const ModelVolume *v : this->volumes)
            if (v->is_model_part())
                m_raw_mesh_bounding_box.merge(v->mesh().transformed_bounding_box(v->get_matrix()));
    }
    return m_raw_mesh_bounding_box;
}

BoundingBoxf3 ModelObject::full_raw_mesh_bounding_box() const
{
	BoundingBoxf3 bb;
	for (const ModelVolume *v : this->volumes)
		bb.merge(v->mesh().transformed_bounding_box(v->get_matrix()));
	return bb;
}

// A transformed snug bounding box around the non-modifier object volumes, without the translation applied.
// This bounding box is only used for the actual slicing and for layer editing UI to calculate the layers.
const BoundingBoxf3& ModelObject::raw_bounding_box() const
{
    if (! m_raw_bounding_box_valid) {
        m_raw_bounding_box_valid = true;
        m_raw_bounding_box.reset();
        if (this->instances.empty())
            throw Slic3r::InvalidArgument("Can't call raw_bounding_box() with no instances");

        const Transform3d& inst_matrix = this->instances.front()->get_transformation().get_matrix(true);
        for (const ModelVolume *v : this->volumes)
            if (v->is_model_part())
                m_raw_bounding_box.merge(v->mesh().transformed_bounding_box(inst_matrix * v->get_matrix()));
    }
	return m_raw_bounding_box;
}

// This returns an accurate snug bounding box of the transformed object instance, without the translation applied.
BoundingBoxf3 ModelObject::instance_bounding_box(size_t instance_idx, bool dont_translate) const
{
    BoundingBoxf3 bb;
    const Transform3d& inst_matrix = this->instances[instance_idx]->get_transformation().get_matrix(dont_translate);
    for (ModelVolume *v : this->volumes)
    {
        if (v->is_model_part())
            bb.merge(v->mesh().transformed_bounding_box(inst_matrix * v->get_matrix()));
    }
    return bb;
}

//BBS: add convex bounding box
BoundingBoxf3 ModelObject::instance_convex_hull_bounding_box(size_t instance_idx, bool dont_translate) const
{
    BoundingBoxf3 bb;
    const Transform3d& inst_matrix = this->instances[instance_idx]->get_transformation().get_matrix(dont_translate);
    for (ModelVolume *v : this->volumes)
    {
        if (v->is_model_part())
            bb.merge(v->get_convex_hull().transformed_bounding_box(inst_matrix * v->get_matrix()));
    }
    return bb;
}


// Calculate 2D convex hull of of a projection of the transformed printable volumes into the XY plane.
// This method is cheap in that it does not make any unnecessary copy of the volume meshes.
// This method is used by the auto arrange function.
Polygon ModelObject::convex_hull_2d(const Transform3d& trafo_instance) const
{
#if 0
    Points pts;

    for (const ModelVolume* v : volumes) {
        if (v->is_model_part())
            //BBS: use convex hull vertex instead of all
            append(pts, its_convex_hull_2d_above(v->get_convex_hull().its, (trafo_instance * v->get_matrix()).cast<float>(), 0.0f).points);
            //append(pts, its_convex_hull_2d_above(v->mesh().its, (trafo_instance * v->get_matrix()).cast<float>(), 0.0f).points);
    }
    return Geometry::convex_hull(std::move(pts));
#else
    Points pts;
    for (const ModelVolume *v : this->volumes)
        if (v->is_model_part()) {
            const Polygon& volume_hull = v->get_convex_hull_2d(trafo_instance);

            pts.insert(pts.end(), volume_hull.points.begin(), volume_hull.points.end());
        }

    //std::sort(pts.begin(), pts.end(), [](const Point& a, const Point& b) { return a(0) < b(0) || (a(0) == b(0) && a(1) < b(1)); });
    //pts.erase(std::unique(pts.begin(), pts.end(), [](const Point& a, const Point& b) { return a(0) == b(0) && a(1) == b(1); }), pts.end());
    /*std::vector<Points> points;
    //points.push_back(pts);
    Polygon hull = Geometry::convex_hull(std::move(pts));
    static int irun = 0;
    BoundingBox bbox_svg;

    bbox_svg.merge(get_extents(pts));
    bbox_svg.merge(get_extents(hull));
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": bbox_svg.min{%1%,%2%} max{%3%,%4%}, points count %5%")% bbox_svg.min.x()% bbox_svg.min.y()% bbox_svg.max.x()% bbox_svg.max.y()%points[0].size();
    {
        std::stringstream stri;
        stri << "convex_2d_hull_" << irun << ".svg";
        SVG svg(stri.str(), bbox_svg);

        std::vector<Polygon> hulls;
        hulls.push_back(hull);
        svg.draw(to_polylines(points), "blue");
        svg.draw(to_polylines(hulls), "red");
        svg.Close();
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": stri %1%, Polygon.size %2%, point[0] {%3%, %4%}, point[1] {%5%, %6%}")% stri.str()% hull.size()% hull[0].x()% hull[0].y()% hull[1].x()% hull[1].y();
    }
    ++ irun;
    return hull;*/
    return Geometry::convex_hull(std::move(pts));
#endif
}

void ModelObject::center_around_origin(bool include_modifiers)
{
    // calculate the displacements needed to 
    // center this object around the origin
    const BoundingBoxf3 bb = include_modifiers ? full_raw_mesh_bounding_box() : raw_mesh_bounding_box();

    // Shift is the vector from the center of the bounding box to the origin
    const Vec3d shift = -bb.center();

    this->translate(shift);
    this->origin_translation += shift;
}

void ModelObject::ensure_on_bed(bool allow_negative_z)
{
    double z_offset = 0.0;

    if (allow_negative_z) {
        if (parts_count() == 1) {
            const double min_z = get_min_z();
            const double max_z = get_max_z();
            if (min_z >= SINKING_Z_THRESHOLD || max_z < 0.0)
                z_offset = -min_z;
        }
        else {
            const double max_z = get_max_z();
            if (max_z < SINKING_MIN_Z_THRESHOLD)
                z_offset = SINKING_MIN_Z_THRESHOLD - max_z;
        }
    }
    else
        z_offset = -get_min_z();

    if (z_offset != 0.0)
        translate_instances(z_offset * Vec3d::UnitZ());
}

void ModelObject::translate_instances(const Vec3d& vector)
{
    for (size_t i = 0; i < instances.size(); ++i) {
        translate_instance(i, vector);
    }
}

void ModelObject::translate_instance(size_t instance_idx, const Vec3d& vector)
{
    assert(instance_idx < instances.size());
    ModelInstance* i = instances[instance_idx];
    i->set_offset(i->get_offset() + vector);
    invalidate_bounding_box();
}

void ModelObject::translate(double x, double y, double z)
{
    for (ModelVolume *v : this->volumes) {
        v->translate(x, y, z);
    }

    if (m_bounding_box_valid)
        m_bounding_box.translate(x, y, z);
}

void ModelObject::scale(const Vec3d &versor)
{
    for (ModelVolume *v : this->volumes) {
        v->scale(versor);
    }
    this->invalidate_bounding_box();
}

void ModelObject::rotate(double angle, Axis axis)
{
    for (ModelVolume *v : this->volumes) {
        v->rotate(angle, axis);
    }
    center_around_origin();
    this->invalidate_bounding_box();
}

void ModelObject::rotate(double angle, const Vec3d& axis)
{
    for (ModelVolume *v : this->volumes) {
        v->rotate(angle, axis);
    }

    //BBS update assemble transformation when modify volume rotation
    for (int i = 0; i < instances.size(); i++) {
        instances[i]->rotate_assemble(-angle, axis);
    }

    center_around_origin();
    this->invalidate_bounding_box();
}

void ModelObject::mirror(Axis axis)
{
    for (ModelVolume *v : this->volumes) {
        v->mirror(axis);
    }
    this->invalidate_bounding_box();
}

// This method could only be called before the meshes of this ModelVolumes are not shared!
void ModelObject::scale_mesh_after_creation(const float scale)
{
    for (ModelVolume *v : this->volumes) {
        v->scale_geometry_after_creation(scale);
        v->set_offset(Vec3d(scale, scale, scale).cwiseProduct(v->get_offset()));
    }
    this->invalidate_bounding_box();
}

void ModelObject::convert_units(ModelObjectPtrs& new_objects, ConversionType conv_type, std::vector<int> volume_idxs)
{
    BOOST_LOG_TRIVIAL(trace) << "ModelObject::convert_units - start";

    ModelObject* new_object = new_clone(*this);

    float koef = conv_type == ConversionType::CONV_FROM_INCH   ? 25.4f  : conv_type == ConversionType::CONV_TO_INCH  ? 0.0393700787f  :
                 conv_type == ConversionType::CONV_FROM_METER  ? 1000.f : conv_type == ConversionType::CONV_TO_METER ? 0.001f         : 1.f;

    new_object->set_model(nullptr);
    new_object->sla_support_points.clear();
    new_object->sla_drain_holes.clear();
    new_object->sla_points_status = sla::PointsStatus::NoPoints;
    new_object->clear_volumes();
    new_object->input_file.clear();

    int vol_idx = 0;
    for (ModelVolume* volume : volumes) {
        if (!volume->mesh().empty()) {
            TriangleMesh mesh(volume->mesh());

            ModelVolume* vol = new_object->add_volume(mesh);
            vol->name = volume->name;
            vol->set_type(volume->type());
            // Don't copy the config's ID.
            vol->config.assign_config(volume->config);
            assert(vol->config.id().valid());
            assert(vol->config.id() != volume->config.id());
            vol->set_material(volume->material_id(), *volume->material());
            vol->source.input_file = volume->source.input_file;
            vol->source.object_idx = (int)new_objects.size();
            vol->source.volume_idx = vol_idx;
            vol->source.is_converted_from_inches = volume->source.is_converted_from_inches;
            vol->source.is_converted_from_meters = volume->source.is_converted_from_meters;
            vol->source.is_from_builtin_objects = volume->source.is_from_builtin_objects;

            vol->supported_facets.assign(volume->supported_facets);
            vol->seam_facets.assign(volume->seam_facets);
            vol->mmu_segmentation_facets.assign(volume->mmu_segmentation_facets);

            // Perform conversion only if the target "imperial" state is different from the current one.
            // This check supports conversion of "mixed" set of volumes, each with different "imperial" state.
            if (//vol->source.is_converted_from_inches != from_imperial && 
                (volume_idxs.empty() || 
                 std::find(volume_idxs.begin(), volume_idxs.end(), vol_idx) != volume_idxs.end())) {
                vol->scale_geometry_after_creation(koef);
                vol->set_offset(Vec3d(koef, koef, koef).cwiseProduct(volume->get_offset()));
                if (conv_type == ConversionType::CONV_FROM_INCH || conv_type == ConversionType::CONV_TO_INCH)
                    vol->source.is_converted_from_inches = conv_type == ConversionType::CONV_FROM_INCH;
                if (conv_type == ConversionType::CONV_FROM_METER || conv_type == ConversionType::CONV_TO_METER)
                    vol->source.is_converted_from_meters = conv_type == ConversionType::CONV_FROM_METER;
                assert(! vol->source.is_converted_from_inches || ! vol->source.is_converted_from_meters);
            }
            else
                vol->set_offset(volume->get_offset());
        }
        vol_idx ++;
    }
    new_object->invalidate_bounding_box();

    new_objects.push_back(new_object);

    BOOST_LOG_TRIVIAL(trace) << "ModelObject::convert_units - end";
}

size_t ModelObject::materials_count() const
{
    std::set<t_model_material_id> material_ids;
    for (const ModelVolume *v : this->volumes)
        material_ids.insert(v->material_id());
    return material_ids.size();
}

size_t ModelObject::facets_count() const
{
    size_t num = 0;
    for (const ModelVolume *v : this->volumes)
        if (v->is_model_part())
            num += v->mesh().facets_count();
    return num;
}

size_t ModelObject::parts_count() const
{
    size_t num = 0;
    for (const ModelVolume* v : this->volumes)
        if (v->is_model_part())
            ++num;
    return num;
}

// BBS: replace z with plane_points
ModelObjectPtrs ModelObject::cut(size_t instance, std::array<Vec3d, 4> plane_points, ModelObjectCutAttributes attributes)
{
    if (! attributes.has(ModelObjectCutAttribute::KeepUpper) && ! attributes.has(ModelObjectCutAttribute::KeepLower))
        return {};

    BOOST_LOG_TRIVIAL(trace) << "ModelObject::cut - start";

    // Clone the object to duplicate instances, materials etc.
    bool keep_upper = attributes.has(ModelObjectCutAttribute::KeepUpper);
    bool keep_lower = attributes.has(ModelObjectCutAttribute::KeepLower);
    bool cut_to_parts = attributes.has(ModelObjectCutAttribute::CutToParts);
    ModelObject* upper = keep_upper ? ModelObject::new_clone(*this) : nullptr;
    ModelObject* lower = cut_to_parts ? upper : (keep_lower ? ModelObject::new_clone(*this) : nullptr);

    if (attributes.has(ModelObjectCutAttribute::KeepUpper)) {
        upper->set_model(nullptr);
        upper->sla_support_points.clear();
        upper->sla_drain_holes.clear();
        upper->sla_points_status = sla::PointsStatus::NoPoints;
        upper->clear_volumes();
        upper->input_file.clear();
    }

    if (keep_lower && lower != upper) {
        lower->set_model(nullptr);
        lower->sla_support_points.clear();
        lower->sla_drain_holes.clear();
        lower->sla_points_status = sla::PointsStatus::NoPoints;
        lower->clear_volumes();
        lower->input_file.clear();
    }

    // Because transformations are going to be applied to meshes directly,
    // we reset transformation of all instances and volumes,
    // except for translation and Z-rotation on instances, which are preserved
    // in the transformation matrix and not applied to the mesh transform.

    // const auto instance_matrix = instances[instance]->get_matrix(true);
    const auto instance_matrix = Geometry::assemble_transform(
        Vec3d::Zero(),  // don't apply offset
        instances[instance]->get_rotation().cwiseProduct(Vec3d(1.0, 1.0, 1.0)),   // BBS: do apply Z-rotation
        instances[instance]->get_scaling_factor(),
        instances[instance]->get_mirror()
    );

    // BBS
    //z -= instances[instance]->get_offset().z();
    for (Vec3d& point : plane_points) {
        point -= instances[instance]->get_offset();
    }

    // Displacement (in instance coordinates) to be applied to place the upper parts
    Vec3d local_displace = Vec3d::Zero();

    for (ModelVolume *volume : volumes) {
        const auto volume_matrix = volume->get_matrix();

        volume->supported_facets.reset();
        volume->seam_facets.reset();
        volume->mmu_segmentation_facets.reset();

        if (! volume->is_model_part()) {
            // Modifiers are not cut, but we still need to add the instance transformation
            // to the modifier volume transformation to preserve their shape properly.

            volume->set_transformation(Geometry::Transformation(instance_matrix * volume_matrix));

            if (attributes.has(ModelObjectCutAttribute::KeepUpper))
                upper->add_volume(*volume);
            if (attributes.has(ModelObjectCutAttribute::KeepLower))
                lower->add_volume(*volume);
        }
        else if (! volume->mesh().empty()) {            
            // Transform the mesh by the combined transformation matrix.
            // Flip the triangles in case the composite transformation is left handed.
			TriangleMesh mesh(volume->mesh());
			mesh.transform(instance_matrix * volume_matrix, true);
			volume->reset_mesh();
            // Reset volume transformation except for offset
            const Vec3d offset = volume->get_offset();
            volume->set_transformation(Geometry::Transformation());
            volume->set_offset(offset);

            // Perform cut
            TriangleMesh upper_mesh, lower_mesh;
            {
                indexed_triangle_set upper_its, lower_its;
                cut_mesh(mesh.its, plane_points, &upper_its, &lower_its);
                if (attributes.has(ModelObjectCutAttribute::KeepUpper))
                    upper_mesh = TriangleMesh(upper_its);
                if (attributes.has(ModelObjectCutAttribute::KeepLower))
                    lower_mesh = TriangleMesh(lower_its);
            }

            if (attributes.has(ModelObjectCutAttribute::KeepUpper) && ! upper_mesh.empty()) {
                ModelVolume* vol = upper->add_volume(upper_mesh);
                vol->name	= volume->name.substr(0, volume->name.find_last_of('.')) + "_upper"; // BBS
                // Don't copy the config's ID.
                vol->config.assign_config(volume->config);
    			assert(vol->config.id().valid());
	    		assert(vol->config.id() != volume->config.id());
                vol->set_material(volume->material_id(), *volume->material());
            }
            if (attributes.has(ModelObjectCutAttribute::KeepLower) && ! lower_mesh.empty()) {
                ModelVolume* vol = lower->add_volume(lower_mesh);
                vol->name = volume->name.substr(0, volume->name.find_last_of('.')) + "_lower"; // BBS
                // Don't copy the config's ID.
                vol->config.assign_config(volume->config);
                assert(vol->config.id().valid());
	    		assert(vol->config.id() != volume->config.id());
                vol->set_material(volume->material_id(), *volume->material());

                // Compute the displacement (in instance coordinates) to be applied to place the upper parts
                // The upper part displacement is set to half of the lower part bounding box
                // this is done in hope at least a part of the upper part will always be visible and draggable
                local_displace = lower->full_raw_mesh_bounding_box().size().cwiseProduct(Vec3d(-0.5, -0.5, 0.0));
            }
        }
    }

    ModelObjectPtrs res;

    if (attributes.has(ModelObjectCutAttribute::KeepUpper) && upper->volumes.size() > 0) {
        if (!upper->origin_translation.isApprox(Vec3d::Zero()) && instances[instance]->get_offset().isApprox(Vec3d::Zero())) {
            // BBS: do not move the parts if cut_to_parts
            if (!cut_to_parts) {
                upper->center_around_origin();
                upper->translate_instances(-upper->origin_translation);
                upper->origin_translation = Vec3d::Zero();
            }
        }

        // Reset instance transformation except offset and Z-rotation
        for (size_t i = 0; i < instances.size(); ++i) {
            auto &instance = upper->instances[i];
            const Vec3d offset = instance->get_offset();
            // BBS
            //const double rot_z = instance->get_rotation().z();
            // BBS: do not move the parts if cut_to_parts
            Vec3d displace(0, 0, 0);
            if (!cut_to_parts)
                displace = Geometry::assemble_transform(Vec3d::Zero(), instance->get_rotation()) * local_displace;

            instance->set_transformation(Geometry::Transformation());
            instance->set_offset(offset + displace);
            // BBS
            //instance->set_rotation(Vec3d(0.0, 0.0, rot_z));
        }

        res.push_back(upper);
    }
    if (attributes.has(ModelObjectCutAttribute::KeepLower) && lower->volumes.size() > 0) {
        if (!lower->origin_translation.isApprox(Vec3d::Zero()) && instances[instance]->get_offset().isApprox(Vec3d::Zero())) {
            if (!cut_to_parts) {
                lower->center_around_origin();
                lower->translate_instances(-lower->origin_translation);
                lower->origin_translation = Vec3d::Zero();
            }
        }

        // Reset instance transformation except offset and Z-rotation
        for (auto *instance : lower->instances) {
            const Vec3d offset = instance->get_offset();
            // BBS
            //const double rot_z = instance->get_rotation().z();
            instance->set_transformation(Geometry::Transformation());
            instance->set_offset(offset);
            // BBS
            //instance->set_rotation(Vec3d(attributes.has(ModelObjectCutAttribute::FlipLower) ? Geometry::deg2rad(180.0) : 0.0, 0.0, rot_z));
        }

        if(res.empty() || lower != res.back())
            res.push_back(lower);
    }

    BOOST_LOG_TRIVIAL(trace) << "ModelObject::cut - end";

    return res;
}

// BBS
ModelObjectPtrs ModelObject::segment(size_t instance, unsigned int max_extruders, double smoothing_alpha, int segment_number)
{
    BOOST_LOG_TRIVIAL(trace) << "ModelObject::segment - start";

    // Clone the object to duplicate instances, materials etc.
    ModelObject* upper = ModelObject::new_clone(*this);

    upper->set_model(nullptr);
    upper->sla_support_points.clear();
    upper->sla_drain_holes.clear();
    upper->sla_points_status = sla::PointsStatus::NoPoints;
    upper->clear_volumes();
    upper->input_file.clear();

    // Because transformations are going to be applied to meshes directly,
    // we reset transformation of all instances and volumes,
    // except for translation and Z-rotation on instances, which are preserved
    // in the transformation matrix and not applied to the mesh transform.

    // const auto instance_matrix = instances[instance]->get_matrix(true);
    const auto instance_matrix = Geometry::assemble_transform(
        Vec3d::Zero(),  // don't apply offset
        instances[instance]->get_rotation(),   // BBS: keep Z-rotation
        instances[instance]->get_scaling_factor(),
        instances[instance]->get_mirror()
    );

    for (ModelVolume* volume : volumes) {
        const auto volume_matrix = volume->get_matrix();

        volume->supported_facets.reset();
        volume->seam_facets.reset();

        if (!volume->is_model_part()) {
            // Modifiers are not cut, but we still need to add the instance transformation
            // to the modifier volume transformation to preserve their shape properly.
            volume->set_transformation(Geometry::Transformation(instance_matrix * volume_matrix));
            upper->add_volume(*volume); 
        }
        else if (!volume->mesh().empty()) {
            // Transform the mesh by the combined transformation matrix.
            // Flip the triangles in case the composite transformation is left handed.
            TriangleMesh mesh(volume->mesh());
            mesh.transform(instance_matrix * volume_matrix, true);
            volume->reset_mesh();

            auto mesh_segments = MeshBoolean::cgal::segment(mesh, smoothing_alpha, segment_number);


            // Reset volume transformation except for offset
            const Vec3d offset = volume->get_offset();
            volume->set_transformation(Geometry::Transformation());
            volume->set_offset(offset);

            unsigned int extruder_counter = 0;
            for (int idx=0;idx<mesh_segments.size();idx++)
            {
                auto& mesh_segment = mesh_segments[idx];

                if (mesh_segment.facets_count() > 0) {
                    ModelVolume* vol = upper->add_volume(mesh_segment);
                    vol->name = volume->name.substr(0, volume->name.find_last_of('.')) + "_" + std::to_string(idx);
                    // Don't copy the config's ID.
                    vol->config.assign_config(volume->config);
#if 0
                    assert(vol->config.id().valid());
                    assert(vol->config.id() != volume->config.id());
                    vol->set_material(volume->material_id(), *volume->material());
#else
                    vol->config.set("extruder", auto_extruder_id(max_extruders, extruder_counter));
#endif
                }
            }
        }
    }

    ModelObjectPtrs res;

    if (upper->volumes.size() > 0) {
        upper->invalidate_bounding_box();

        // Reset instance transformation except offset and Z-rotation
        for (size_t i = 0; i < instances.size(); i++) {
            auto& instance = upper->instances[i];
            const Vec3d offset = instance->get_offset();
            // BBS
            //const double rot_z = instance->get_rotation()(2);

            instance->set_transformation(Geometry::Transformation());
            instance->set_offset(offset);
            // BBS
            //instance->set_rotation(Vec3d(0.0, 0.0, rot_z));
        }

        res.push_back(upper);
    }

    BOOST_LOG_TRIVIAL(trace) << "ModelObject::segment - end";

    return res;
}

void ModelObject::split(ModelObjectPtrs* new_objects)
{
    std::vector<TriangleMesh> all_meshes;
    std::vector<Transform3d> all_transfos;
    std::vector<std::pair<int, int>> volume_mesh_counts;
    all_meshes.reserve(this->volumes.size() * 5);
    bool is_multi_volume_object = (this->volumes.size() > 1);

    for (int volume_idx = 0; volume_idx < this->volumes.size(); volume_idx++) {
        ModelVolume* volume = this->volumes[volume_idx];
        if (volume->type() != ModelVolumeType::MODEL_PART)
            continue;

        if (!is_multi_volume_object) {
            //BBS: not multi volume object, then split mesh.
            std::vector<TriangleMesh> volume_meshes = volume->mesh().split();
            int mesh_count = 0;
            for (TriangleMesh& mesh : volume_meshes) {
                if (mesh.facets_count() < 3)
                    continue;

                all_meshes.emplace_back(std::move(mesh));
                all_transfos.emplace_back(volume->get_matrix());
                mesh_count++;
            }
            volume_mesh_counts.push_back({ volume_idx, mesh_count });
        } else {
            //BBS: multi volume object, then only split to volume
            if (volume->mesh().facets_count() >= 3) {
                all_meshes.emplace_back(std::move(volume->mesh()));
                all_transfos.emplace_back(volume->get_matrix());
                volume_mesh_counts.push_back({ volume_idx, 1 });
            }
        }
    }

    FaceDetector face_detector(all_meshes, all_transfos, 1.0);
    face_detector.detect_exterior_face();

    int volume_mesh_begin = 0;
    for (int i = 0; i < volume_mesh_counts.size(); i++) {
        std::pair<int, int> mesh_info = volume_mesh_counts[i];
        ModelVolume* volume = this->volumes[mesh_info.first];

        std::vector<TriangleMesh> meshes;
        for (int mesh_idx = volume_mesh_begin; mesh_idx < volume_mesh_begin + mesh_info.second; mesh_idx++) {
            meshes.emplace_back(std::move(all_meshes[mesh_idx]));
        }
        volume_mesh_begin += mesh_info.second;

        size_t counter = 1;
        for (TriangleMesh& mesh : meshes) {
            // FIXME: crashes if not satisfied
            if (mesh.facets_count() < 3)
                continue;

            // XXX: this seems to be the only real usage of m_model, maybe refactor this so that it's not needed?
            ModelObject* new_object = m_model->add_object();
            //BBS: refine the config logic
            //use object as basic, and add volume's config
            if (meshes.size() == 1) {
                new_object->name = volume->name;
                // Don't copy the config's ID.
                //new_object->config.assign_config(this->config.size() > 0 ? this->config : volume->config);
            }
            else {
                new_object->name = this->name + (meshes.size() > 1 ? "_" + std::to_string(counter++) : "");
                // Don't copy the config's ID.
                //new_object->config.assign_config(this->config);
            }
            new_object->config.assign_config(this->config);
            new_object->config.apply(volume->config, true);

            assert(new_object->config.id().valid());
            assert(new_object->config.id() != this->config.id());
            new_object->instances.reserve(this->instances.size());
            for (const ModelInstance* model_instance : this->instances)
                new_object->add_instance(*model_instance);
            ModelVolume* new_vol = new_object->add_volume(*volume, std::move(mesh));

            if (is_multi_volume_object) {
                // BBS: volume geometry not changed, so we can keep the color paint facets
                if (new_vol->mmu_segmentation_facets.timestamp() == volume->mmu_segmentation_facets.timestamp())
                    new_vol->mmu_segmentation_facets.reset(); // BBS: let next assign take effect
                new_vol->mmu_segmentation_facets.assign(volume->mmu_segmentation_facets);
            }

            // BBS: clear volume's config, as we already set them into object
            new_vol->config.reset();

            for (ModelInstance* model_instance : new_object->instances)
            {
                Vec3d shift = model_instance->get_transformation().get_matrix(true) * new_vol->get_offset();
                model_instance->set_offset(model_instance->get_offset() + shift);
                //BBS: add assemble_view related logic
                model_instance->set_assemble_transformation(model_instance->get_transformation());
                model_instance->set_offset_to_assembly(new_vol->get_offset());
            }

            new_vol->set_offset(Vec3d::Zero());
            // reset the source to disable reload from disk
            new_vol->source = ModelVolume::Source();
            new_objects->emplace_back(new_object);
        }
    }
}


void ModelObject::merge()
{
    if (this->volumes.size() == 1) {
        // We can't merge meshes if there's just one volume
        return;
    }

    TriangleMesh mesh;

    for (ModelVolume* volume : volumes)
        if (!volume->mesh().empty())
            mesh.merge(volume->mesh());

    this->clear_volumes();
    ModelVolume* vol = this->add_volume(mesh);

    if (!vol)
        return;
}

ModelObjectPtrs ModelObject::merge_volumes(std::vector<int>& vol_indeces)
{
    ModelObjectPtrs res;
    if (this->volumes.size() == 1) {
        // We can't merge meshes if there's just one volume
        return res;
    }

    ModelObject* upper = ModelObject::new_clone(*this);
    upper->set_model(nullptr);
    upper->sla_support_points.clear();
    upper->sla_drain_holes.clear();
    upper->sla_points_status = sla::PointsStatus::NoPoints;
    upper->clear_volumes();
    upper->input_file.clear();

#if 1
    TriangleMesh mesh;
    for (int i : vol_indeces) {
        auto volume = volumes[i];
        if (!volume->mesh().empty()) {
            const auto volume_matrix = volume->get_matrix();
            TriangleMesh mesh_(volume->mesh());
            mesh_.transform(volume_matrix, true);
            volume->reset_mesh();

            mesh.merge(mesh_);
        }
    }
#else
    std::vector<TriangleMesh> meshes;
    for (int i : vol_indeces) {
        auto volume = volumes[i];
        if (!volume->mesh().empty())
            meshes.emplace_back(volume->mesh());
    }
    TriangleMesh mesh = MeshBoolean::cgal::merge(meshes);
#endif

    ModelVolume* vol = upper->add_volume(mesh);
    for (int i = 0; i < volumes.size();i++) {
        if (std::find(vol_indeces.begin(), vol_indeces.end(), i) != vol_indeces.end()) {
            vol->name = volumes[i]->name + "_merged";
            vol->config.assign_config(volumes[i]->config);
        }
        else
            upper->add_volume(*volumes[i]);
    }
    upper->invalidate_bounding_box();
    res.push_back(upper);
    return res;
}

// Support for non-uniform scaling of instances. If an instance is rotated by angles, which are not multiples of ninety degrees,
// then the scaling in world coordinate system is not representable by the Geometry::Transformation structure.
// This situation is solved by baking in the instance transformation into the mesh vertices.
// Rotation and mirroring is being baked in. In case the instance scaling was non-uniform, it is baked in as well.
void ModelObject::bake_xy_rotation_into_meshes(size_t instance_idx)
{
    assert(instance_idx < this->instances.size());

	const Geometry::Transformation reference_trafo = this->instances[instance_idx]->get_transformation();
    if (Geometry::is_rotation_ninety_degrees(reference_trafo.get_rotation()))
        // nothing to do, scaling in the world coordinate space is possible in the representation of Geometry::Transformation.
        return;

    bool   left_handed        = reference_trafo.is_left_handed();
    bool   has_mirrorring     = ! reference_trafo.get_mirror().isApprox(Vec3d(1., 1., 1.));
    bool   uniform_scaling    = std::abs(reference_trafo.get_scaling_factor().x() - reference_trafo.get_scaling_factor().y()) < EPSILON &&
                                std::abs(reference_trafo.get_scaling_factor().x() - reference_trafo.get_scaling_factor().z()) < EPSILON;
    double new_scaling_factor = uniform_scaling ? reference_trafo.get_scaling_factor().x() : 1.;

    // Adjust the instances.
    for (size_t i = 0; i < this->instances.size(); ++ i) {
        ModelInstance &model_instance = *this->instances[i];
        model_instance.set_rotation(Vec3d(0., 0., Geometry::rotation_diff_z(reference_trafo.get_rotation(), model_instance.get_rotation())));
        model_instance.set_scaling_factor(Vec3d(new_scaling_factor, new_scaling_factor, new_scaling_factor));
        model_instance.set_mirror(Vec3d(1., 1., 1.));
    }

    // Adjust the meshes.
    // Transformation to be applied to the meshes.
    Eigen::Matrix3d mesh_trafo_3x3           = reference_trafo.get_matrix(true, false, uniform_scaling, ! has_mirrorring).matrix().block<3, 3>(0, 0);
	Transform3d     volume_offset_correction = this->instances[instance_idx]->get_transformation().get_matrix().inverse() * reference_trafo.get_matrix();
    for (ModelVolume *model_volume : this->volumes) {
        const Geometry::Transformation volume_trafo = model_volume->get_transformation();
        bool   volume_left_handed        = volume_trafo.is_left_handed();
        bool   volume_has_mirrorring     = ! volume_trafo.get_mirror().isApprox(Vec3d(1., 1., 1.));
        bool   volume_uniform_scaling    = std::abs(volume_trafo.get_scaling_factor().x() - volume_trafo.get_scaling_factor().y()) < EPSILON &&
                                           std::abs(volume_trafo.get_scaling_factor().x() - volume_trafo.get_scaling_factor().z()) < EPSILON;
        double volume_new_scaling_factor = volume_uniform_scaling ? volume_trafo.get_scaling_factor().x() : 1.;
        // Transform the mesh.
		Matrix3d volume_trafo_3x3 = volume_trafo.get_matrix(true, false, volume_uniform_scaling, !volume_has_mirrorring).matrix().block<3, 3>(0, 0);
        // Following method creates a new shared_ptr<TriangleMesh>
		model_volume->transform_this_mesh(mesh_trafo_3x3 * volume_trafo_3x3, left_handed != volume_left_handed);
        // Reset the rotation, scaling and mirroring.
        model_volume->set_rotation(Vec3d(0., 0., 0.));
        model_volume->set_scaling_factor(Vec3d(volume_new_scaling_factor, volume_new_scaling_factor, volume_new_scaling_factor));
        model_volume->set_mirror(Vec3d(1., 1., 1.));
        // Move the reference point of the volume to compensate for the change of the instance trafo.
        model_volume->set_offset(volume_offset_correction * volume_trafo.get_offset());
        // reset the source to disable reload from disk
        model_volume->source = ModelVolume::Source();
    }

    this->invalidate_bounding_box();
}

double ModelObject::get_min_z() const
{
    if (instances.empty())
        return 0.0;
    else {
        double min_z = DBL_MAX;
        for (size_t i = 0; i < instances.size(); ++i) {
            min_z = std::min(min_z, get_instance_min_z(i));
        }
        return min_z;
    }
}

double ModelObject::get_max_z() const
{
    if (instances.empty())
        return 0.0;
    else {
        double max_z = -DBL_MAX;
        for (size_t i = 0; i < instances.size(); ++i) {
            max_z = std::max(max_z, get_instance_max_z(i));
        }
        return max_z;
    }
}

double ModelObject::get_instance_min_z(size_t instance_idx) const
{
    double min_z = DBL_MAX;

    const ModelInstance* inst = instances[instance_idx];
    const Transform3d& mi = inst->get_matrix(true);

    for (const ModelVolume* v : volumes) {
        if (!v->is_model_part())
            continue;

        const Transform3d mv = mi * v->get_matrix();
        const TriangleMesh& hull = v->get_convex_hull();
        //BBS: in some case the convex hull is empty due to the qhull algo
        //use the original mesh instead
        //TODO: when the vertex's x/y/z are all the same, the run_qhull can not get correct result
        //we need to find another algo then
        if (hull.its.indices.size() == 0) {
            const TriangleMesh& mesh = v->mesh();
            for (const stl_triangle_vertex_indices& facet : mesh.its.indices)
                for (int i = 0; i < 3; ++i)
                    min_z = std::min(min_z, (mv * mesh.its.vertices[facet[i]].cast<double>()).z());
        }
        else {
            for (const stl_triangle_vertex_indices& facet : hull.its.indices)
                for (int i = 0; i < 3; ++i)
                    min_z = std::min(min_z, (mv * hull.its.vertices[facet[i]].cast<double>()).z());
        }
    }

    //BBS: add some logic to avoid wrong compute for min_z
    if (min_z == DBL_MAX)
        min_z = 0;
    return min_z + inst->get_offset(Z);
}

double ModelObject::get_instance_max_z(size_t instance_idx) const
{
    double max_z = -DBL_MAX;

    const ModelInstance* inst = instances[instance_idx];
    const Transform3d& mi = inst->get_matrix(true);

    for (const ModelVolume* v : volumes) {
        if (!v->is_model_part())
            continue;

        const Transform3d mv = mi * v->get_matrix();
        const TriangleMesh& hull = v->get_convex_hull();
        for (const stl_triangle_vertex_indices& facet : hull.its.indices)
            for (int i = 0; i < 3; ++i)
                max_z = std::max(max_z, (mv * hull.its.vertices[facet[i]].cast<double>()).z());
    }

    return max_z + inst->get_offset(Z);
}

unsigned int ModelObject::update_instances_print_volume_state(const BuildVolume &build_volume)
{
    unsigned int num_printable = 0;
    enum {
        INSIDE = 1,
        OUTSIDE = 2
    };

    //BBS: add logs for build_volume
    //const BoundingBoxf3& print_volume = build_volume.bounding_volume();
    //BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", print_volume {%1%, %2%, %3%} to {%4%, %5%, %6%}")\
    //    %print_volume.min.x() %print_volume.min.y() %print_volume.min.z()%print_volume.max.x() %print_volume.max.y() %print_volume.max.z();
    for (ModelInstance* model_instance : this->instances) {
        unsigned int inside_outside = 0;
        for (const ModelVolume* vol : this->volumes)
            if (vol->is_model_part()) {
                //BBS: add bounding box empty check logic, for some volume is empty before split(it will be removed after split to object)
                BoundingBoxf3 bb = vol->get_convex_hull().bounding_box();
                Vec3d size = bb.size();
                if ((size.x() == 0.f) || (size.y() == 0.f) || (size.z() == 0.f)) {
                    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", object %1%'s vol %2% is empty, skip it, box: {%3%, %4%, %5%} to {%6%, %7%, %8%}")%this->name %vol->name\
                        %bb.min.x() %bb.min.y() %bb.min.z()%bb.max.x() %bb.max.y() %bb.max.z();
                    continue;
                }

                const Transform3d matrix = model_instance->get_matrix() * vol->get_matrix();
                BuildVolume::ObjectState state = build_volume.object_state(vol->mesh().its, matrix.cast<float>(), true /* may be below print bed */);
                if (state == BuildVolume::ObjectState::Inside)
                    // Volume is completely inside.
                    inside_outside |= INSIDE;
                else if (state == BuildVolume::ObjectState::Outside)
                    // Volume is completely outside.
                    inside_outside |= OUTSIDE;
                else if (state == BuildVolume::ObjectState::Below) {
                    // Volume below the print bed, thus it is completely outside, however this does not prevent the object to be printable
                    // if some of its volumes are still inside the build volume.
                } else
                    // Volume colliding with the build volume.
                    inside_outside |= INSIDE | OUTSIDE;
            }
        model_instance->print_volume_state =
            inside_outside == (INSIDE | OUTSIDE) ? ModelInstancePVS_Partly_Outside :
            inside_outside == INSIDE ? ModelInstancePVS_Inside : ModelInstancePVS_Fully_Outside;
        if (inside_outside == INSIDE) {
            //BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", object %1%'s instance inside print volum")%this->name;
            ++num_printable;
        }
    }
    //BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", found %1% printable instances")%num_printable;
    return num_printable;
}

void ModelObject::print_info() const
{
    using namespace std;
    cout << fixed;
    boost::nowide::cout << "[" << boost::filesystem::path(this->input_file).filename().string() << "]" << endl;
    
    TriangleMesh mesh = this->raw_mesh();
    BoundingBoxf3 bb = mesh.bounding_box();
    Vec3d size = bb.size();
    cout << "size_x = " << size(0) << endl;
    cout << "size_y = " << size(1) << endl;
    cout << "size_z = " << size(2) << endl;
    cout << "min_x = " << bb.min(0) << endl;
    cout << "min_y = " << bb.min(1) << endl;
    cout << "min_z = " << bb.min(2) << endl;
    cout << "max_x = " << bb.max(0) << endl;
    cout << "max_y = " << bb.max(1) << endl;
    cout << "max_z = " << bb.max(2) << endl;
    cout << "number_of_facets = " << mesh.facets_count() << endl;

    cout << "manifold = "   << (mesh.stats().manifold() ? "yes" : "no") << endl;
    if (! mesh.stats().manifold())
        cout << "open_edges = " << mesh.stats().open_edges << endl;
    
    if (mesh.stats().repaired()) {
        const RepairedMeshErrors& stats = mesh.stats().repaired_errors;
        if (stats.degenerate_facets > 0)
            cout << "degenerate_facets = "  << stats.degenerate_facets << endl;
        if (stats.edges_fixed > 0)
            cout << "edges_fixed = "        << stats.edges_fixed       << endl;
        if (stats.facets_removed > 0)
            cout << "facets_removed = "     << stats.facets_removed    << endl;
        if (stats.facets_reversed > 0)
            cout << "facets_reversed = "    << stats.facets_reversed   << endl;
        if (stats.backwards_edges > 0)
            cout << "backwards_edges = "    << stats.backwards_edges   << endl;
    }
    cout << "number_of_parts =  " << mesh.stats().number_of_parts << endl;
    cout << "volume = "           << mesh.volume()                << endl;
}

std::string ModelObject::get_export_filename() const
{
    std::string ret = input_file;

    if (!name.empty())
    {
        if (ret.empty())
            // input_file was empty, just use name
            ret = name;
        else
        {
            // Replace file name in input_file with name, but keep the path and file extension.
            ret = (boost::filesystem::path(name).parent_path().empty()) ?
                (boost::filesystem::path(ret).parent_path() / name).make_preferred().string() : name;
        }
    }

    return ret;
}

TriangleMeshStats ModelObject::get_object_stl_stats() const
{
    TriangleMeshStats full_stats;
    full_stats.volume = 0.f;

    // fill full_stats from all objet's meshes
    for (ModelVolume* volume : this->volumes)
    {
        const TriangleMeshStats& stats = volume->mesh().stats();

        // initialize full_stats (for repaired errors)
        full_stats.open_edges           += stats.open_edges;
        full_stats.repaired_errors.merge(stats.repaired_errors);

        // another used satistics value
        if (volume->is_model_part()) {
            Transform3d trans = instances.empty() ? volume->get_matrix() : (volume->get_matrix() * instances[0]->get_matrix());
            full_stats.volume           += stats.volume * std::fabs(trans.matrix().block(0, 0, 3, 3).determinant());
            full_stats.number_of_parts  += stats.number_of_parts;
        }
    }

    return full_stats;
}

int ModelObject::get_repaired_errors_count(const int vol_idx /*= -1*/) const
{
    if (vol_idx >= 0)
        return this->volumes[vol_idx]->get_repaired_errors_count();

    const RepairedMeshErrors& stats = get_object_stl_stats().repaired_errors;

    return  stats.degenerate_facets + stats.edges_fixed     + stats.facets_removed +
            stats.facets_reversed + stats.backwards_edges;
}

void ModelVolume::set_material_id(t_model_material_id material_id)
{
    m_material_id = material_id;
    // ensure m_material_id references an existing material
    if (! material_id.empty())
        this->object->get_model()->add_material(material_id);
}

ModelMaterial* ModelVolume::material() const
{ 
    return this->object->get_model()->get_material(m_material_id);
}

void ModelVolume::set_material(t_model_material_id material_id, const ModelMaterial &material)
{
    m_material_id = material_id;
    if (! material_id.empty())
        this->object->get_model()->add_material(material_id, material);
}

// Extract the current extruder ID based on this ModelVolume's config and the parent ModelObject's config.
int ModelVolume::extruder_id() const
{
    int extruder_id = -1;
    //if (this->is_model_part())
    {
        const ConfigOption *opt = this->config.option("extruder");
        if ((opt == nullptr) || (opt->getInt() == 0))
            opt = this->object->config.option("extruder");
        extruder_id = (opt == nullptr) ? 1 : opt->getInt();
    }
    return extruder_id;
}

bool ModelVolume::is_splittable() const
{
    // the call mesh.is_splittable() is expensive, so cache the value to calculate it only once
    if (m_is_splittable == -1)
        m_is_splittable = its_is_splittable(this->mesh().its);

    return m_is_splittable == 1;
}

// BBS
std::vector<int> ModelVolume::get_extruders() const
{
    if (mmu_segmentation_facets.timestamp() != mmuseg_ts) {
        std::vector<indexed_triangle_set> its_per_type;
        mmuseg_extruders.clear();
        mmuseg_ts = mmu_segmentation_facets.timestamp();
        mmu_segmentation_facets.get_facets(*this, its_per_type);
        for (int idx = 1; idx < its_per_type.size(); idx++) {
            indexed_triangle_set& its = its_per_type[idx];
            if (its.indices.empty())
                continue;

            mmuseg_extruders.push_back(idx);
        }
    }

    std::vector<int> volume_extruders = mmuseg_extruders;

    int volume_extruder_id = this->extruder_id();
    if (volume_extruder_id > 0)
        volume_extruders.push_back(volume_extruder_id);

    return volume_extruders;
}

void ModelVolume::update_extruder_count(size_t extruder_count)
{
    std::vector<int> used_extruders = get_extruders();
    for (int extruder_id : used_extruders) {
        if (extruder_id > extruder_count) {
            mmu_segmentation_facets.set_enforcer_block_type_limit(*this, (EnforcerBlockerType)extruder_count);
            break;
        }
    }
}

void ModelVolume::center_geometry_after_creation(bool update_source_offset)
{
    Vec3d shift = this->mesh().bounding_box().center();
    if (!shift.isApprox(Vec3d::Zero()))
    {
    	if (m_mesh)
        	const_cast<TriangleMesh*>(m_mesh.get())->translate(-(float)shift(0), -(float)shift(1), -(float)shift(2));
        if (m_convex_hull)
			const_cast<TriangleMesh*>(m_convex_hull.get())->translate(-(float)shift(0), -(float)shift(1), -(float)shift(2));
        translate(shift);
    }

    if (update_source_offset)
        source.mesh_offset = shift;
}

void ModelVolume::calculate_convex_hull()
{
    m_convex_hull = std::make_shared<TriangleMesh>(this->mesh().convex_hull_3d());
    assert(m_convex_hull.get());
}

//BBS: convex_hull_2d using convex_hull_3d
void  ModelVolume::calculate_convex_hull_2d(const Geometry::Transformation &transformation) const
{
    const indexed_triangle_set &its = m_convex_hull->its;
	if (its.vertices.empty())
        return;

    Points pts;
    Vec3d rotation = transformation.get_rotation();
    Vec3d mirror = transformation.get_mirror();
    Vec3d scale = transformation.get_scaling_factor();
    //rotation(2) = 0.f;
    Transform3d new_matrix = Geometry::assemble_transform(Vec3d::Zero(), rotation, scale, mirror);

    pts.reserve(its.vertices.size());
    // Using the shared vertices should be a bit quicker than using the STL faces.
    for (size_t i = 0; i < its.vertices.size(); ++ i) {
        Vec3d p = new_matrix * its.vertices[i].cast<double>();
        pts.emplace_back(coord_t(scale_(p.x())), coord_t(scale_(p.y())));
    }
    //TODO, do we need to remove the duplicate points before convex_hull?
    m_cached_2d_polygon = Slic3r::Geometry::convex_hull(pts);

    m_convex_hull_2d = m_cached_2d_polygon;
    m_convex_hull_2d.translate(scale_(transformation.get_offset(X)), scale_(transformation.get_offset(Y)));
    //int size = m_cached_2d_polygon.size();
    //BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": size %1%, offset {%2%, %3%}")% size% transformation.get_offset(X)% transformation.get_offset(Y);
    //for (int i = 0; i < size; i++)
    //    BOOST_LOG_TRIVIAL(info) << boost::format(": point %1%, position {%2%, %3%}")% i% m_cached_2d_polygon[i].x()% m_cached_2d_polygon[i].y();
	//m_convex_hull_2d.rotate(transformation.get_rotation(Z));
    //m_convex_hull_2d.scale(transformation.get_scaling_factor(X), transformation.get_scaling_factor(Y));
}

const Polygon& ModelVolume::get_convex_hull_2d(const Transform3d &trafo_instance) const
{
    Transform3d  new_matrix;

    new_matrix = trafo_instance * m_transformation.get_matrix();

    auto need_recompute = [](Geometry::Transformation& old_transform, Geometry::Transformation& new_transform)->bool {
            //double old_rot_x = old_transform.get_rotation(X);
            //double old_rot_y = old_transform.get_rotation(Y);
            //double new_rot_x = new_transform.get_rotation(X);
            //double new_rot_y = new_transform.get_rotation(Y);
            const Vec3d &old_rotation = old_transform.get_rotation();
            const Vec3d &new_rotation = new_transform.get_rotation();
            const Vec3d &old_mirror = old_transform.get_mirror();
            const Vec3d &new_mirror = new_transform.get_mirror();
            const Vec3d &old_scaling = old_transform.get_scaling_factor();
            const Vec3d &new_scaling = new_transform.get_scaling_factor();

            if ((old_scaling != new_scaling) || (old_rotation != new_rotation) || (old_mirror != new_mirror))
                return true;
            else
                return false;
        };

    if ((new_matrix.matrix() != m_cached_trans_matrix.matrix()) || !m_convex_hull_2d.is_valid())
    {
        Geometry::Transformation new_trans(new_matrix), old_trans(m_cached_trans_matrix);

        if (need_recompute(old_trans, new_trans) || !m_convex_hull_2d.is_valid())
        {
            //need to update
            calculate_convex_hull_2d(new_trans);
        }
        else
        {
            m_convex_hull_2d = m_cached_2d_polygon;
            m_convex_hull_2d.translate(scale_(new_trans.get_offset(X)), scale_(new_trans.get_offset(Y)));
            //m_convex_hull_2d.rotate(new_trans.get_rotation(Z));
            //m_convex_hull_2d.scale(new_trans.get_scaling_factor(X), new_trans.get_scaling_factor(Y));
            //int size = m_cached_2d_polygon.size();
            //BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": use previous cached, size %1%, offset {%2%, %3%}")% size% new_trans.get_offset(X)% new_trans.get_offset(Y);
            //for (int i = 0; i < size; i++)
            //    BOOST_LOG_TRIVIAL(info) << boost::format(": point %1%, position {%2%, %3%}")% i% m_cached_2d_polygon[i].x()% m_cached_2d_polygon[i].y();
        }
        m_cached_trans_matrix = new_matrix;
    }

    return m_convex_hull_2d;
}

int ModelVolume::get_repaired_errors_count() const
{
    const RepairedMeshErrors &stats = this->mesh().stats().repaired_errors;

    return  stats.degenerate_facets + stats.edges_fixed     + stats.facets_removed +
            stats.facets_reversed + stats.backwards_edges;
}

const TriangleMesh& ModelVolume::get_convex_hull() const
{
    return *m_convex_hull.get();
}

//BBS: refine the model part names
ModelVolumeType ModelVolume::type_from_string(const std::string &s)
{
    // New type (supporting the support enforcers & blockers)
    if (s == "normal_part")
		return ModelVolumeType::MODEL_PART;
    if (s == "negative_part")
        return ModelVolumeType::NEGATIVE_VOLUME;
    if (s == "modifier_part")
		return ModelVolumeType::PARAMETER_MODIFIER;
    if (s == "support_enforcer")
		return ModelVolumeType::SUPPORT_ENFORCER;
    if (s == "support_blocker")
		return ModelVolumeType::SUPPORT_BLOCKER;
    //assert(s == "0");
    // Default value if invalud type string received.
	return ModelVolumeType::MODEL_PART;
}

//BBS: refine the model part names
std::string ModelVolume::type_to_string(const ModelVolumeType t)
{
    switch (t) {
	case ModelVolumeType::MODEL_PART:         return "normal_part";
    case ModelVolumeType::NEGATIVE_VOLUME:    return "negative_part";
	case ModelVolumeType::PARAMETER_MODIFIER: return "modifier_part";
	case ModelVolumeType::SUPPORT_ENFORCER:   return "support_enforcer";
	case ModelVolumeType::SUPPORT_BLOCKER:    return "support_blocker";
    default:
        assert(false);
        return "normal_part";
    }
}

// Split this volume, append the result to the object owning this volume.
// Return the number of volumes created from this one.
// This is useful to assign different materials to different volumes of an object.
size_t ModelVolume::split(unsigned int max_extruders)
{
    std::vector<TriangleMesh> meshes = this->mesh().split();
    if (meshes.size() <= 1)
        return 1;

    size_t idx = 0;
    size_t ivolume = std::find(this->object->volumes.begin(), this->object->volumes.end(), this) - this->object->volumes.begin();
    const std::string name = this->name;

    unsigned int extruder_counter = 0;
    const Vec3d offset = this->get_offset();

    for (TriangleMesh &mesh : meshes) {
        if (mesh.empty())
            // Repair may have removed unconnected triangles, thus emptying the mesh.
            continue;

        if (idx == 0) {
            this->set_mesh(std::move(mesh));
            this->calculate_convex_hull();
            // Assign a new unique ID, so that a new GLVolume will be generated.
            this->set_new_unique_id();
            // reset the source to disable reload from disk
            this->source = ModelVolume::Source();

            // BBS: reset facet annotations
            this->mmu_segmentation_facets.reset();
            this->exterior_facets.reset();
            this->supported_facets.reset();
            this->seam_facets.reset();
        }
        else
            this->object->volumes.insert(this->object->volumes.begin() + (++ivolume), new ModelVolume(object, *this, std::move(mesh)));

        this->object->volumes[ivolume]->set_offset(Vec3d::Zero());
        this->object->volumes[ivolume]->center_geometry_after_creation();
        this->object->volumes[ivolume]->translate(offset);
        this->object->volumes[ivolume]->name = name + "_" + std::to_string(idx + 1);
        //BBS: always set the extruder id the same as original
        this->object->volumes[ivolume]->config.set("extruder", this->extruder_id());
        //this->object->volumes[ivolume]->config.set("extruder", auto_extruder_id(max_extruders, extruder_counter));
        this->object->volumes[ivolume]->m_is_splittable = 0;
        ++ idx;
    }

    // discard volumes for which the convex hull was not generated or is degenerate
    size_t i = 0;
    while (i < this->object->volumes.size()) {
        const std::shared_ptr<const TriangleMesh> &hull = this->object->volumes[i]->get_convex_hull_shared_ptr();
        if (hull == nullptr || hull->its.vertices.empty() || hull->its.indices.empty()) {
            this->object->delete_volume(i);
            --idx;
            --i;
        }
        ++i;
    }

    return idx;
}

void ModelVolume::translate(const Vec3d& displacement)
{
    set_offset(get_offset() + displacement);
}

void ModelVolume::scale(const Vec3d& scaling_factors)
{
    set_scaling_factor(get_scaling_factor().cwiseProduct(scaling_factors));
}

void ModelObject::scale_to_fit(const Vec3d &size)
{
    Vec3d orig_size = this->bounding_box().size();
    double factor = std::min(
        size.x() / orig_size.x(),
        std::min(
            size.y() / orig_size.y(),
            size.z() / orig_size.z()
        )
    );
    this->scale(factor);
}

void ModelVolume::assign_new_unique_ids_recursive()
{
    ObjectBase::set_new_unique_id();
    config.set_new_unique_id();
    supported_facets.set_new_unique_id();
    seam_facets.set_new_unique_id();
    mmu_segmentation_facets.set_new_unique_id();
}

void ModelVolume::rotate(double angle, Axis axis)
{
    switch (axis)
    {
    case X: { rotate(angle, Vec3d::UnitX()); break; }
    case Y: { rotate(angle, Vec3d::UnitY()); break; }
    case Z: { rotate(angle, Vec3d::UnitZ()); break; }
    default: break;
    }
}

void ModelVolume::rotate(double angle, const Vec3d& axis)
{
    set_rotation(get_rotation() + Geometry::extract_euler_angles(Eigen::Quaterniond(Eigen::AngleAxisd(angle, axis)).toRotationMatrix()));
}

void ModelVolume::mirror(Axis axis)
{
    Vec3d mirror = get_mirror();
    switch (axis)
    {
    case X: { mirror(0) *= -1.0; break; }
    case Y: { mirror(1) *= -1.0; break; }
    case Z: { mirror(2) *= -1.0; break; }
    default: break;
    }
    set_mirror(mirror);
}

// This method could only be called before the meshes of this ModelVolumes are not shared!
void ModelVolume::scale_geometry_after_creation(const Vec3f& versor)
{
	const_cast<TriangleMesh*>(m_mesh.get())->scale(versor);
    if (m_convex_hull->empty())
        //BBS: recompute the convex hull if it is null for previous too small
        this->calculate_convex_hull();
    else
        const_cast<TriangleMesh*>(m_convex_hull.get())->scale(versor);
}

void ModelVolume::transform_this_mesh(const Transform3d &mesh_trafo, bool fix_left_handed)
{
	TriangleMesh mesh = this->mesh();
	mesh.transform(mesh_trafo, fix_left_handed);
	this->set_mesh(std::move(mesh));
    TriangleMesh convex_hull = this->get_convex_hull();
    convex_hull.transform(mesh_trafo, fix_left_handed);
    m_convex_hull = std::make_shared<TriangleMesh>(std::move(convex_hull));
    // Let the rest of the application know that the geometry changed, so the meshes have to be reloaded.
    this->set_new_unique_id();
}

void ModelVolume::transform_this_mesh(const Matrix3d &matrix, bool fix_left_handed)
{
	TriangleMesh mesh = this->mesh();
	mesh.transform(matrix, fix_left_handed);
	this->set_mesh(std::move(mesh));
    TriangleMesh convex_hull = this->get_convex_hull();
    convex_hull.transform(matrix, fix_left_handed);
    m_convex_hull = std::make_shared<TriangleMesh>(std::move(convex_hull));
    // Let the rest of the application know that the geometry changed, so the meshes have to be reloaded.
    this->set_new_unique_id();
}

void ModelVolume::convert_from_imperial_units()
{
    assert(! this->source.is_converted_from_meters);
    this->scale_geometry_after_creation(25.4f);
    this->set_offset(Vec3d(0, 0, 0));
    this->source.is_converted_from_inches = true;
}

void ModelVolume::convert_from_meters()
{
    assert(! this->source.is_converted_from_inches);
    this->scale_geometry_after_creation(1000.f);
    this->set_offset(Vec3d(0, 0, 0));
    this->source.is_converted_from_meters = true;
}

void ModelInstance::transform_mesh(TriangleMesh* mesh, bool dont_translate) const
{
    mesh->transform(get_matrix(dont_translate));
}

BoundingBoxf3 ModelInstance::transform_mesh_bounding_box(const TriangleMesh& mesh, bool dont_translate) const
{
    // Rotate around mesh origin.
    TriangleMesh copy(mesh);
    copy.transform(get_matrix(true, false, true, true));
    BoundingBoxf3 bbox = copy.bounding_box();

    if (!empty(bbox)) {
        // Scale the bounding box along the three axes.
        for (unsigned int i = 0; i < 3; ++i)
        {
            if (std::abs(get_scaling_factor((Axis)i)-1.0) > EPSILON)
            {
                bbox.min(i) *= get_scaling_factor((Axis)i);
                bbox.max(i) *= get_scaling_factor((Axis)i);
            }
        }

        // Translate the bounding box.
        if (! dont_translate) {
            bbox.min += get_offset();
            bbox.max += get_offset();
        }
    }
    return bbox;
}

BoundingBoxf3 ModelInstance::transform_bounding_box(const BoundingBoxf3 &bbox, bool dont_translate) const
{
    return bbox.transformed(get_matrix(dont_translate));
}

Vec3d ModelInstance::transform_vector(const Vec3d& v, bool dont_translate) const
{
    return get_matrix(dont_translate) * v;
}

void ModelInstance::transform_polygon(Polygon* polygon) const
{
    // CHECK_ME -> Is the following correct or it should take in account all three rotations ?
    polygon->rotate(get_rotation(Z)); // rotate around polygon origin
    // CHECK_ME -> Is the following correct ?
    polygon->scale(get_scaling_factor(X), get_scaling_factor(Y)); // scale around polygon origin
}

//BBS
// update the maxSpeed of an object if it is different from the global configuration
double Model::findMaxSpeed(const ModelObject* object) {
    auto objectKeys = object->config.keys();
    double objMaxSpeed = -1.;
    if (objectKeys.empty())
        return Model::printSpeedMap.maxSpeed;
    double perimeterSpeedObj = Model::printSpeedMap.perimeterSpeed;
    double externalPerimeterSpeedObj = Model::printSpeedMap.externalPerimeterSpeed;
    double infillSpeedObj = Model::printSpeedMap.infillSpeed;
    double solidInfillSpeedObj = Model::printSpeedMap.solidInfillSpeed;
    double topSolidInfillSpeedObj = Model::printSpeedMap.topSolidInfillSpeed;
    double supportSpeedObj = Model::printSpeedMap.supportSpeed;
    for (std::string objectKey : objectKeys) {
        if (objectKey == "inner_wall_speed"){
            perimeterSpeedObj = object->config.opt_float(objectKey);
            externalPerimeterSpeedObj = Model::printSpeedMap.externalPerimeterSpeed / Model::printSpeedMap.perimeterSpeed * perimeterSpeedObj;
        }
        if (objectKey == "sparse_infill_speed")
            infillSpeedObj = object->config.opt_float(objectKey);
        if (objectKey == "internal_solid_infill_speed")
            solidInfillSpeedObj = object->config.opt_float(objectKey);
        if (objectKey == "top_surface_speed")
            topSolidInfillSpeedObj = object->config.opt_float(objectKey);
        if (objectKey == "support_speed")
            supportSpeedObj = object->config.opt_float(objectKey);
        if (objectKey == "outer_wall_speed")
            externalPerimeterSpeedObj = object->config.opt_float(objectKey);
    }
    objMaxSpeed = std::max(perimeterSpeedObj, std::max(externalPerimeterSpeedObj, std::max(infillSpeedObj, std::max(solidInfillSpeedObj, std::max(topSolidInfillSpeedObj, std::max(supportSpeedObj, objMaxSpeed))))));
    if (objMaxSpeed <= 0) objMaxSpeed = 250.;
    return objMaxSpeed;
}

// BBS: thermal length is calculated according to the material of a volume
double Model::getThermalLength(const ModelVolume* modelVolumePtr) {
    double thermalLength = 200.;
    auto aa = modelVolumePtr->extruder_id();
    if (Model::extruderParamsMap.find(aa) != Model::extruderParamsMap.end()) {
        if (Model::extruderParamsMap.at(aa).materialName == "ABS" ||
            Model::extruderParamsMap.at(aa).materialName == "PA-CF" ||
            Model::extruderParamsMap.at(aa).materialName == "PET-CF") {
            thermalLength = 100;
        }
        if (Model::extruderParamsMap.at(aa).materialName == "PC") {
            thermalLength = 40;
        }
        if (Model::extruderParamsMap.at(aa).materialName == "TPU") {
            thermalLength = 1000;
        }

    }
    return thermalLength;
}

// BBS: thermal length calculation for a group of volumes
double Model::getThermalLength(const std::vector<ModelVolume*> modelVolumePtrs)
{
    double thermalLength = 1250.;

    for (const auto& modelVolumePtr : modelVolumePtrs) {
        if (modelVolumePtr != nullptr) {
            // the thermal length of a group is decided by the volume with shortest thermal length
            thermalLength = std::min(thermalLength, getThermalLength(modelVolumePtr));
        }
    }
    return thermalLength;
}
// max printing speed, difference in bed temperature and envirument temperature and bed adhension coefficients are considered 
double ModelInstance::get_auto_brim_width(double deltaT, double adhension) const
{
    BoundingBoxf3 raw_bbox = object->raw_mesh_bounding_box();
    double maxSpeed = Model::findMaxSpeed(object);

    auto bbox_size = transform_bounding_box(raw_bbox).size();
    double height_to_area = std::max(bbox_size(2) / (bbox_size(0) * bbox_size(0) * bbox_size(1)),
        bbox_size(2) / (bbox_size(1) * bbox_size(1) * bbox_size(0)));
    double thermalLength = sqrt(bbox_size(0)* bbox_size(0) + bbox_size(1)* bbox_size(1));
    double thermalLengthRef = Model::getThermalLength(object->volumes);

    double brim_width = adhension * std::min(std::min(std::max(height_to_area * 200 * maxSpeed/200, thermalLength * 8. / thermalLengthRef * std::min(bbox_size(2), 30.) / 30.), 20.), 1.5 * thermalLength);
    // small brims are omitted
    if (brim_width < 5 && brim_width < 1.5 * thermalLength)
        brim_width = 0;
    return brim_width;
}

//BBS: instance's convex_hull_2d
Polygon ModelInstance::convex_hull_2d()
{
    //BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": name %1%, is_valid %2%")% this->object->name.c_str()% convex_hull.is_valid();
    //if (!convex_hull.is_valid())
    { // this logic is not working right now, as moving instance doesn't update convex_hull
        const Transform3d& trafo_instance = get_matrix(false);
        convex_hull = get_object()->convex_hull_2d(trafo_instance);
    }
    //int size = convex_hull.size();
    //BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": convex_hull, point size %1%")% size;
    //for (int i = 0; i < size; i++)
    //    BOOST_LOG_TRIVIAL(info) << boost::format(": point %1%, position {%2%, %3%}")% i% convex_hull[i].x()% convex_hull[i].y();

    return convex_hull;
}

//BBS: invalidate instance's convex_hull_2d
void ModelInstance::invalidate_convex_hull_2d()
{
    convex_hull.clear();
}

//BBS adhesion coefficients from model object class
double getadhesionCoeff(const ModelVolumePtrs objectVolumes)
{
    double adhesionCoeff = 1;
    for (const ModelVolume* modelVolume : objectVolumes) {
        if (Model::extruderParamsMap.find(modelVolume->extruder_id()) != Model::extruderParamsMap.end())
            if (Model::extruderParamsMap.at(modelVolume->extruder_id()).materialName == "PETG") {
                adhesionCoeff = 2;
            }
            else if (Model::extruderParamsMap.at(modelVolume->extruder_id()).materialName == "TPU") {
                adhesionCoeff = 0.5;
            }
    }
    return adhesionCoeff;
}

//BBS maximum temperature difference from model object class
double getTemperatureFromExtruder(const ModelVolumePtrs objectVolumes) {
    // BBS: FIXME
#if 1
    std::vector<size_t> extruders;
    for (const ModelVolume* modelVolume : objectVolumes) {
        if (modelVolume->extruder_id() >= 0)
            extruders.push_back(modelVolume->extruder_id());
    }

    double maxDeltaTemp = 0;
    for (auto extruderID : extruders) {
        if (Model::extruderParamsMap.find(extruderID) != Model::extruderParamsMap.end())
            if (Model::extruderParamsMap.at(extruderID).bedTemp != 0){
                maxDeltaTemp = std::max(maxDeltaTemp, (double)Model::extruderParamsMap.at(extruderID).bedTemp);
                break;
            }
    }
    return maxDeltaTemp;
#else
    return 0.f;
#endif
}

void ModelInstance::get_arrange_polygon(void* ap) const
{
//    static const double SIMPLIFY_TOLERANCE_MM = 0.1;
    
    Vec3d rotation = get_rotation();
    rotation.z()   = 0.;
    Transform3d trafo_instance =
        Geometry::assemble_transform(get_offset().z() * Vec3d::UnitZ(), rotation, get_scaling_factor(), get_mirror());

    Polygon p = get_object()->convex_hull_2d(trafo_instance);

//    if (!p.points.empty()) {
//        Polygons pp{p};
//        pp = p.simplify(scaled<double>(SIMPLIFY_TOLERANCE_MM));
//        if (!pp.empty()) p = pp.front();
//    }
   
    arrangement::ArrangePolygon& ret = *(arrangement::ArrangePolygon*)ap;
    ret.poly.contour = std::move(p);
    ret.translation  = Vec2crd{scaled(get_offset(X)), scaled(get_offset(Y))};
    ret.rotation     = get_rotation(Z);

    //BBS: add materials related information
    ModelVolume *volume = NULL;
    for (size_t i = 0; i < object->volumes.size(); ++ i) {
        if (object->volumes[i]->is_model_part())
        {
            volume = object->volumes[i];
            break;
        }
    }
    if (!volume)
    {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "invalid object, should not happen";
        return;
    }
    ret.extrude_ids = volume->get_extruders();
    if (ret.extrude_ids.empty()) //the default extruder
        ret.extrude_ids.push_back(1);

    // get user specified brim width per object
    // Note: if global brim_type=btNoBrim or brAutoBrim, user can't set individual brim_width
    if (object->config.has("brim_width"))
        ret.user_brim_width = object->config.opt_float("brim_width");
    else {
        // BBS: get DeltaT, adhcoeff before calculating brim width
        double adhcoeff = getadhesionCoeff(object->volumes);
        double DeltaT = getTemperatureFromExtruder(object->volumes);
        // get auto brim width (Note even if the global brim_type=btOuterBrim, we can still go into this branch)
        ret.auto_brim_width = get_auto_brim_width(DeltaT, adhcoeff);
    }
}

indexed_triangle_set FacetsAnnotation::get_facets(const ModelVolume& mv, EnforcerBlockerType type) const
{
    TriangleSelector selector(mv.mesh());
    // Reset of TriangleSelector is done inside TriangleSelector's constructor, so we don't need it to perform it again in deserialize().
    selector.deserialize(m_data, false);
    return selector.get_facets(type);
}

// BBS
void FacetsAnnotation::get_facets(const ModelVolume& mv, std::vector<indexed_triangle_set>& facets_per_type) const
{
    TriangleSelector selector(mv.mesh());
    selector.deserialize(m_data, false);
    selector.get_facets(facets_per_type);
}

void FacetsAnnotation::set_enforcer_block_type_limit(const ModelVolume& mv, EnforcerBlockerType max_type)
{
    TriangleSelector selector(mv.mesh());
    selector.deserialize(m_data, false, max_type);
    this->set(selector);
}

indexed_triangle_set FacetsAnnotation::get_facets_strict(const ModelVolume& mv, EnforcerBlockerType type) const
{
    TriangleSelector selector(mv.mesh());
    // Reset of TriangleSelector is done inside TriangleSelector's constructor, so we don't need it to perform it again in deserialize().
    selector.deserialize(m_data, false);
    return selector.get_facets_strict(type);
}

bool FacetsAnnotation::has_facets(const ModelVolume& mv, EnforcerBlockerType type) const
{
    return TriangleSelector::has_facets(m_data, type);
}

bool FacetsAnnotation::set(const TriangleSelector& selector)
{
    std::pair<std::vector<std::pair<int, int>>, std::vector<bool>> sel_map = selector.serialize();
    if (sel_map != m_data) {
        m_data = std::move(sel_map);
        this->touch();
        return true;
    }
    return false;
}

void FacetsAnnotation::reset()
{
    m_data.first.clear();
    m_data.second.clear();
    this->touch();
}

// Following function takes data from a triangle and encodes it as string
// of hexadecimal numbers (one digit per triangle). Used for 3MF export,
// changing it may break backwards compatibility !!!!!
std::string FacetsAnnotation::get_triangle_as_string(int triangle_idx) const
{
    std::string out;

    auto triangle_it = std::lower_bound(m_data.first.begin(), m_data.first.end(), triangle_idx, [](const std::pair<int, int> &l, const int r) { return l.first < r; });
    if (triangle_it != m_data.first.end() && triangle_it->first == triangle_idx) {
        int offset = triangle_it->second;
        int end    = ++ triangle_it == m_data.first.end() ? int(m_data.second.size()) : triangle_it->second;
        while (offset < end) {
            int next_code = 0;
            for (int i=3; i>=0; --i) {
                next_code = next_code << 1;
                next_code |= int(m_data.second[offset + i]);
            }
            offset += 4;

            assert(next_code >=0 && next_code <= 15);
            char digit = next_code < 10 ? next_code + '0' : (next_code-10)+'A';
            out.insert(out.begin(), digit);
        }
    }
    return out;
}

// Recover triangle splitting & state from string of hexadecimal values previously
// generated by get_triangle_as_string. Used to load from 3MF.
void FacetsAnnotation::set_triangle_from_string(int triangle_id, const std::string& str)
{
    assert(! str.empty());
    assert(m_data.first.empty() || m_data.first.back().first < triangle_id);
    m_data.first.emplace_back(triangle_id, int(m_data.second.size()));

    for (auto it = str.crbegin(); it != str.crend(); ++it) {
        const char ch = *it;
        int dec = 0;
        if (ch >= '0' && ch<='9')
            dec = int(ch - '0');
        else if (ch >='A' && ch <= 'F')
            dec = 10 + int(ch - 'A');
        else
            assert(false);

        // Convert to binary and append into code.
        for (int i=0; i<4; ++i)
            m_data.second.insert(m_data.second.end(), bool(dec & (1 << i)));
    }
}

// Test whether the two models contain the same number of ModelObjects with the same set of IDs
// ordered in the same order. In that case it is not necessary to kill the background processing.
bool model_object_list_equal(const Model &model_old, const Model &model_new)
{
    if (model_old.objects.size() != model_new.objects.size())
        return false;
    for (size_t i = 0; i < model_old.objects.size(); ++ i)
        if (model_old.objects[i]->id() != model_new.objects[i]->id())
            return false;
    return true;
}

// Test whether the new model is just an extension of the old model (new objects were added
// to the end of the original list. In that case it is not necessary to kill the background processing.
bool model_object_list_extended(const Model &model_old, const Model &model_new)
{
    if (model_old.objects.size() >= model_new.objects.size())
        return false;
    for (size_t i = 0; i < model_old.objects.size(); ++ i)
        if (model_old.objects[i]->id() != model_new.objects[i]->id())
            return false;
    return true;
}

template<typename TypeFilterFn>
bool model_volume_list_changed(const ModelObject &model_object_old, const ModelObject &model_object_new, TypeFilterFn type_filter)
{
    size_t i_old, i_new;
    for (i_old = 0, i_new = 0; i_old < model_object_old.volumes.size() && i_new < model_object_new.volumes.size();) {
        const ModelVolume &mv_old = *model_object_old.volumes[i_old];
        const ModelVolume &mv_new = *model_object_new.volumes[i_new];
        if (! type_filter(mv_old.type())) {
            ++ i_old;
            continue;
        }
        if (! type_filter(mv_new.type())) {
            ++ i_new;
            continue;
        }
        if (mv_old.type() != mv_new.type() || mv_old.id() != mv_new.id())
            return true;
        //FIXME test for the content of the mesh!
        if (! mv_old.get_matrix().isApprox(mv_new.get_matrix()))
            return true;
        ++ i_old;
        ++ i_new;
    }
    for (; i_old < model_object_old.volumes.size(); ++ i_old) {
        const ModelVolume &mv_old = *model_object_old.volumes[i_old];
        if (type_filter(mv_old.type()))
            // ModelVolume was deleted.
            return true;
    }
    for (; i_new < model_object_new.volumes.size(); ++ i_new) {
        const ModelVolume &mv_new = *model_object_new.volumes[i_new];
        if (type_filter(mv_new.type()))
            // ModelVolume was added.
            return true;
    }
    return false;
}

bool model_volume_list_changed(const ModelObject &model_object_old, const ModelObject &model_object_new, const ModelVolumeType type)
{
    return model_volume_list_changed(model_object_old, model_object_new, [type](const ModelVolumeType t) { return t == type; });
}

bool model_volume_list_changed(const ModelObject &model_object_old, const ModelObject &model_object_new, const std::initializer_list<ModelVolumeType> &types)
{
    return model_volume_list_changed(model_object_old, model_object_new, [&types](const ModelVolumeType t) {
        return std::find(types.begin(), types.end(), t) != types.end();
    });
}

template< typename TypeFilterFn, typename CompareFn>
bool model_property_changed(const ModelObject &model_object_old, const ModelObject &model_object_new, TypeFilterFn type_filter, CompareFn compare)
{
    assert(! model_volume_list_changed(model_object_old, model_object_new, type_filter));
    size_t i_old, i_new;
    for (i_old = 0, i_new = 0; i_old < model_object_old.volumes.size() && i_new < model_object_new.volumes.size();) {
        const ModelVolume &mv_old = *model_object_old.volumes[i_old];
        const ModelVolume &mv_new = *model_object_new.volumes[i_new];
        if (! type_filter(mv_old.type())) {
            ++ i_old;
            continue;
        }
        if (! type_filter(mv_new.type())) {
            ++ i_new;
            continue;
        }
        assert(mv_old.type() == mv_new.type() && mv_old.id() == mv_new.id());
        if (! compare(mv_old, mv_new))
            return true;
        ++ i_old;
        ++ i_new;
    }
    return false;
}

bool model_custom_supports_data_changed(const ModelObject& mo, const ModelObject& mo_new)
{
    return model_property_changed(mo, mo_new, 
        [](const ModelVolumeType t) { return t == ModelVolumeType::MODEL_PART; }, 
        [](const ModelVolume &mv_old, const ModelVolume &mv_new){ return mv_old.supported_facets.timestamp_matches(mv_new.supported_facets); });
}

bool model_custom_seam_data_changed(const ModelObject& mo, const ModelObject& mo_new)
{
    return model_property_changed(mo, mo_new, 
        [](const ModelVolumeType t) { return t == ModelVolumeType::MODEL_PART; }, 
        [](const ModelVolume &mv_old, const ModelVolume &mv_new){ return mv_old.seam_facets.timestamp_matches(mv_new.seam_facets); });
}

bool model_mmu_segmentation_data_changed(const ModelObject& mo, const ModelObject& mo_new)
{
    return model_property_changed(mo, mo_new, 
        [](const ModelVolumeType t) { return t == ModelVolumeType::MODEL_PART; }, 
        [](const ModelVolume &mv_old, const ModelVolume &mv_new){ return mv_old.mmu_segmentation_facets.timestamp_matches(mv_new.mmu_segmentation_facets); });
}

bool model_has_multi_part_objects(const Model &model)
{
    for (const ModelObject *model_object : model.objects)
    	if (model_object->volumes.size() != 1 || ! model_object->volumes.front()->is_model_part())
    		return true;
    return false;
}

bool model_has_advanced_features(const Model &model)
{
	auto config_is_advanced = [](const ModelConfig &config) {
        return ! (config.empty() || (config.size() == 1 && config.cbegin()->first == "extruder"));
	};
    for (const ModelObject *model_object : model.objects) {
        // Is there more than one instance or advanced config data?
        if (model_object->instances.size() > 1 || config_is_advanced(model_object->config))
        	return true;
        // Is there any modifier or advanced config data?
        for (const ModelVolume* model_volume : model_object->volumes)
            if (! model_volume->is_model_part() || config_is_advanced(model_volume->config))
            	return true;
    }
    return false;
}

#ifndef NDEBUG
// Verify whether the IDs of Model / ModelObject / ModelVolume / ModelInstance / ModelMaterial are valid and unique.
void check_model_ids_validity(const Model &model)
{
    std::set<ObjectID> ids;
    auto check = [&ids](ObjectID id) { 
        assert(id.valid());
        assert(ids.find(id) == ids.end());
        ids.insert(id);
    };
    for (const ModelObject *model_object : model.objects) {
        check(model_object->id());
        check(model_object->config.id());
        for (const ModelVolume *model_volume : model_object->volumes) {
            check(model_volume->id());
	        check(model_volume->config.id());
        }
        for (const ModelInstance *model_instance : model_object->instances)
            check(model_instance->id());
    }
    for (const auto mm : model.materials) {
        check(mm.second->id());
        check(mm.second->config.id());
    }
}

void check_model_ids_equal(const Model &model1, const Model &model2)
{
    // Verify whether the IDs of model1 and model match.
    assert(model1.objects.size() == model2.objects.size());
    for (size_t idx_model = 0; idx_model < model2.objects.size(); ++ idx_model) {
        const ModelObject &model_object1 = *model1.objects[idx_model];
        const ModelObject &model_object2 = *  model2.objects[idx_model];
        assert(model_object1.id() == model_object2.id());
        assert(model_object1.config.id() == model_object2.config.id());
        assert(model_object1.volumes.size() == model_object2.volumes.size());
        assert(model_object1.instances.size() == model_object2.instances.size());
        for (size_t i = 0; i < model_object1.volumes.size(); ++ i) {
            assert(model_object1.volumes[i]->id() == model_object2.volumes[i]->id());
        	assert(model_object1.volumes[i]->config.id() == model_object2.volumes[i]->config.id());
        }
        for (size_t i = 0; i < model_object1.instances.size(); ++ i)
            assert(model_object1.instances[i]->id() == model_object2.instances[i]->id());
    }
    assert(model1.materials.size() == model2.materials.size());
    {
        auto it1 = model1.materials.begin();
        auto it2 = model2.materials.begin();
        for (; it1 != model1.materials.end(); ++ it1, ++ it2) {
            assert(it1->first == it2->first); // compare keys
            assert(it1->second->id() == it2->second->id());
        	assert(it1->second->config.id() == it2->second->config.id());
        }
    }
}

#endif /* NDEBUG */

}

#if 0
CEREAL_REGISTER_TYPE(Slic3r::ModelObject)
CEREAL_REGISTER_TYPE(Slic3r::ModelVolume)
CEREAL_REGISTER_TYPE(Slic3r::ModelInstance)
CEREAL_REGISTER_TYPE(Slic3r::Model)

CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ObjectBase, Slic3r::ModelObject)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ObjectBase, Slic3r::ModelVolume)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ObjectBase, Slic3r::ModelInstance)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::ObjectBase, Slic3r::Model)
#endif
