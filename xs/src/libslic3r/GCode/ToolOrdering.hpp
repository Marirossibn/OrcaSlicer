// Ordering of the tools to minimize tool switches.

#ifndef slic3r_ToolOrdering_hpp_
#define slic3r_ToolOrdering_hpp_

#include "libslic3r.h"

namespace Slic3r {

class Print;
class PrintObject;



// Object of this class holds information about whether an extrusion is printed immediately
// after a toolchange (as part of infill/perimeter wiping) or not. One extrusion can be a part
// of several copies - this has to be taken into account.
class WipingExtrusions
{
    public:
    bool is_anything_overridden() const {   // if there are no overrides, all the agenda can be skipped - this function can tell us if that's the case
        return something_overridden;
    }

    // Returns true in case that entity is not printed with its usual extruder for a given copy:
    bool is_entity_overridden(const ExtrusionEntity* entity, int copy_id) const {
        return (entity_map.find(entity) == entity_map.end() ? false : entity_map.at(entity).at(copy_id) != -1);
    }

    // This function is called from Print::mark_wiping_extrusions and sets extruder that it should be printed with (-1 .. as usual)
    void set_extruder_override(const ExtrusionEntity* entity, unsigned int copy_id, int extruder, unsigned int num_of_copies);

    // This is called from GCode::process_layer - see implementation for further comments:
    const std::vector<int>* get_extruder_overrides(const ExtrusionEntity* entity, int correct_extruder_id, int num_of_copies);

private:
    std::map<const ExtrusionEntity*, std::vector<int>> entity_map;  // to keep track of who prints what
    bool something_overridden = false;
};



class ToolOrdering 
{
public:
	struct LayerTools
	{
	    LayerTools(const coordf_t z) :
	    	print_z(z), 
	    	has_object(false),
			has_support(false),
			has_wipe_tower(false),
	    	wipe_tower_partitions(0),
	    	wipe_tower_layer_height(0.) {}

	    bool operator< (const LayerTools &rhs) const { return print_z <  rhs.print_z; }
	    bool operator==(const LayerTools &rhs) const { return print_z == rhs.print_z; }

		coordf_t 					print_z;
		bool 						has_object;
		bool						has_support;
		// Zero based extruder IDs, ordered to minimize tool switches.
		std::vector<unsigned int> 	extruders;
		// Will there be anything extruded on this layer for the wipe tower?
		// Due to the support layers possibly interleaving the object layers,
		// wipe tower will be disabled for some support only layers.
		bool 						has_wipe_tower;
		// Number of wipe tower partitions to support the required number of tool switches
		// and to support the wipe tower partitions above this one.
	    size_t                      wipe_tower_partitions;
	    coordf_t 					wipe_tower_layer_height;


        // This holds list of extrusion that will be used for extruder wiping
        WipingExtrusions wiping_extrusions;

	};

	ToolOrdering() {}

	// For the use case when each object is printed separately
	// (print.config.complete_objects is true).
	ToolOrdering(const PrintObject &object, unsigned int first_extruder = (unsigned int)-1, bool prime_multi_material = false);

	// For the use case when all objects are printed at once.
	// (print.config.complete_objects is false).
	ToolOrdering(const Print &print, unsigned int first_extruder = (unsigned int)-1, bool prime_multi_material = false);

	void 				clear() { m_layer_tools.clear(); }

	// Get the first extruder printing, including the extruder priming areas, returns -1 if there is no layer printed.
	unsigned int   		first_extruder() const { return m_first_printing_extruder; }

	// Get the first extruder printing the layer_tools, returns -1 if there is no layer printed.
	unsigned int   		last_extruder() const { return m_last_printing_extruder; }

	// For a multi-material print, the printing extruders are ordered in the order they shall be primed.
	const std::vector<unsigned int>& all_extruders() const { return m_all_printing_extruders; }

	// Find LayerTools with the closest print_z.
	LayerTools&			tools_for_layer(coordf_t print_z);
	const LayerTools&	tools_for_layer(coordf_t print_z) const 
		{ return *const_cast<const LayerTools*>(&const_cast<const ToolOrdering*>(this)->tools_for_layer(print_z)); }

	const LayerTools&   front()       const { return m_layer_tools.front(); }
	const LayerTools&   back()        const { return m_layer_tools.back(); }
	std::vector<LayerTools>::const_iterator begin() const { return m_layer_tools.begin(); }
	std::vector<LayerTools>::const_iterator end()   const { return m_layer_tools.end(); }
	bool 				empty()       const { return m_layer_tools.empty(); }
	std::vector<LayerTools>& layer_tools() { return m_layer_tools; }
	bool 				has_wipe_tower() const { return ! m_layer_tools.empty() && m_first_printing_extruder != (unsigned int)-1 && m_layer_tools.front().wipe_tower_partitions > 0; }

private:
	void				initialize_layers(std::vector<coordf_t> &zs);
	void 				collect_extruders(const PrintObject &object);
	void				reorder_extruders(unsigned int last_extruder_id);
	void 				fill_wipe_tower_partitions(const PrintConfig &config, coordf_t object_bottom_z);
	void 				collect_extruder_statistics(bool prime_multi_material);

	std::vector<LayerTools> 	m_layer_tools;
	// First printing extruder, including the multi-material priming sequence.
	unsigned int 				m_first_printing_extruder = (unsigned int)-1;
	// Final printing extruder.
	unsigned int 				m_last_printing_extruder  = (unsigned int)-1;
	// All extruders, which extrude some material over m_layer_tools.
    std::vector<unsigned int> 	m_all_printing_extruders;
};

} // namespace SLic3r

#endif /* slic3r_ToolOrdering_hpp_ */
