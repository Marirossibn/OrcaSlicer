#ifndef slic3r_GCodeWriter_hpp_
#define slic3r_GCodeWriter_hpp_

#include "libslic3r.h"
#include <string>
#include <charconv>
#include "Extruder.hpp"
#include "Point.hpp"
#include "PrintConfig.hpp"
#include "GCode/CoolingBuffer.hpp"

namespace Slic3r {

enum class LiftType {
    NormalLift,
    LazyLift,
    SpiralLift
};

class GCodeWriter {
public:
    GCodeConfig config;
    bool multiple_extruders;
    
    GCodeWriter() : 
        multiple_extruders(false), m_extruder(nullptr),
        m_single_extruder_multi_material(false),
        m_last_acceleration(0), m_max_acceleration(0),
        /*m_last_bed_temperature(0), */m_last_bed_temperature_reached(true),
        m_lifted(0),
        m_to_lift(0),
        m_to_lift_type(LiftType::NormalLift)
        {}
    Extruder*            extruder()             { return m_extruder; }
    const Extruder*      extruder()     const   { return m_extruder; }

    void                 apply_print_config(const PrintConfig &print_config);
    // Extruders are expected to be sorted in an increasing order.
    void                 set_extruders(std::vector<unsigned int> extruder_ids);
    const std::vector<Extruder>& extruders() const { return m_extruders; }
    std::vector<unsigned int> extruder_ids() const { 
        std::vector<unsigned int> out; 
        out.reserve(m_extruders.size()); 
        for (const Extruder &e : m_extruders) 
            out.push_back(e.id()); 
        return out;
    }
    std::string preamble();
    std::string postamble() const;
    std::string set_temperature(unsigned int temperature, bool wait = false, int tool = -1) const;
    std::string set_bed_temperature(int temperature, bool wait = false);
    std::string set_acceleration(unsigned int acceleration);
    std::string reset_e(bool force = false);
    std::string update_progress(unsigned int num, unsigned int tot, bool allow_100 = false) const;
    // return false if this extruder was already selected
    bool        need_toolchange(unsigned int extruder_id) const 
        { return m_extruder == nullptr || m_extruder->id() != extruder_id; }
    std::string set_extruder(unsigned int extruder_id)
        { return this->need_toolchange(extruder_id) ? this->toolchange(extruder_id) : ""; }
    // Prefix of the toolchange G-code line, to be used by the CoolingBuffer to separate sections of the G-code
    // printed with the same extruder.
    std::string toolchange_prefix() const;
    std::string toolchange(unsigned int extruder_id);
    std::string set_speed(double F, const std::string &comment = std::string(), const std::string &cooling_marker = std::string()) const;
    std::string travel_to_xy(const Vec2d &point, const std::string &comment = std::string());
    std::string travel_to_xyz(const Vec3d &point, const std::string &comment = std::string());
    std::string travel_to_z(double z, const std::string &comment = std::string());
    bool        will_move_z(double z) const;
    std::string extrude_to_xy(const Vec2d &point, double dE, const std::string &comment = std::string(), bool force_no_extrusion = false);
    //BBS: generate G2 or G3 extrude which moves by arc
    std::string extrude_arc_to_xy(const Vec2d &point, const Vec2d &center_offset, double dE, const bool is_ccw, const std::string &comment = std::string(), bool force_no_extrusion = false);
    std::string extrude_to_xyz(const Vec3d &point, double dE, const std::string &comment = std::string(), bool force_no_extrusion = false);
    std::string retract(bool before_wipe = false);
    std::string retract_for_toolchange(bool before_wipe = false);
    std::string unretract();
    std::string lift(LiftType lift_type = LiftType::NormalLift);
    std::string unlift();
    Vec3d       get_position() const { return m_pos; }

    //BBS: set offset for gcode writer
    void set_xy_offset(double x, double y) { m_x_offset = x; m_y_offset = y; }

    // To be called by the CoolingBuffer from another thread.
    static std::string set_fan(const GCodeFlavor gcode_flavor, unsigned int speed);
    // To be called by the main thread. It always emits the G-code, it does not remember the previous state.
    // Keeping the state is left to the CoolingBuffer, which runs asynchronously on another thread.
    std::string set_fan(unsigned int speed) const;
    //BBS: set additional fan speed for BBS machine only
    static std::string set_additional_fan(unsigned int speed);

    //BBS:
    void set_current_position_clear(bool clear) { m_is_current_pos_clear = clear; };
    bool is_current_position_clear() const { return m_is_current_pos_clear; };
    //BBS:
    static const bool full_gcode_comment;

private:
	// Extruders are sorted by their ID, so that binary search is possible.
    std::vector<Extruder> m_extruders;
    bool            m_single_extruder_multi_material;
    Extruder*       m_extruder;
    unsigned int    m_last_acceleration;
    // Limit for setting the acceleration, to respect the machine limits set for the Marlin firmware.
    // If set to zero, the limit is not in action.
    unsigned int    m_max_acceleration;
    //BBS
    unsigned int    m_last_additional_fan_speed;
    int             m_last_bed_temperature;
    bool            m_last_bed_temperature_reached;
    double          m_lifted;

    // BBS
    double          m_to_lift;
    LiftType        m_to_lift_type;
    Vec3d           m_pos = Vec3d::Zero();
    //BBS: this flag is used to indicate whether the m_pos is real.
    //A example that of the first move, the m_pos is zero, but the real position of extruder doesn't
    //Pos must be clear after the first xyz travel move
    bool            m_is_current_pos_clear = false;
    //BBS: x, y offset for gcode generated
    double          m_x_offset{ 0 };
    double          m_y_offset{ 0 };

    //Radian threshold of slope for lazy lift and spiral lift;
    static const double slope_threshold;

    std::string _travel_to_z(double z, const std::string &comment);
    std::string _spiral_travel_to_z(double z, const Vec2d &ij_offset, const std::string &comment);
    std::string _retract(double length, double restart_extra, const std::string &comment);
};

class GCodeFormatter {
public:
    GCodeFormatter() {
        this->buf_end = buf + buflen;
        this->ptr_err.ptr = this->buf;
    }

    GCodeFormatter(const GCodeFormatter&) = delete;
    GCodeFormatter& operator=(const GCodeFormatter&) = delete;

    // At layer height 0.15mm, extrusion width 0.2mm and filament diameter 1.75mm,
    // the crossection of extrusion is 0.4 * 0.15 = 0.06mm2
    // and the filament crossection is 1.75^2 = 3.063mm2
    // thus the filament moves 3.063 / 0.6 = 51x slower than the XY axes
    // and we need roughly two decimal digits more on extruder than on XY.
#if 1
    static constexpr const int XYZF_EXPORT_DIGITS = 3;
    static constexpr const int E_EXPORT_DIGITS    = 5;
#else
    // order of magnitude smaller extrusion rate erros
    static constexpr const int XYZF_EXPORT_DIGITS = 4;
    static constexpr const int E_EXPORT_DIGITS    = 6;
    // excessive accuracy
//    static constexpr const int XYZF_EXPORT_DIGITS = 6;
//    static constexpr const int E_EXPORT_DIGITS    = 9;
#endif

    void emit_axis(const char axis, const double v, size_t digits);

    void emit_xy(const Vec2d &point) {
        this->emit_axis('X', point.x(), XYZF_EXPORT_DIGITS);
        this->emit_axis('Y', point.y(), XYZF_EXPORT_DIGITS);
    }

    void emit_xyz(const Vec3d &point) {
        this->emit_axis('X', point.x(), XYZF_EXPORT_DIGITS);
        this->emit_axis('Y', point.y(), XYZF_EXPORT_DIGITS);
        this->emit_z(point.z());
    }

    void emit_z(const double z) {
        this->emit_axis('Z', z, XYZF_EXPORT_DIGITS);
    }

    void emit_e(double v) {
        this->emit_axis('E', v, E_EXPORT_DIGITS);
    }

    void emit_f(double speed) {
        this->emit_axis('F', speed, XYZF_EXPORT_DIGITS);
    }
    //BBS
    void emit_ij(const Vec2d &point) {
        this->emit_axis('I', point.x(), XYZF_EXPORT_DIGITS);
        this->emit_axis('J', point.y(), XYZF_EXPORT_DIGITS);
    }

    void emit_string(const std::string &s) {
        strncpy(ptr_err.ptr, s.c_str(), s.size());
        ptr_err.ptr += s.size();
    }

    void emit_comment(bool allow_comments, const std::string &comment) {
        if (allow_comments && ! comment.empty()) {
            *ptr_err.ptr ++ = ' '; *ptr_err.ptr ++ = ';'; *ptr_err.ptr ++ = ' ';
            this->emit_string(comment);
        }
    }

    std::string string() {
        *ptr_err.ptr ++ = '\n';
        return std::string(this->buf, ptr_err.ptr - buf);
    }

protected:
    static constexpr const size_t   buflen = 256;
    char                            buf[buflen];
    char* buf_end;
    std::to_chars_result            ptr_err;
};

class GCodeG1Formatter : public GCodeFormatter {
public:
    GCodeG1Formatter() {
        this->buf[0] = 'G';
        this->buf[1] = '1';
        this->buf_end = buf + buflen;
        this->ptr_err.ptr = this->buf + 2;
    }

    GCodeG1Formatter(const GCodeG1Formatter&) = delete;
    GCodeG1Formatter& operator=(const GCodeG1Formatter&) = delete;
};

class GCodeG2G3Formatter : public GCodeFormatter {
public:
    GCodeG2G3Formatter(bool is_ccw) {
        this->buf[0] = 'G';
        this->buf[1] = is_ccw ? '3' : '2';
        this->buf_end = buf + buflen;
        this->ptr_err.ptr = this->buf + 2;
    }

    GCodeG2G3Formatter(const GCodeG2G3Formatter&) = delete;
    GCodeG2G3Formatter& operator=(const GCodeG2G3Formatter&) = delete;
};

} /* namespace Slic3r */

#endif /* slic3r_GCodeWriter_hpp_ */
