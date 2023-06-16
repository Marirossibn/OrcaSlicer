// Configuration store of Slic3r.
//
// The configuration store is either static or dynamic.
// DynamicPrintConfig is used mainly at the user interface. while the StaticPrintConfig is used
// during the slicing and the g-code generation.
//
// The classes derived from StaticPrintConfig form a following hierarchy.
//
// FullPrintConfig
//    PrintObjectConfig
//    PrintRegionConfig
//    PrintConfig
//        GCodeConfig
//

#ifndef slic3r_PrintConfig_hpp_
#define slic3r_PrintConfig_hpp_

#include "libslic3r.h"
#include "Config.hpp"
#include "Polygon.hpp"
#include <boost/preprocessor/facilities/empty.hpp>
#include <boost/preprocessor/punctuation/comma_if.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/seq/for_each_i.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/preprocessor/tuple/elem.hpp>
#include <boost/preprocessor/tuple/to_seq.hpp>

// #define HAS_PRESSURE_EQUALIZER

namespace Slic3r {

enum GCodeFlavor : unsigned char {
    gcfMarlinLegacy, gcfKlipper, gcfRepRapSprinter, gcfRepRapFirmware, gcfRepetier, gcfTeacup, gcfMakerWare, gcfMarlinFirmware, gcfSailfish, gcfMach3, gcfMachinekit,
    gcfSmoothie, gcfNoExtrusion
};

enum class FuzzySkinType {
    None,
    External,
    All,
    AllWalls,
};

enum PrintHostType {
    htPrusaLink, htOctoPrint, htDuet, htFlashAir, htAstroBox, htRepetier, htMKS
};

enum AuthorizationType {
    atKeyPassword, atUserPassword
};

enum InfillPattern : int {
    ipConcentric, ipRectilinear, ipGrid, ipLine, ipCubic, ipTriangles, ipStars, ipGyroid, ipHoneycomb, ipAdaptiveCubic, ipMonotonic, ipMonotonicLine, ipAlignedRectilinear, ip3DHoneycomb,
    ipHilbertCurve, ipArchimedeanChords, ipOctagramSpiral, ipSupportCubic, ipSupportBase, ipConcentricInternal,
    ipLightning,
ipCount,
};

enum class IroningType {
    NoIroning,
    TopSurfaces,
    TopmostOnly,
    AllSolid,
    Count,
};

//BBS
enum class WallInfillOrder {
    InnerOuterInfill,
    OuterInnerInfill,
    InfillInnerOuter,
    InfillOuterInner,
    InnerOuterInnerInfill,
    Count,
};
//BBS
enum class PrintSequence {
    ByLayer,
    ByObject,
    ByDefault,
    Count,
};

enum class SlicingMode
{
    // Regular, applying ClipperLib::pftNonZero rule when creating ExPolygons.
    Regular,
    // Compatible with 3DLabPrint models, applying ClipperLib::pftEvenOdd rule when creating ExPolygons.
    EvenOdd,
    // Orienting all contours CCW, thus closing all holes.
    CloseHoles,
};

enum SupportMaterialPattern {
    smpDefault,
    smpRectilinear, smpRectilinearGrid, smpHoneycomb,
    smpLightning,
    smpNone,
};

enum SupportMaterialStyle {
    smsDefault, smsGrid, smsSnug, smsTreeSlim, smsTreeStrong, smsTreeHybrid
};

enum SupportMaterialInterfacePattern {
    smipAuto, smipRectilinear, smipConcentric, smipRectilinearInterlaced, smipGrid
};

// BBS
enum SupportType {
    stNormalAuto, stTreeAuto, stNormal, stTree
};
inline bool is_tree(SupportType stype)
{
    return std::set<SupportType>{stTreeAuto, stTree}.count(stype) != 0;
};
inline bool is_tree_slim(SupportType type, SupportMaterialStyle style)
{
    return is_tree(type) && style==smsTreeSlim;
};
inline bool is_auto(SupportType stype)
{
    return std::set<SupportType>{stNormalAuto, stTreeAuto}.count(stype) != 0;
};

enum SeamPosition {
    spNearest, spAligned, spRear, spRandom
};

enum SLAMaterial {
    slamTough,
    slamFlex,
    slamCasting,
    slamDental,
    slamHeatResistant,
};

enum SLADisplayOrientation {
    sladoLandscape,
    sladoPortrait
};

enum SLAPillarConnectionMode {
    slapcmZigZag,
    slapcmCross,
    slapcmDynamic
};

enum BrimType {
    btAutoBrim,  // BBS
    btOuterOnly,
    btInnerOnly,
    btOuterAndInner,
    btNoBrim,
};

enum TimelapseType : int {
    tlTraditional = 0,
    tlSmooth
};

enum DraftShield {
    dsDisabled, dsLimited, dsEnabled
};

enum class PerimeterGeneratorType
{
    // Classic perimeter generator using Clipper offsets with constant extrusion width.
    Classic,
    // Perimeter generator with variable extrusion width based on the paper
    // "A framework for adaptive width control of dense contour-parallel toolpaths in fused deposition modeling" ported from Cura.
    Arachne
};

// BBS
enum OverhangFanThreshold {
    Overhang_threshold_none = 0,
    Overhang_threshold_1_4,
    Overhang_threshold_2_4,
    Overhang_threshold_3_4,
    Overhang_threshold_4_4,
    Overhang_threshold_bridge
};

// BBS
enum BedType {
    btDefault = 0,
    btPC,
    btEP,
    btPEI,
    btPTE,
    btCount
};

// BBS
enum NozzleType {
    ntUndefine = 0,
    ntHardenedSteel,
    ntStainlessSteel,
    ntBrass,
    ntCount
};

// BBS
enum ZHopType {
    zhtAuto = 0,
    zhtNormal,
    zhtSlope,
    zhtSpiral,
    zhtCount
};

static std::string bed_type_to_gcode_string(const BedType type)
{
    std::string type_str;

    switch (type) {
    case btPC:
        type_str = "cool_plate";
        break;
    case btEP:
        type_str = "eng_plate";
        break;
    case btPEI:
        type_str = "hot_plate";
        break;
    case btPTE:
        type_str = "textured_plate";
        break;
    default:
        type_str = "unknown";
        break;
    }

    return type_str;
}

static std::string get_bed_temp_key(const BedType type)
{
    if (type == btPC)
        return "cool_plate_temp";

    if (type == btEP)
        return "eng_plate_temp";

    if (type == btPEI)
        return "hot_plate_temp";

    if (type == btPTE)
        return "textured_plate_temp";

    return "";
}

static std::string get_bed_temp_1st_layer_key(const BedType type)
{
    if (type == btPC)
        return "cool_plate_temp_initial_layer";

    if (type == btEP)
        return "eng_plate_temp_initial_layer";

    if (type == btPEI)
        return "hot_plate_temp_initial_layer";

    if (type == btPTE)
        return "textured_plate_temp_initial_layer";

    return "";
}

#define CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(NAME) \
    template<> const t_config_enum_names& ConfigOptionEnum<NAME>::get_enum_names(); \
    template<> const t_config_enum_values& ConfigOptionEnum<NAME>::get_enum_values();

CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(PrinterTechnology)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(GCodeFlavor)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(FuzzySkinType)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(InfillPattern)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(IroningType)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(SlicingMode)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(SupportMaterialPattern)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(SupportMaterialStyle)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(SupportMaterialInterfacePattern)
// BBS
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(SupportType)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(SeamPosition)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(SLADisplayOrientation)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(SLAPillarConnectionMode)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(BrimType)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(TimelapseType)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(BedType)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(DraftShield)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(ForwardCompatibilitySubstitutionRule)

CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(PrintHostType)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(AuthorizationType)
CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS(PerimeterGeneratorType)

#undef CONFIG_OPTION_ENUM_DECLARE_STATIC_MAPS

// Defines each and every confiuration option of Slic3r, including the properties of the GUI dialogs.
// Does not store the actual values, but defines default values.
class PrintConfigDef : public ConfigDef
{
public:
    PrintConfigDef();

    static void handle_legacy(t_config_option_key &opt_key, std::string &value);

    // Array options growing with the number of extruders
    const std::vector<std::string>& extruder_option_keys() const { return m_extruder_option_keys; }
    // Options defining the extruder retract properties. These keys are sorted lexicographically.
    // The extruder retract keys could be overidden by the same values defined at the Filament level
    // (then the key is further prefixed with the "filament_" prefix).
    const std::vector<std::string>& extruder_retract_keys() const { return m_extruder_retract_keys; }

    // BBS
    const std::vector<std::string>& filament_option_keys() const { return m_filament_option_keys; }
    const std::vector<std::string>& filament_retract_keys() const { return m_filament_retract_keys; }

private:
    void init_common_params();
    void init_fff_params();
    void init_extruder_option_keys();
    void init_sla_params();

    std::vector<std::string>    m_extruder_option_keys;
    std::vector<std::string>    m_extruder_retract_keys;

    // BBS
    void init_filament_option_keys();

    std::vector<std::string>    m_filament_option_keys;
    std::vector<std::string>    m_filament_retract_keys;
};

// The one and only global definition of SLic3r configuration options.
// This definition is constant.
extern const PrintConfigDef print_config_def;

class StaticPrintConfig;

// Minimum object distance for arrangement, based on printer technology.
double min_object_distance(const ConfigBase &cfg);

// Slic3r dynamic configuration, used to override the configuration
// per object, per modification volume or per printing material.
// The dynamic configuration is also used to store user modifications of the print global parameters,
// so the modified configuration values may be diffed against the active configuration
// to invalidate the proper slicing resp. g-code generation processing steps.
// This object is mapped to Perl as Slic3r::Config.
class DynamicPrintConfig : public DynamicConfig
{
public:
    DynamicPrintConfig() {}
    DynamicPrintConfig(const DynamicPrintConfig &rhs) : DynamicConfig(rhs) {}
    DynamicPrintConfig(DynamicPrintConfig &&rhs) noexcept : DynamicConfig(std::move(rhs)) {}
    explicit DynamicPrintConfig(const StaticPrintConfig &rhs);
    explicit DynamicPrintConfig(const ConfigBase &rhs) : DynamicConfig(rhs) {}

    DynamicPrintConfig& operator=(const DynamicPrintConfig &rhs) { DynamicConfig::operator=(rhs); return *this; }
    DynamicPrintConfig& operator=(DynamicPrintConfig &&rhs) noexcept { DynamicConfig::operator=(std::move(rhs)); return *this; }

    static DynamicPrintConfig  full_print_config();
    static DynamicPrintConfig* new_from_defaults_keys(const std::vector<std::string> &keys);

    // Overrides ConfigBase::def(). Static configuration definition. Any value stored into this ConfigBase shall have its definition here.
    const ConfigDef*    def() const override { return &print_config_def; }

    void                normalize_fdm(int used_filaments = 0);
    void                normalize_fdm_1();
    //return the changed param set
    t_config_option_keys normalize_fdm_2(int num_objects, int used_filaments = 0);

    void                set_num_extruders(unsigned int num_extruders);

    // BBS
    void                set_num_filaments(unsigned int num_filaments);

    //BBS
    // Validate the PrintConfig. Returns an empty string on success, otherwise an error message is returned.
    std::map<std::string, std::string>         validate(bool under_cli = false);

    // Verify whether the opt_key has not been obsoleted or renamed.
    // Both opt_key and value may be modified by handle_legacy().
    // If the opt_key is no more valid in this version of Slic3r, opt_key is cleared by handle_legacy().
    // handle_legacy() is called internally by set_deserialize().
    void                handle_legacy(t_config_option_key &opt_key, std::string &value) const override
        { PrintConfigDef::handle_legacy(opt_key, value); }

    //BBS special case Support G/ Support W
    std::string get_filament_type(std::string &displayed_filament_type, int id = 0);

    bool is_custom_defined();
};

void handle_legacy_sla(DynamicPrintConfig &config);

class StaticPrintConfig : public StaticConfig
{
public:
    StaticPrintConfig() {}

    // Overrides ConfigBase::def(). Static configuration definition. Any value stored into this ConfigBase shall have its definition here.
    const ConfigDef*    def() const override { return &print_config_def; }
    // Reference to the cached list of keys.
    virtual const t_config_option_keys& keys_ref() const = 0;

protected:
    // Verify whether the opt_key has not been obsoleted or renamed.
    // Both opt_key and value may be modified by handle_legacy().
    // If the opt_key is no more valid in this version of Slic3r, opt_key is cleared by handle_legacy().
    // handle_legacy() is called internally by set_deserialize().
    void                handle_legacy(t_config_option_key &opt_key, std::string &value) const override
        { PrintConfigDef::handle_legacy(opt_key, value); }

    // Internal class for keeping a dynamic map to static options.
    class StaticCacheBase
    {
    public:
        // To be called during the StaticCache setup.
        // Add one ConfigOption into m_map_name_to_offset.
        template<typename T>
        void                opt_add(const std::string &name, const char *base_ptr, const T &opt)
        {
            assert(m_map_name_to_offset.find(name) == m_map_name_to_offset.end());
            m_map_name_to_offset[name] = (const char*)&opt - base_ptr;
        }

    protected:
        std::map<std::string, ptrdiff_t>    m_map_name_to_offset;
    };

    // Parametrized by the type of the topmost class owning the options.
    template<typename T>
    class StaticCache : public StaticCacheBase
    {
    public:
        // Calling the constructor of m_defaults with 0 forces m_defaults to not run the initialization.
        StaticCache() : m_defaults(nullptr) {}
        ~StaticCache() { delete m_defaults; m_defaults = nullptr; }

        bool                initialized() const { return ! m_keys.empty(); }

        ConfigOption*       optptr(const std::string &name, T *owner) const
        {
            const auto it = m_map_name_to_offset.find(name);
            return (it == m_map_name_to_offset.end()) ? nullptr : reinterpret_cast<ConfigOption*>((char*)owner + it->second);
        }

        const ConfigOption* optptr(const std::string &name, const T *owner) const
        {
            const auto it = m_map_name_to_offset.find(name);
            return (it == m_map_name_to_offset.end()) ? nullptr : reinterpret_cast<const ConfigOption*>((const char*)owner + it->second);
        }

        const std::vector<std::string>& keys()      const { return m_keys; }
        const T&                        defaults()  const { return *m_defaults; }

        // To be called during the StaticCache setup.
        // Collect option keys from m_map_name_to_offset,
        // assign default values to m_defaults.
        void                finalize(T *defaults, const ConfigDef *defs)
        {
            assert(defs != nullptr);
            m_defaults = defaults;
            m_keys.clear();
            m_keys.reserve(m_map_name_to_offset.size());
            for (const auto &kvp : defs->options) {
                // Find the option given the option name kvp.first by an offset from (char*)m_defaults.
                ConfigOption *opt = this->optptr(kvp.first, m_defaults);
                if (opt == nullptr)
                    // This option is not defined by the ConfigBase of type T.
                    continue;
                m_keys.emplace_back(kvp.first);
                const ConfigOptionDef *def = defs->get(kvp.first);
                assert(def != nullptr);
                if (def->default_value)
                    opt->set(def->default_value.get());
            }
        }

    private:
        T                                  *m_defaults;
        std::vector<std::string>            m_keys;
    };
};

#define STATIC_PRINT_CONFIG_CACHE_BASE(CLASS_NAME) \
public: \
    /* Overrides ConfigBase::optptr(). Find ando/or create a ConfigOption instance for a given name. */ \
    const ConfigOption*      optptr(const t_config_option_key &opt_key) const override \
        { return s_cache_##CLASS_NAME.optptr(opt_key, this); } \
    /* Overrides ConfigBase::optptr(). Find ando/or create a ConfigOption instance for a given name. */ \
    ConfigOption*            optptr(const t_config_option_key &opt_key, bool create = false) override \
        { return s_cache_##CLASS_NAME.optptr(opt_key, this); } \
    /* Overrides ConfigBase::keys(). Collect names of all configuration values maintained by this configuration store. */ \
    t_config_option_keys     keys() const override { return s_cache_##CLASS_NAME.keys(); } \
    const t_config_option_keys& keys_ref() const override { return s_cache_##CLASS_NAME.keys(); } \
    static const CLASS_NAME& defaults() { assert(s_cache_##CLASS_NAME.initialized()); return s_cache_##CLASS_NAME.defaults(); } \
private: \
    friend int print_config_static_initializer(); \
    static void initialize_cache() \
    { \
        assert(! s_cache_##CLASS_NAME.initialized()); \
        if (! s_cache_##CLASS_NAME.initialized()) { \
            CLASS_NAME *inst = new CLASS_NAME(1); \
            inst->initialize(s_cache_##CLASS_NAME, (const char*)inst); \
            s_cache_##CLASS_NAME.finalize(inst, inst->def()); \
        } \
    } \
    /* Cache object holding a key/option map, a list of option keys and a copy of this static config initialized with the defaults. */ \
    static StaticPrintConfig::StaticCache<CLASS_NAME> s_cache_##CLASS_NAME;

#define STATIC_PRINT_CONFIG_CACHE(CLASS_NAME) \
    STATIC_PRINT_CONFIG_CACHE_BASE(CLASS_NAME) \
public: \
    /* Public default constructor will initialize the key/option cache and the default object copy if needed. */ \
    CLASS_NAME() { assert(s_cache_##CLASS_NAME.initialized()); *this = s_cache_##CLASS_NAME.defaults(); } \
protected: \
    /* Protected constructor to be called when compounded. */ \
    CLASS_NAME(int) {}

#define STATIC_PRINT_CONFIG_CACHE_DERIVED(CLASS_NAME) \
    STATIC_PRINT_CONFIG_CACHE_BASE(CLASS_NAME) \
public: \
    /* Overrides ConfigBase::def(). Static configuration definition. Any value stored into this ConfigBase shall have its definition here. */ \
    const ConfigDef*    def() const override { return &print_config_def; } \
    /* Handle legacy and obsoleted config keys */ \
    void                handle_legacy(t_config_option_key &opt_key, std::string &value) const override \
        { PrintConfigDef::handle_legacy(opt_key, value); }

#define PRINT_CONFIG_CLASS_ELEMENT_DEFINITION(r, data, elem) BOOST_PP_TUPLE_ELEM(0, elem) BOOST_PP_TUPLE_ELEM(1, elem);
#define PRINT_CONFIG_CLASS_ELEMENT_INITIALIZATION2(KEY) cache.opt_add(BOOST_PP_STRINGIZE(KEY), base_ptr, this->KEY);
#define PRINT_CONFIG_CLASS_ELEMENT_INITIALIZATION(r, data, elem) PRINT_CONFIG_CLASS_ELEMENT_INITIALIZATION2(BOOST_PP_TUPLE_ELEM(1, elem))
#define PRINT_CONFIG_CLASS_ELEMENT_HASH(r, data, elem) boost::hash_combine(seed, BOOST_PP_TUPLE_ELEM(1, elem).hash());
#define PRINT_CONFIG_CLASS_ELEMENT_EQUAL(r, data, elem) if (! (BOOST_PP_TUPLE_ELEM(1, elem) == rhs.BOOST_PP_TUPLE_ELEM(1, elem))) return false;
#define PRINT_CONFIG_CLASS_ELEMENT_LOWER(r, data, elem) \
        if (BOOST_PP_TUPLE_ELEM(1, elem) < rhs.BOOST_PP_TUPLE_ELEM(1, elem)) return true; \
        if (! (BOOST_PP_TUPLE_ELEM(1, elem) == rhs.BOOST_PP_TUPLE_ELEM(1, elem))) return false;

#define PRINT_CONFIG_CLASS_DEFINE(CLASS_NAME, PARAMETER_DEFINITION_SEQ) \
class CLASS_NAME : public StaticPrintConfig { \
    STATIC_PRINT_CONFIG_CACHE(CLASS_NAME) \
public: \
    BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_ELEMENT_DEFINITION, _, PARAMETER_DEFINITION_SEQ) \
    size_t hash() const throw() \
    { \
        size_t seed = 0; \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_ELEMENT_HASH, _, PARAMETER_DEFINITION_SEQ) \
        return seed; \
    } \
    bool operator==(const CLASS_NAME &rhs) const throw() \
    { \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_ELEMENT_EQUAL, _, PARAMETER_DEFINITION_SEQ) \
        return true; \
    } \
    bool operator!=(const CLASS_NAME &rhs) const throw() { return ! (*this == rhs); } \
    bool operator<(const CLASS_NAME &rhs) const throw() \
    { \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_ELEMENT_LOWER, _, PARAMETER_DEFINITION_SEQ) \
        return false; \
    } \
protected: \
    void initialize(StaticCacheBase &cache, const char *base_ptr) \
    { \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_ELEMENT_INITIALIZATION, _, PARAMETER_DEFINITION_SEQ) \
    } \
};

#define PRINT_CONFIG_CLASS_DERIVED_CLASS_LIST_ITEM(r, data, i, elem) BOOST_PP_COMMA_IF(i) public elem
#define PRINT_CONFIG_CLASS_DERIVED_CLASS_LIST(CLASSES_PARENTS_TUPLE) BOOST_PP_SEQ_FOR_EACH_I(PRINT_CONFIG_CLASS_DERIVED_CLASS_LIST_ITEM, _, BOOST_PP_TUPLE_TO_SEQ(CLASSES_PARENTS_TUPLE))
#define PRINT_CONFIG_CLASS_DERIVED_INITIALIZER_ITEM(r, VALUE, i, elem) BOOST_PP_COMMA_IF(i) elem(VALUE)
#define PRINT_CONFIG_CLASS_DERIVED_INITIALIZER(CLASSES_PARENTS_TUPLE, VALUE) BOOST_PP_SEQ_FOR_EACH_I(PRINT_CONFIG_CLASS_DERIVED_INITIALIZER_ITEM, VALUE, BOOST_PP_TUPLE_TO_SEQ(CLASSES_PARENTS_TUPLE))
#define PRINT_CONFIG_CLASS_DERIVED_INITCACHE_ITEM(r, data, elem) this->elem::initialize(cache, base_ptr);
#define PRINT_CONFIG_CLASS_DERIVED_INITCACHE(CLASSES_PARENTS_TUPLE) BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_DERIVED_INITCACHE_ITEM, _, BOOST_PP_TUPLE_TO_SEQ(CLASSES_PARENTS_TUPLE))
#define PRINT_CONFIG_CLASS_DERIVED_HASH(r, data, elem) boost::hash_combine(seed, static_cast<const elem*>(this)->hash());
#define PRINT_CONFIG_CLASS_DERIVED_EQUAL(r, data, elem) \
    if (! (*static_cast<const elem*>(this) == static_cast<const elem&>(rhs))) return false;

// Generic version, with or without new parameters. Don't use this directly.
#define PRINT_CONFIG_CLASS_DERIVED_DEFINE1(CLASS_NAME, CLASSES_PARENTS_TUPLE, PARAMETER_DEFINITION, PARAMETER_REGISTRATION, PARAMETER_HASHES, PARAMETER_EQUALS) \
class CLASS_NAME : PRINT_CONFIG_CLASS_DERIVED_CLASS_LIST(CLASSES_PARENTS_TUPLE) { \
    STATIC_PRINT_CONFIG_CACHE_DERIVED(CLASS_NAME) \
    CLASS_NAME() : PRINT_CONFIG_CLASS_DERIVED_INITIALIZER(CLASSES_PARENTS_TUPLE, 0) { assert(s_cache_##CLASS_NAME.initialized()); *this = s_cache_##CLASS_NAME.defaults(); } \
public: \
    PARAMETER_DEFINITION \
    size_t hash() const throw() \
    { \
        size_t seed = 0; \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_DERIVED_HASH, _, BOOST_PP_TUPLE_TO_SEQ(CLASSES_PARENTS_TUPLE)) \
        PARAMETER_HASHES \
        return seed; \
    } \
    bool operator==(const CLASS_NAME &rhs) const throw() \
    { \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_DERIVED_EQUAL, _, BOOST_PP_TUPLE_TO_SEQ(CLASSES_PARENTS_TUPLE)) \
        PARAMETER_EQUALS \
        return true; \
    } \
    bool operator!=(const CLASS_NAME &rhs) const throw() { return ! (*this == rhs); } \
protected: \
    CLASS_NAME(int) : PRINT_CONFIG_CLASS_DERIVED_INITIALIZER(CLASSES_PARENTS_TUPLE, 1) {} \
    void initialize(StaticCacheBase &cache, const char* base_ptr) { \
        PRINT_CONFIG_CLASS_DERIVED_INITCACHE(CLASSES_PARENTS_TUPLE) \
        PARAMETER_REGISTRATION \
    } \
};
// Variant without adding new parameters.
#define PRINT_CONFIG_CLASS_DERIVED_DEFINE0(CLASS_NAME, CLASSES_PARENTS_TUPLE) \
    PRINT_CONFIG_CLASS_DERIVED_DEFINE1(CLASS_NAME, CLASSES_PARENTS_TUPLE, BOOST_PP_EMPTY(), BOOST_PP_EMPTY(), BOOST_PP_EMPTY(), BOOST_PP_EMPTY())
// Variant with adding new parameters.
#define PRINT_CONFIG_CLASS_DERIVED_DEFINE(CLASS_NAME, CLASSES_PARENTS_TUPLE, PARAMETER_DEFINITION_SEQ) \
    PRINT_CONFIG_CLASS_DERIVED_DEFINE1(CLASS_NAME, CLASSES_PARENTS_TUPLE, \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_ELEMENT_DEFINITION, _, PARAMETER_DEFINITION_SEQ), \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_ELEMENT_INITIALIZATION, _, PARAMETER_DEFINITION_SEQ), \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_ELEMENT_HASH, _, PARAMETER_DEFINITION_SEQ), \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CLASS_ELEMENT_EQUAL, _, PARAMETER_DEFINITION_SEQ))

// This object is mapped to Perl as Slic3r::Config::PrintObject.
PRINT_CONFIG_CLASS_DEFINE(
    PrintObjectConfig,

    ((ConfigOptionFloat,               brim_object_gap))
    ((ConfigOptionEnum<BrimType>,      brim_type))
    ((ConfigOptionFloat,               brim_width))
    ((ConfigOptionBool,                bridge_no_support))
    ((ConfigOptionFloat,               elefant_foot_compensation))
    ((ConfigOptionFloat,               max_bridge_length))
    ((ConfigOptionFloat,               line_width))
    // Force the generation of solid shells between adjacent materials/volumes.
    ((ConfigOptionBool,                interface_shells))
    ((ConfigOptionFloat,               layer_height))
    ((ConfigOptionFloat,               raft_contact_distance))
    ((ConfigOptionFloat,               raft_expansion))
    ((ConfigOptionPercent,             raft_first_layer_density))
    ((ConfigOptionFloat,               raft_first_layer_expansion))
    ((ConfigOptionInt,                 raft_layers))
    ((ConfigOptionEnum<SeamPosition>,  seam_position))
    ((ConfigOptionFloat,               slice_closing_radius))
    ((ConfigOptionEnum<SlicingMode>,   slicing_mode))
    ((ConfigOptionBool,                enable_support))
    // Automatic supports (generated based on support_threshold_angle).
    ((ConfigOptionEnum<SupportType>,   support_type))
    // Direction of the support pattern (in XY plane).`
    ((ConfigOptionFloat,               support_angle))
    ((ConfigOptionBool,                support_on_build_plate_only))
    ((ConfigOptionBool,                support_critical_regions_only))
    ((ConfigOptionBool,                support_remove_small_overhang))
    ((ConfigOptionFloat,               support_top_z_distance))
    ((ConfigOptionFloat,               support_bottom_z_distance))
    ((ConfigOptionInt,                 enforce_support_layers))
    ((ConfigOptionInt,                 support_filament))
    ((ConfigOptionFloat,               support_line_width))
    ((ConfigOptionBool,                support_interface_loop_pattern))
    ((ConfigOptionInt,                 support_interface_filament))
    ((ConfigOptionInt,                 support_interface_top_layers))
    ((ConfigOptionInt,                 support_interface_bottom_layers))
    // Spacing between interface lines (the hatching distance). Set zero to get a solid interface.
    ((ConfigOptionFloat,               support_interface_spacing))
    ((ConfigOptionFloat,               support_interface_speed))
    ((ConfigOptionEnum<SupportMaterialPattern>, support_base_pattern))
    ((ConfigOptionEnum<SupportMaterialInterfacePattern>, support_interface_pattern))
    // Spacing between support material lines (the hatching distance).
    ((ConfigOptionFloat,               support_base_pattern_spacing))
    ((ConfigOptionFloat,               support_expansion))
    ((ConfigOptionFloat,               support_speed))
    ((ConfigOptionEnum<SupportMaterialStyle>, support_style))
    // BBS
    //((ConfigOptionBool,                independent_support_layer_height))
    ((ConfigOptionBool,                thick_bridges))
    // Overhang angle threshold.
    ((ConfigOptionInt,                 support_threshold_angle))
    ((ConfigOptionFloat,               support_object_xy_distance))
    ((ConfigOptionFloat,               xy_hole_compensation))
    ((ConfigOptionFloat,               xy_contour_compensation))
    ((ConfigOptionBool,                flush_into_objects))
    // BBS
    ((ConfigOptionBool,                flush_into_infill))
    ((ConfigOptionBool,                flush_into_support))
    // BBS
    ((ConfigOptionFloat,              tree_support_branch_distance))
    ((ConfigOptionFloat,              tree_support_branch_diameter))
    ((ConfigOptionFloat,              tree_support_branch_angle))
    ((ConfigOptionInt,                tree_support_wall_count))
    ((ConfigOptionFloat,              tree_support_brim_width))
    ((ConfigOptionBool,               detect_narrow_internal_solid_infill))
    // ((ConfigOptionBool,               adaptive_layer_height))
    ((ConfigOptionFloat,              support_bottom_interface_spacing))
    ((ConfigOptionFloat,              internal_bridge_support_thickness))
    ((ConfigOptionEnum<PerimeterGeneratorType>, wall_generator))
    ((ConfigOptionPercent,            wall_transition_length))
    ((ConfigOptionPercent,            wall_transition_filter_deviation))
    ((ConfigOptionFloat,              wall_transition_angle))
    ((ConfigOptionInt,                wall_distribution_count))
    ((ConfigOptionPercent,            min_feature_size))
    ((ConfigOptionPercent,            min_bead_width))
    ((ConfigOptionBool,               only_one_wall_top))
    ((ConfigOptionBool,               only_one_wall_first_layer))
    // SoftFever
    ((ConfigOptionFloat,              seam_gap))
    ((ConfigOptionPercent,            wipe_speed))
)

// This object is mapped to Perl as Slic3r::Config::PrintRegion.
PRINT_CONFIG_CLASS_DEFINE(
    PrintRegionConfig,

    ((ConfigOptionInt, bottom_shell_layers))
    ((ConfigOptionFloat, bottom_shell_thickness))
    ((ConfigOptionFloat, bridge_angle))
    ((ConfigOptionFloat, bridge_flow))
    ((ConfigOptionFloat, bridge_speed))
    ((ConfigOptionBool, ensure_vertical_shell_thickness))
    ((ConfigOptionEnum<InfillPattern>, top_surface_pattern))
    ((ConfigOptionEnum<InfillPattern>, bottom_surface_pattern))
    ((ConfigOptionFloat, outer_wall_line_width))
    ((ConfigOptionFloat, outer_wall_speed))
    ((ConfigOptionFloat, infill_direction))
    ((ConfigOptionPercent, sparse_infill_density))
    ((ConfigOptionEnum<InfillPattern>, sparse_infill_pattern))
    ((ConfigOptionEnum<FuzzySkinType>, fuzzy_skin))
    ((ConfigOptionFloat, fuzzy_skin_thickness))
    ((ConfigOptionFloat, fuzzy_skin_point_distance))
    ((ConfigOptionFloat, gap_infill_speed))
    ((ConfigOptionInt, sparse_infill_filament))
    ((ConfigOptionFloat, sparse_infill_line_width))
    ((ConfigOptionPercent, infill_wall_overlap))
    ((ConfigOptionFloat, sparse_infill_speed))
    //BBS
    ((ConfigOptionBool, infill_combination))
    // Ironing options
    ((ConfigOptionEnum<IroningType>, ironing_type))
    ((ConfigOptionEnum<InfillPattern>, ironing_pattern))
    ((ConfigOptionPercent, ironing_flow))
    ((ConfigOptionFloat, ironing_spacing))
    ((ConfigOptionFloat, ironing_speed))
    // Detect bridging perimeters
    ((ConfigOptionBool, detect_overhang_wall))
    ((ConfigOptionInt, wall_filament))
    ((ConfigOptionFloat, inner_wall_line_width))
    ((ConfigOptionFloat, inner_wall_speed))
    // Total number of perimeters.
    ((ConfigOptionInt, wall_loops))
    ((ConfigOptionFloat, minimum_sparse_infill_area))
    ((ConfigOptionInt, solid_infill_filament))
    ((ConfigOptionFloat, internal_solid_infill_line_width))
    ((ConfigOptionFloat, internal_solid_infill_speed))
    // Detect thin walls.
    ((ConfigOptionBool, detect_thin_wall))
    ((ConfigOptionFloat, top_surface_line_width))
    ((ConfigOptionInt, top_shell_layers))
    ((ConfigOptionFloat, top_shell_thickness))
    ((ConfigOptionFloat, top_surface_speed))
    //BBS
    ((ConfigOptionBool, enable_overhang_speed))
    ((ConfigOptionFloat, overhang_1_4_speed))
    ((ConfigOptionFloat, overhang_2_4_speed))
    ((ConfigOptionFloat, overhang_3_4_speed))
    ((ConfigOptionFloat, overhang_4_4_speed))
    ((ConfigOptionFloatOrPercent, sparse_infill_anchor))
    ((ConfigOptionFloatOrPercent, sparse_infill_anchor_max))
    //SoftFever
    ((ConfigOptionFloat, top_solid_infill_flow_ratio))
    ((ConfigOptionFloat, bottom_solid_infill_flow_ratio))
    ((ConfigOptionFloat, filter_out_gap_fill))
    //calib
    ((ConfigOptionFloat, print_flow_ratio)))

PRINT_CONFIG_CLASS_DEFINE(
    MachineEnvelopeConfig,

    // M201 X... Y... Z... E... [mm/sec^2]
    ((ConfigOptionFloats,               machine_max_acceleration_x))
    ((ConfigOptionFloats,               machine_max_acceleration_y))
    ((ConfigOptionFloats,               machine_max_acceleration_z))
    ((ConfigOptionFloats,               machine_max_acceleration_e))
    // M203 X... Y... Z... E... [mm/sec]
    ((ConfigOptionFloats,               machine_max_speed_x))
    ((ConfigOptionFloats,               machine_max_speed_y))
    ((ConfigOptionFloats,               machine_max_speed_z))
    ((ConfigOptionFloats,               machine_max_speed_e))

    // M204 P... R... T...[mm/sec^2]
    ((ConfigOptionFloats,               machine_max_acceleration_extruding))
    ((ConfigOptionFloats,               machine_max_acceleration_retracting))
    ((ConfigOptionFloats,               machine_max_acceleration_travel))

    // M205 X... Y... Z... E... [mm/sec]
    ((ConfigOptionFloats,               machine_max_jerk_x))
    ((ConfigOptionFloats,               machine_max_jerk_y))
    ((ConfigOptionFloats,               machine_max_jerk_z))
    ((ConfigOptionFloats,               machine_max_jerk_e))
    // M205 T... [mm/sec]
    ((ConfigOptionFloats,               machine_min_travel_rate))
    // M205 S... [mm/sec]
    ((ConfigOptionFloats,               machine_min_extruding_rate))
)

// This object is mapped to Perl as Slic3r::Config::GCode.
PRINT_CONFIG_CLASS_DEFINE(
    GCodeConfig,

    ((ConfigOptionString,              before_layer_change_gcode))
    ((ConfigOptionFloats,              deretraction_speed))
    //BBS
    ((ConfigOptionBool,                enable_arc_fitting))
    ((ConfigOptionString,              machine_end_gcode))
    ((ConfigOptionStrings,             filament_end_gcode))
    ((ConfigOptionFloats,              filament_flow_ratio))
    ((ConfigOptionBools,               enable_pressure_advance))
    ((ConfigOptionFloats,              pressure_advance))
    ((ConfigOptionFloats,              filament_diameter))
    ((ConfigOptionFloats,              filament_density))
    ((ConfigOptionStrings,             filament_type))
    ((ConfigOptionBools,               filament_soluble))
    ((ConfigOptionBools,               filament_is_support))
    ((ConfigOptionFloats,              filament_cost))
    ((ConfigOptionStrings,             default_filament_colour))
    ((ConfigOptionInts,                temperature_vitrification))  //BBS
    ((ConfigOptionFloats,              filament_max_volumetric_speed))
    ((ConfigOptionInts,                required_nozzle_HRC))
    ((ConfigOptionFloat,               machine_load_filament_time))
    ((ConfigOptionFloat,               machine_unload_filament_time))
    ((ConfigOptionFloats,              filament_minimal_purge_on_wipe_tower))
    // BBS
    ((ConfigOptionBool,                scan_first_layer))
    // ((ConfigOptionBool,                spaghetti_detector))
    ((ConfigOptionBool,                gcode_add_line_number))
    ((ConfigOptionBool,                bbl_bed_temperature_gcode))
    ((ConfigOptionEnum<GCodeFlavor>,   gcode_flavor))
    ((ConfigOptionString,              layer_change_gcode))
//#ifdef HAS_PRESSURE_EQUALIZER
//    ((ConfigOptionFloat,               max_volumetric_extrusion_rate_slope_positive))
//    ((ConfigOptionFloat,               max_volumetric_extrusion_rate_slope_negative))
//#endif
    ((ConfigOptionPercents,            retract_before_wipe))
    ((ConfigOptionFloats,              retraction_length))
    ((ConfigOptionFloats,              retract_length_toolchange))
    ((ConfigOptionFloats,              z_hop))
    // BBS
    ((ConfigOptionEnumsGeneric,        z_hop_types))
    ((ConfigOptionFloats,              retract_restart_extra))
    ((ConfigOptionFloats,              retract_restart_extra_toolchange))
    ((ConfigOptionFloats,              retraction_speed))
    ((ConfigOptionString,              machine_start_gcode))
    ((ConfigOptionStrings,             filament_start_gcode))
    ((ConfigOptionBool,                single_extruder_multi_material))
    ((ConfigOptionBool,                wipe_tower_no_sparse_layers))
    ((ConfigOptionString,              change_filament_gcode))
    ((ConfigOptionFloat,               travel_speed))
    ((ConfigOptionFloat,               travel_speed_z))
    ((ConfigOptionBool,                silent_mode))
    ((ConfigOptionString,              machine_pause_gcode))
    ((ConfigOptionString,              template_custom_gcode))
    //BBS
    ((ConfigOptionEnum<NozzleType>,    nozzle_type))
    ((ConfigOptionInt,                 nozzle_hrc))
    ((ConfigOptionBool,                auxiliary_fan))
    ((ConfigOptionBool,                accel_to_decel_enable))
    ((ConfigOptionPercent,             accel_to_decel_factor))
)

// This object is mapped to Perl as Slic3r::Config::Print.
PRINT_CONFIG_CLASS_DERIVED_DEFINE(
    PrintConfig,
    (MachineEnvelopeConfig, GCodeConfig),

    //BBS
    ((ConfigOptionInts,               additional_cooling_fan_speed))
    ((ConfigOptionBool,               reduce_crossing_wall))
    ((ConfigOptionFloatOrPercent,     max_travel_detour_distance))
    ((ConfigOptionPoints,             printable_area))
    //BBS: add bed_exclude_area
    ((ConfigOptionPoints,             bed_exclude_area))
    // BBS
    ((ConfigOptionEnum<BedType>,      curr_bed_type))
    ((ConfigOptionInts,               cool_plate_temp))
    ((ConfigOptionInts,               eng_plate_temp))
    ((ConfigOptionInts,               hot_plate_temp)) // hot is short for high temperature
    ((ConfigOptionInts,               textured_plate_temp))
    ((ConfigOptionInts,               cool_plate_temp_initial_layer))
    ((ConfigOptionInts,               eng_plate_temp_initial_layer))
    ((ConfigOptionInts,               hot_plate_temp_initial_layer)) // hot is short for high temperature
    ((ConfigOptionInts,               textured_plate_temp_initial_layer))
    ((ConfigOptionBools,              enable_overhang_bridge_fan))
    ((ConfigOptionInts,               overhang_fan_speed))
    ((ConfigOptionEnumsGeneric,       overhang_fan_threshold))
    ((ConfigOptionEnum<PrintSequence>,print_sequence))
    ((ConfigOptionBools,              slow_down_for_layer_cooling))
    ((ConfigOptionFloat,              default_acceleration))
    ((ConfigOptionInts,               close_fan_the_first_x_layers))
    ((ConfigOptionEnum<DraftShield>,  draft_shield))
    ((ConfigOptionFloat,              extruder_clearance_height_to_rod))//BBs
    ((ConfigOptionFloat,              extruder_clearance_height_to_lid))//BBS
    ((ConfigOptionFloat,              extruder_clearance_radius))
    ((ConfigOptionFloat,              extruder_clearance_max_radius))
    ((ConfigOptionStrings,            extruder_colour))
    ((ConfigOptionPoints,             extruder_offset))
    ((ConfigOptionBools,              reduce_fan_stop_start_freq))
    ((ConfigOptionInts,               fan_cooling_layer_time))
    ((ConfigOptionStrings,            filament_colour))
    ((ConfigOptionFloat,              top_surface_acceleration))
    ((ConfigOptionFloat,              outer_wall_acceleration))
    ((ConfigOptionFloat,              initial_layer_acceleration))
    ((ConfigOptionFloat,              initial_layer_line_width))
    ((ConfigOptionFloat,              initial_layer_print_height))
    ((ConfigOptionFloat,              initial_layer_speed))
    //BBS
    ((ConfigOptionFloat,              initial_layer_infill_speed))
    ((ConfigOptionInts,               nozzle_temperature_initial_layer))
    ((ConfigOptionInts,               full_fan_speed_layer))
    ((ConfigOptionEnum<WallInfillOrder>,wall_infill_order))
    ((ConfigOptionInts,               fan_max_speed))
    ((ConfigOptionFloats,             max_layer_height))
    ((ConfigOptionInts,               fan_min_speed))
    ((ConfigOptionFloats,             min_layer_height))
    ((ConfigOptionFloat,              printable_height))
    ((ConfigOptionFloats,             slow_down_min_speed))
    ((ConfigOptionFloats,             nozzle_diameter))
    ((ConfigOptionBool,               reduce_infill_retraction))
    ((ConfigOptionBool,               ooze_prevention))
    ((ConfigOptionString,             filename_format))
    ((ConfigOptionStrings,            post_process))
    ((ConfigOptionString,             printer_model))
    ((ConfigOptionFloat,              resolution))
    ((ConfigOptionFloats,             retraction_minimum_travel))
    ((ConfigOptionBools,              retract_when_changing_layer))
    ((ConfigOptionFloat,              skirt_distance))
    ((ConfigOptionInt,                skirt_height))
    ((ConfigOptionInt,                skirt_loops))
    ((ConfigOptionInts,               slow_down_layer_time))
    ((ConfigOptionBool,               spiral_mode))
    ((ConfigOptionInt,                standby_temperature_delta))
    ((ConfigOptionInts,               nozzle_temperature))
    ((ConfigOptionInt,                chamber_temperature))
    ((ConfigOptionBools,              wipe))
    // BBS
    ((ConfigOptionInts,               bed_temperature_difference))
    ((ConfigOptionInts,               nozzle_temperature_range_low))
    ((ConfigOptionInts,               nozzle_temperature_range_high))
    ((ConfigOptionFloats,             wipe_distance))
    ((ConfigOptionBool,               enable_prime_tower))
    // BBS: change wipe_tower_x and wipe_tower_y data type to floats to add partplate logic
    ((ConfigOptionFloats,             wipe_tower_x))
    ((ConfigOptionFloats,             wipe_tower_y))
    ((ConfigOptionFloat,              prime_tower_width))
    ((ConfigOptionFloat,              wipe_tower_per_color_wipe))
    ((ConfigOptionFloat,              wipe_tower_rotation_angle))
    ((ConfigOptionFloat,              prime_tower_brim_width))
    //((ConfigOptionFloat,              wipe_tower_bridging))
    ((ConfigOptionFloats,             flush_volumes_matrix))
    ((ConfigOptionFloats,             flush_volumes_vector))
    // BBS: wipe tower is only used for priming
    ((ConfigOptionFloat,              prime_volume))
    ((ConfigOptionFloat,              flush_multiplier))
    //((ConfigOptionFloat,              z_offset))
    // BBS: project filaments
    ((ConfigOptionFloats,             filament_colour_new))
    // BBS: not in any preset, calculated before slicing
    ((ConfigOptionBool,               has_prime_tower))
    ((ConfigOptionFloat,              nozzle_volume))
    ((ConfigOptionPoints,             start_end_points))
    ((ConfigOptionEnum<TimelapseType>,    timelapse_type))
    ((ConfigOptionFloat,              default_jerk))
    ((ConfigOptionFloat,              outer_wall_jerk))
    ((ConfigOptionFloat,              inner_wall_jerk))
    ((ConfigOptionFloat,              infill_jerk))
    ((ConfigOptionFloat,              top_surface_jerk))
    ((ConfigOptionFloat,              initial_layer_jerk))
    ((ConfigOptionFloat,              travel_jerk))

    // BBS: move from PrintObjectConfig
    ((ConfigOptionBool, independent_support_layer_height))
)

// This object is mapped to Perl as Slic3r::Config::Full.
PRINT_CONFIG_CLASS_DERIVED_DEFINE0(
    FullPrintConfig,
    (PrintObjectConfig, PrintRegionConfig, PrintConfig)
)

// Validate the FullPrintConfig. Returns an empty string on success, otherwise an error message is returned.
std::map<std::string, std::string> validate(const FullPrintConfig &config, bool under_cli = false);

PRINT_CONFIG_CLASS_DEFINE(
    SLAPrintConfig,
    ((ConfigOptionString,     filename_format))
)

PRINT_CONFIG_CLASS_DEFINE(
    SLAPrintObjectConfig,

    ((ConfigOptionFloat, layer_height))

    //Number of the layers needed for the exposure time fade [3;20]
    ((ConfigOptionInt,  faded_layers))/*= 10*/

    ((ConfigOptionFloat, slice_closing_radius))

    // Enabling or disabling support creation
    ((ConfigOptionBool,  supports_enable))

    // Diameter in mm of the pointing side of the head.
    ((ConfigOptionFloat, support_head_front_diameter))/*= 0.2*/

    // How much the pinhead has to penetrate the model surface
    ((ConfigOptionFloat, support_head_penetration))/*= 0.2*/

    // Width in mm from the back sphere center to the front sphere center.
    ((ConfigOptionFloat, support_head_width))/*= 1.0*/

    // Radius in mm of the support pillars.
    ((ConfigOptionFloat, support_pillar_diameter))/*= 0.8*/

    // The percentage of smaller pillars compared to the normal pillar diameter
    // which are used in problematic areas where a normal pilla cannot fit.
    ((ConfigOptionPercent, support_small_pillar_diameter_percent))

    // How much bridge (supporting another pinhead) can be placed on a pillar.
    ((ConfigOptionInt,   support_max_bridges_on_pillar))

    // How the pillars are bridged together
    ((ConfigOptionEnum<SLAPillarConnectionMode>, support_pillar_connection_mode))

    // Generate only ground facing supports
    ((ConfigOptionBool, support_buildplate_only))

    // TODO: unimplemented at the moment. This coefficient will have an impact
    // when bridges and pillars are merged. The resulting pillar should be a bit
    // thicker than the ones merging into it. How much thicker? I don't know
    // but it will be derived from this value.
    ((ConfigOptionFloat, support_pillar_widening_factor))

    // Radius in mm of the pillar base.
    ((ConfigOptionFloat, support_base_diameter))/*= 2.0*/

    // The height of the pillar base cone in mm.
    ((ConfigOptionFloat, support_base_height))/*= 1.0*/

    // The minimum distance of the pillar base from the model in mm.
    ((ConfigOptionFloat, support_base_safety_distance)) /*= 1.0*/

    // The default angle for connecting support sticks and junctions.
    ((ConfigOptionFloat, support_critical_angle))/*= 45*/

    // The max length of a bridge in mm
    ((ConfigOptionFloat, support_max_bridge_length))/*= 15.0*/

    // The max distance of two pillars to get cross linked.
    ((ConfigOptionFloat, support_max_pillar_link_distance))

    // The elevation in Z direction upwards. This is the space between the pad
    // and the model object's bounding box bottom. Units in mm.
    ((ConfigOptionFloat, support_object_elevation))/*= 5.0*/

    /////// Following options influence automatic support points placement:
    ((ConfigOptionInt, support_points_density_relative))
    ((ConfigOptionFloat, support_points_minimal_distance))

    // Now for the base pool (pad) /////////////////////////////////////////////

    // Enabling or disabling support creation
    ((ConfigOptionBool,  pad_enable))

    // The thickness of the pad walls
    ((ConfigOptionFloat, pad_wall_thickness))/*= 2*/

    // The height of the pad from the bottom to the top not considering the pit
    ((ConfigOptionFloat, pad_wall_height))/*= 5*/

    // How far should the pad extend around the contained geometry
    ((ConfigOptionFloat, pad_brim_size))

    // The greatest distance where two individual pads are merged into one. The
    // distance is measured roughly from the centroids of the pads.
    ((ConfigOptionFloat, pad_max_merge_distance))/*= 50*/

    // The smoothing radius of the pad edges
    // ((ConfigOptionFloat, pad_edge_radius))/*= 1*/;

    // The slope of the pad wall...
    ((ConfigOptionFloat, pad_wall_slope))

    // /////////////////////////////////////////////////////////////////////////
    // Zero elevation mode parameters:
    //    - The object pad will be derived from the model geometry.
    //    - There will be a gap between the object pad and the generated pad
    //      according to the support_base_safety_distance parameter.
    //    - The two pads will be connected with tiny connector sticks
    // /////////////////////////////////////////////////////////////////////////

    // Disable the elevation (ignore its value) and use the zero elevation mode
    ((ConfigOptionBool, pad_around_object))

    ((ConfigOptionBool, pad_around_object_everywhere))

    // This is the gap between the object bottom and the generated pad
    ((ConfigOptionFloat, pad_object_gap))

    // How far to place the connector sticks on the object pad perimeter
    ((ConfigOptionFloat, pad_object_connector_stride))

    // The width of the connectors sticks
    ((ConfigOptionFloat, pad_object_connector_width))

    // How much should the tiny connectors penetrate into the model body
    ((ConfigOptionFloat, pad_object_connector_penetration))

    // /////////////////////////////////////////////////////////////////////////
    // Model hollowing parameters:
    //   - Models can be hollowed out as part of the SLA print process
    //   - Thickness of the hollowed model walls can be adjusted
    //   -
    //   - Additional holes will be drilled into the hollow model to allow for
    //   - resin removal.
    // /////////////////////////////////////////////////////////////////////////

    ((ConfigOptionBool, hollowing_enable))

    // The minimum thickness of the model walls to maintain. Note that the
    // resulting walls may be thicker due to smoothing out fine cavities where
    // resin could stuck.
    ((ConfigOptionFloat, hollowing_min_thickness))

    // Indirectly controls the voxel size (resolution) used by openvdb
    ((ConfigOptionFloat, hollowing_quality))

    // Indirectly controls the minimum size of created cavities.
    ((ConfigOptionFloat, hollowing_closing_distance))
)

enum SLAMaterialSpeed { slamsSlow, slamsFast };

PRINT_CONFIG_CLASS_DEFINE(
    SLAMaterialConfig,

    ((ConfigOptionFloat,                       initial_layer_height))
    ((ConfigOptionFloat,                       bottle_cost))
    ((ConfigOptionFloat,                       bottle_volume))
    ((ConfigOptionFloat,                       bottle_weight))
    ((ConfigOptionFloat,                       material_density))
    ((ConfigOptionFloat,                       exposure_time))
    ((ConfigOptionFloat,                       initial_exposure_time))
    ((ConfigOptionFloats,                      material_correction))
    ((ConfigOptionFloat,                       material_correction_x))
    ((ConfigOptionFloat,                       material_correction_y))
    ((ConfigOptionFloat,                       material_correction_z))
    ((ConfigOptionEnum<SLAMaterialSpeed>,      material_print_speed))
)

PRINT_CONFIG_CLASS_DEFINE(
    SLAPrinterConfig,

    ((ConfigOptionEnum<PrinterTechnology>,    printer_technology))
    ((ConfigOptionPoints,                     printable_area))
    ((ConfigOptionFloat,                      printable_height))
    ((ConfigOptionFloat,                      display_width))
    ((ConfigOptionFloat,                      display_height))
    ((ConfigOptionInt,                        display_pixels_x))
    ((ConfigOptionInt,                        display_pixels_y))
    ((ConfigOptionEnum<SLADisplayOrientation>,display_orientation))
    ((ConfigOptionBool,                       display_mirror_x))
    ((ConfigOptionBool,                       display_mirror_y))
    ((ConfigOptionFloats,                     relative_correction))
    ((ConfigOptionFloat,                      relative_correction_x))
    ((ConfigOptionFloat,                      relative_correction_y))
    ((ConfigOptionFloat,                      relative_correction_z))
    ((ConfigOptionFloat,                      absolute_correction))
    ((ConfigOptionFloat,                      elefant_foot_compensation))
    ((ConfigOptionFloat,                      elefant_foot_min_width))
    ((ConfigOptionFloat,                      gamma_correction))
    ((ConfigOptionFloat,                      fast_tilt_time))
    ((ConfigOptionFloat,                      slow_tilt_time))
    ((ConfigOptionFloat,                      area_fill))
    ((ConfigOptionFloat,                      min_exposure_time))
    ((ConfigOptionFloat,                      max_exposure_time))
    ((ConfigOptionFloat,                      min_initial_exposure_time))
    ((ConfigOptionFloat,                      max_initial_exposure_time))
)

PRINT_CONFIG_CLASS_DERIVED_DEFINE0(
    SLAFullPrintConfig,
    (SLAPrinterConfig, SLAPrintConfig, SLAPrintObjectConfig, SLAMaterialConfig)
)

#undef STATIC_PRINT_CONFIG_CACHE
#undef STATIC_PRINT_CONFIG_CACHE_BASE
#undef STATIC_PRINT_CONFIG_CACHE_DERIVED
#undef PRINT_CONFIG_CLASS_ELEMENT_DEFINITION
#undef PRINT_CONFIG_CLASS_ELEMENT_EQUAL
#undef PRINT_CONFIG_CLASS_ELEMENT_LOWER
#undef PRINT_CONFIG_CLASS_ELEMENT_HASH
#undef PRINT_CONFIG_CLASS_ELEMENT_INITIALIZATION
#undef PRINT_CONFIG_CLASS_ELEMENT_INITIALIZATION2
#undef PRINT_CONFIG_CLASS_DEFINE
#undef PRINT_CONFIG_CLASS_DERIVED_CLASS_LIST
#undef PRINT_CONFIG_CLASS_DERIVED_CLASS_LIST_ITEM
#undef PRINT_CONFIG_CLASS_DERIVED_DEFINE
#undef PRINT_CONFIG_CLASS_DERIVED_DEFINE0
#undef PRINT_CONFIG_CLASS_DERIVED_DEFINE1
#undef PRINT_CONFIG_CLASS_DERIVED_HASH
#undef PRINT_CONFIG_CLASS_DERIVED_EQUAL
#undef PRINT_CONFIG_CLASS_DERIVED_INITCACHE_ITEM
#undef PRINT_CONFIG_CLASS_DERIVED_INITCACHE
#undef PRINT_CONFIG_CLASS_DERIVED_INITIALIZER
#undef PRINT_CONFIG_CLASS_DERIVED_INITIALIZER_ITEM

class CLIActionsConfigDef : public ConfigDef
{
public:
    CLIActionsConfigDef();
};

class CLITransformConfigDef : public ConfigDef
{
public:
    CLITransformConfigDef();
};

class CLIMiscConfigDef : public ConfigDef
{
public:
    CLIMiscConfigDef();
};

// This class defines the command line options representing actions.
extern const CLIActionsConfigDef    cli_actions_config_def;

// This class defines the command line options representing transforms.
extern const CLITransformConfigDef  cli_transform_config_def;

// This class defines all command line options that are not actions or transforms.
extern const CLIMiscConfigDef       cli_misc_config_def;

class DynamicPrintAndCLIConfig : public DynamicPrintConfig
{
public:
    DynamicPrintAndCLIConfig() {}
    DynamicPrintAndCLIConfig(const DynamicPrintAndCLIConfig &other) : DynamicPrintConfig(other) {}

    // Overrides ConfigBase::def(). Static configuration definition. Any value stored into this ConfigBase shall have its definition here.
    const ConfigDef*        def() const override { return &s_def; }

    // Verify whether the opt_key has not been obsoleted or renamed.
    // Both opt_key and value may be modified by handle_legacy().
    // If the opt_key is no more valid in this version of Slic3r, opt_key is cleared by handle_legacy().
    // handle_legacy() is called internally by set_deserialize().
    void                    handle_legacy(t_config_option_key &opt_key, std::string &value) const override;

private:
    class PrintAndCLIConfigDef : public ConfigDef
    {
    public:
        PrintAndCLIConfigDef() {
            this->options.insert(print_config_def.options.begin(), print_config_def.options.end());
            this->options.insert(cli_actions_config_def.options.begin(), cli_actions_config_def.options.end());
            this->options.insert(cli_transform_config_def.options.begin(), cli_transform_config_def.options.end());
            this->options.insert(cli_misc_config_def.options.begin(), cli_misc_config_def.options.end());
            for (const auto &kvp : this->options)
                this->by_serialization_key_ordinal[kvp.second.serialization_key_ordinal] = &kvp.second;
        }
        // Do not release the default values, they are handled by print_config_def & cli_actions_config_def / cli_transform_config_def / cli_misc_config_def.
        ~PrintAndCLIConfigDef() { this->options.clear(); }
    };
    static PrintAndCLIConfigDef s_def;
};

Points get_bed_shape(const DynamicPrintConfig &cfg);
Points get_bed_shape(const PrintConfig &cfg);
Points get_bed_shape(const SLAPrinterConfig &cfg);
Slic3r::Polygon get_bed_shape_with_excluded_area(const PrintConfig& cfg);

// ModelConfig is a wrapper around DynamicPrintConfig with an addition of a timestamp.
// Each change of ModelConfig is tracked by assigning a new timestamp from a global counter.
// The counter is used for faster synchronization of the background slicing thread
// with the front end by skipping synchronization of equal config dictionaries.
// The global counter is also used for avoiding unnecessary serialization of config
// dictionaries when taking an Undo snapshot.
//
// The global counter is NOT thread safe, therefore it is recommended to use ModelConfig from
// the main thread only.
//
// As there is a global counter and it is being increased with each change to any ModelConfig,
// if two ModelConfig dictionaries differ, they should differ with their timestamp as well.
// Therefore copying the ModelConfig including its timestamp is safe as there is no harm
// in having multiple ModelConfig with equal timestamps as long as their dictionaries are equal.
//
// The timestamp is used by the Undo/Redo stack. As zero timestamp means invalid timestamp
// to the Undo/Redo stack (zero timestamp means the Undo/Redo stack needs to serialize and
// compare serialized data for differences), zero timestamp shall never be used.
// Timestamp==1 shall only be used for empty dictionaries.
class ModelConfig
{
public:
    // Following method clears the config and increases its timestamp, so the deleted
    // state is considered changed from perspective of the undo/redo stack.
    void         reset() { m_data.clear(); touch(); }

    void         assign_config(const ModelConfig &rhs) {
        if (m_timestamp != rhs.m_timestamp) {
            m_data      = rhs.m_data;
            m_timestamp = rhs.m_timestamp;
        }
    }
    void         assign_config(ModelConfig &&rhs) {
        if (m_timestamp != rhs.m_timestamp) {
            m_data      = std::move(rhs.m_data);
            m_timestamp = rhs.m_timestamp;
            rhs.reset();
        }
    }

    // Modification of the ModelConfig is not thread safe due to the global timestamp counter!
    // Don't call modification methods from the back-end!
    // Assign methods don't assign if src==dst to not having to bump the timestamp in case they are equal.
    void         assign_config(const DynamicPrintConfig &rhs)  { if (m_data != rhs) { m_data = rhs; this->touch(); } }
    void         assign_config(DynamicPrintConfig &&rhs)       { if (m_data != rhs) { m_data = std::move(rhs); this->touch(); } }
    void         apply(const ModelConfig &other, bool ignore_nonexistent = false) { this->apply(other.get(), ignore_nonexistent); }
    void         apply(const ConfigBase &other, bool ignore_nonexistent = false) { m_data.apply_only(other, other.keys(), ignore_nonexistent); this->touch(); }
    void         apply_only(const ModelConfig &other, const t_config_option_keys &keys, bool ignore_nonexistent = false) { this->apply_only(other.get(), keys, ignore_nonexistent); }
    void         apply_only(const ConfigBase &other, const t_config_option_keys &keys, bool ignore_nonexistent = false) { m_data.apply_only(other, keys, ignore_nonexistent); this->touch(); }
    bool         set_key_value(const std::string &opt_key, ConfigOption *opt) { bool out = m_data.set_key_value(opt_key, opt); this->touch(); return out; }
    template<typename T>
    void         set(const std::string &opt_key, T value) { m_data.set(opt_key, value, true); this->touch(); }
    void         set_deserialize(const t_config_option_key &opt_key, const std::string &str, ConfigSubstitutionContext &substitution_context, bool append = false)
        { m_data.set_deserialize(opt_key, str, substitution_context, append); this->touch(); }
    bool         erase(const t_config_option_key &opt_key) { bool out = m_data.erase(opt_key); if (out) this->touch(); return out; }

    // Getters are thread safe.
    // The following implicit conversion breaks the Cereal serialization.
//    operator const DynamicPrintConfig&() const throw() { return this->get(); }
    const DynamicPrintConfig&   get() const throw() { return m_data; }
    bool                        empty() const throw() { return m_data.empty(); }
    size_t                      size() const throw() { return m_data.size(); }
    auto                        cbegin() const { return m_data.cbegin(); }
    auto                        cend() const { return m_data.cend(); }
    t_config_option_keys        keys() const { return m_data.keys(); }
    bool                        has(const t_config_option_key &opt_key) const { return m_data.has(opt_key); }
    const ConfigOption*         option(const t_config_option_key &opt_key) const { return m_data.option(opt_key); }
    int                         opt_int(const t_config_option_key &opt_key) const { return m_data.opt_int(opt_key); }
    int                         extruder() const { return opt_int("extruder"); }
    double                      opt_float(const t_config_option_key &opt_key) const { return m_data.opt_float(opt_key); }
    std::string                 opt_serialize(const t_config_option_key &opt_key) const { return m_data.opt_serialize(opt_key); }

    // Return an optional timestamp of this object.
    // If the timestamp returned is non-zero, then the serialization framework will
    // only save this object on the Undo/Redo stack if the timestamp is different
    // from the timestmap of the object at the top of the Undo / Redo stack.
    virtual uint64_t    timestamp() const throw() { return m_timestamp; }
    bool                timestamp_matches(const ModelConfig &rhs) const throw() { return m_timestamp == rhs.m_timestamp; }
    // Not thread safe! Should not be called from other than the main thread!
    void                touch() { m_timestamp = ++ s_last_timestamp; }

private:
    friend class cereal::access;
    template<class Archive> void serialize(Archive& ar) { ar(m_timestamp); ar(m_data); }

    uint64_t                    m_timestamp { 1 };
    DynamicPrintConfig          m_data;

    static uint64_t             s_last_timestamp;
};

} // namespace Slic3r

// Serialization through the Cereal library
namespace cereal {
    // Let cereal know that there are load / save non-member functions declared for DynamicPrintConfig, ignore serialize / load / save from parent class DynamicConfig.
    template <class Archive> struct specialize<Archive, Slic3r::DynamicPrintConfig, cereal::specialization::non_member_load_save> {};

    template<class Archive> void load(Archive& archive, Slic3r::DynamicPrintConfig &config)
    {
        size_t cnt;
        archive(cnt);
        config.clear();
        for (size_t i = 0; i < cnt; ++ i) {
            size_t serialization_key_ordinal;
            archive(serialization_key_ordinal);
            assert(serialization_key_ordinal > 0);
            auto it = Slic3r::print_config_def.by_serialization_key_ordinal.find(serialization_key_ordinal);
            assert(it != Slic3r::print_config_def.by_serialization_key_ordinal.end());
            config.set_key_value(it->second->opt_key, it->second->load_option_from_archive(archive));
        }
    }

    template<class Archive> void save(Archive& archive, const Slic3r::DynamicPrintConfig &config)
    {
        size_t cnt = config.size();
        archive(cnt);
        for (auto it = config.cbegin(); it != config.cend(); ++it) {
            const Slic3r::ConfigOptionDef* optdef = Slic3r::print_config_def.get(it->first);
            assert(optdef != nullptr);
            assert(optdef->serialization_key_ordinal > 0);
            archive(optdef->serialization_key_ordinal);
            optdef->save_option_to_archive(archive, it->second.get());
        }
    }
}

#endif
