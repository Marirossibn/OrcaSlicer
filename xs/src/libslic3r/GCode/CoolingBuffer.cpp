#include "CoolingBuffer.hpp"
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <iostream>

namespace Slic3r {

std::string
CoolingBuffer::append(const std::string &gcode, std::string obj_id, size_t layer_id, float print_z)
{
    std::string out;
    if (this->_last_z.find(obj_id) != this->_last_z.end()) {
        // A layer was finished, Z of the object's layer changed. Process the layer.
        out = this->flush();
    }
    
    this->_layer_id = layer_id;
    this->_last_z[obj_id] = print_z;
    this->_gcode += gcode;
    // This is a very rough estimate of the print time, 
    // not taking into account the acceleration curves generated by the printer firmware.
    this->_elapsed_time += this->_gcodegen->elapsed_time;
    this->_gcodegen->elapsed_time = 0;
    
    return out;
}

void
apply_speed_factor(std::string &line, float speed_factor, float min_print_speed)
{
    // find pos of F
    size_t pos = line.find_first_of('F');
    size_t last_pos = line.find_first_of(' ', pos+1);
    
    // extract current speed
    float speed;
    {
        std::istringstream iss(line.substr(pos+1, last_pos));
        iss >> speed;
    }
    
    // change speed
    speed *= speed_factor;
    speed = std::max(speed, min_print_speed);
    
    // replace speed in string
    {
        std::ostringstream oss;
        oss << speed;
        line.replace(pos+1, (last_pos-pos), oss.str());
    }
}

std::string
CoolingBuffer::flush()
{
    GCode &gg = *this->_gcodegen;
    
    std::string gcode   = this->_gcode;
    float elapsed       = this->_elapsed_time;
    this->_gcode        = "";
    this->_elapsed_time = 0;
    this->_last_z.clear(); // reset the whole table otherwise we would compute overlapping times
    
    int fan_speed = gg.config.fan_always_on ? gg.config.min_fan_speed.value : 0;
    
    float speed_factor = 1.0;
    
    if (gg.config.cooling) {
        #ifdef SLIC3R_DEBUG
        printf("Layer %zu estimated printing time: %f seconds\n", this->_layer_id, elapsed);
        #endif        
        if (elapsed < (float)gg.config.slowdown_below_layer_time) {
            // Layer time very short. Enable the fan to a full throttle and slow down the print
            // (stretch the layer print time to slowdown_below_layer_time).
            fan_speed = gg.config.max_fan_speed;
            speed_factor = elapsed / (float)gg.config.slowdown_below_layer_time;
        } else if (elapsed < (float)gg.config.fan_below_layer_time) {
            // Layer time quite short. Enable the fan proportionally according to the current layer time.
            fan_speed = gg.config.max_fan_speed
                - (gg.config.max_fan_speed - gg.config.min_fan_speed)
                * (elapsed - (float)gg.config.slowdown_below_layer_time)
                / (gg.config.fan_below_layer_time - gg.config.slowdown_below_layer_time);
        }
        
        #ifdef SLIC3R_DEBUG
        printf("  fan = %d%%, speed = %f%%\n", fan_speed, speed_factor * 100);
        #endif
        
        if (speed_factor < 1.0) {
            // Adjust feed rate of G1 commands marked with an _EXTRUDE_SET_SPEED
            // as long as they are not _WIPE moves (they cannot if they are _EXTRUDE_SET_SPEED)
            // and they are not preceded directly by _BRIDGE_FAN_START (do not adjust bridging speed).
            std::string new_gcode;
            std::istringstream ss(gcode);
            std::string line;
            bool bridge_fan_start = false;
            while (std::getline(ss, line)) {
                if (boost::starts_with(line, "G1")
                    && boost::contains(line, ";_EXTRUDE_SET_SPEED")
                    && !boost::contains(line, ";_WIPE")
                    && !bridge_fan_start) {
                    apply_speed_factor(line, speed_factor, this->_min_print_speed);
                    boost::replace_first(line, ";_EXTRUDE_SET_SPEED", "");
                }
                bridge_fan_start = boost::contains(line, ";_BRIDGE_FAN_START");
                new_gcode += line + '\n';
            }
            gcode = new_gcode;
        }
    }
    if (this->_layer_id < gg.config.disable_fan_first_layers)
        fan_speed = 0;
    
    gcode = gg.writer.set_fan(fan_speed) + gcode;
    
    // bridge fan speed
    if (!gg.config.cooling || gg.config.bridge_fan_speed == 0 || this->_layer_id < gg.config.disable_fan_first_layers) {
        boost::replace_all(gcode, ";_BRIDGE_FAN_START", "");
        boost::replace_all(gcode, ";_BRIDGE_FAN_END", "");
    } else {
        boost::replace_all(gcode, ";_BRIDGE_FAN_START", gg.writer.set_fan(gg.config.bridge_fan_speed, true));
        boost::replace_all(gcode, ";_BRIDGE_FAN_END",   gg.writer.set_fan(fan_speed, true));
    }
    boost::replace_all(gcode, ";_WIPE", "");
    boost::replace_all(gcode, ";_EXTRUDE_SET_SPEED", "");
    
    return gcode;
}

}
