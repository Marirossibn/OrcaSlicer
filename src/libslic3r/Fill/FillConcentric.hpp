#ifndef slic3r_FillConcentric_hpp_
#define slic3r_FillConcentric_hpp_

#include "FillBase.hpp"

namespace Slic3r {

class FillConcentric : public Fill
{
public:
    ~FillConcentric() override = default;

protected:
    Fill* clone() const override { return new FillConcentric(*this); };
	void _fill_surface_single(
	    const FillParams                &params, 
	    unsigned int                     thickness_layers,
	    const std::pair<float, Point>   &direction, 
	    ExPolygon     		             expolygon,
	    Polylines                       &polylines_out) override;

	void _fill_surface_single(const FillParams& params,
		unsigned int                   thickness_layers,
		const std::pair<float, Point>& direction,
		ExPolygon                      expolygon,
		ThickPolylines& thick_polylines_out) override;

	bool no_sort() const override { return true; }

	const PrintConfig* print_config = nullptr;
	const PrintObjectConfig* print_object_config = nullptr;

	friend class Layer;
};

} // namespace Slic3r

#endif // slic3r_FillConcentric_hpp_
