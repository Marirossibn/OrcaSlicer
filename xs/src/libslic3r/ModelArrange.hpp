#ifndef MODELARRANGE_HPP
#define MODELARRANGE_HPP

#include "Model.hpp"
#include "SVG.hpp"
#include <libnest2d.h>

#include <numeric>
#include <ClipperUtils.hpp>

#include <boost/geometry/index/rtree.hpp>

namespace Slic3r {
namespace arr {

using namespace libnest2d;

std::string toString(const Model& model, bool holes = true) {
    std::stringstream  ss;

    ss << "{\n";

    for(auto objptr : model.objects) {
        if(!objptr) continue;

        auto rmesh = objptr->raw_mesh();

        for(auto objinst : objptr->instances) {
            if(!objinst) continue;

            Slic3r::TriangleMesh tmpmesh = rmesh;
            tmpmesh.scale(objinst->scaling_factor);
            objinst->transform_mesh(&tmpmesh);
            ExPolygons expolys = tmpmesh.horizontal_projection();
            for(auto& expoly_complex : expolys) {

                auto tmp = expoly_complex.simplify(1.0/SCALING_FACTOR);
                if(tmp.empty()) continue;
                auto expoly = tmp.front();
                expoly.contour.make_clockwise();
                for(auto& h : expoly.holes) h.make_counter_clockwise();

                ss << "\t{\n";
                ss << "\t\t{\n";

                for(auto v : expoly.contour.points) ss << "\t\t\t{"
                                                    << v.x << ", "
                                                    << v.y << "},\n";
                {
                    auto v = expoly.contour.points.front();
                    ss << "\t\t\t{" << v.x << ", " << v.y << "},\n";
                }
                ss << "\t\t},\n";

                // Holes:
                ss << "\t\t{\n";
                if(holes) for(auto h : expoly.holes) {
                    ss << "\t\t\t{\n";
                    for(auto v : h.points) ss << "\t\t\t\t{"
                                           << v.x << ", "
                                           << v.y << "},\n";
                    {
                        auto v = h.points.front();
                        ss << "\t\t\t\t{" << v.x << ", " << v.y << "},\n";
                    }
                    ss << "\t\t\t},\n";
                }
                ss << "\t\t},\n";

                ss << "\t},\n";
            }
        }
    }

    ss << "}\n";

    return ss.str();
}

void toSVG(SVG& svg, const Model& model) {
    for(auto objptr : model.objects) {
        if(!objptr) continue;

        auto rmesh = objptr->raw_mesh();

        for(auto objinst : objptr->instances) {
            if(!objinst) continue;

            Slic3r::TriangleMesh tmpmesh = rmesh;
            tmpmesh.scale(objinst->scaling_factor);
            objinst->transform_mesh(&tmpmesh);
            ExPolygons expolys = tmpmesh.horizontal_projection();
            svg.draw(expolys);
        }
    }
}

namespace bgi = boost::geometry::index;

using SpatElement = std::pair<Box, unsigned>;
using SpatIndex = bgi::rtree< SpatElement, bgi::rstar<16, 4> >;
using ItemGroup = std::vector<std::reference_wrapper<Item>>;

std::tuple<double /*score*/, Box /*farthest point from bin center*/>
objfunc(const PointImpl& bincenter,
        double bin_area,
        sl::Shapes<PolygonImpl>& pile,   // The currently arranged pile
        const Item &item,
        double norm,            // A norming factor for physical dimensions
        std::vector<double>& areacache, // pile item areas will be cached
        // a spatial index to quickly get neighbors of the candidate item
        SpatIndex& spatindex,
        const ItemGroup& remaining
        )
{
    using Coord = TCoord<PointImpl>;

    static const double BIG_ITEM_TRESHOLD = 0.02;
    static const double ROUNDNESS_RATIO = 0.5;
    static const double DENSITY_RATIO = 1.0 - ROUNDNESS_RATIO;

    // We will treat big items (compared to the print bed) differently
    auto isBig = [&areacache, bin_area](double a) {
        return a/bin_area > BIG_ITEM_TRESHOLD ;
    };

    // If a new bin has been created:
    if(pile.size() < areacache.size()) {
        areacache.clear();
        spatindex.clear();
    }

    // We must fill the caches:
    int idx = 0;
    for(auto& p : pile) {
        if(idx == areacache.size()) {
            areacache.emplace_back(sl::area(p));
            if(isBig(areacache[idx]))
                spatindex.insert({sl::boundingBox(p), idx});
        }

        idx++;
    }

    // Candidate item bounding box
    auto ibb = item.boundingBox();

    // Calculate the full bounding box of the pile with the candidate item
    pile.emplace_back(item.transformedShape());
    auto fullbb = sl::boundingBox(pile);
    pile.pop_back();

    // The bounding box of the big items (they will accumulate in the center
    // of the pile
    Box bigbb;
    if(spatindex.empty()) bigbb = fullbb;
    else {
        auto boostbb = spatindex.bounds();
        boost::geometry::convert(boostbb, bigbb);
    }

    // Will hold the resulting score
    double score = 0;

    if(isBig(item.area())) {
        // This branch is for the bigger items..

        auto minc = ibb.minCorner(); // bottom left corner
        auto maxc = ibb.maxCorner(); // top right corner

        // top left and bottom right corners
        auto top_left = PointImpl{getX(minc), getY(maxc)};
        auto bottom_right = PointImpl{getX(maxc), getY(minc)};

        // Now the distance of the gravity center will be calculated to the
        // five anchor points and the smallest will be chosen.
        std::array<double, 5> dists;
        auto cc = fullbb.center(); // The gravity center
        dists[0] = pl::distance(minc, cc);
        dists[1] = pl::distance(maxc, cc);
        dists[2] = pl::distance(ibb.center(), cc);
        dists[3] = pl::distance(top_left, cc);
        dists[4] = pl::distance(bottom_right, cc);

        // The smalles distance from the arranged pile center:
        auto dist = *(std::min_element(dists.begin(), dists.end())) / norm;

        // Density is the pack density: how big is the arranged pile
        double density = 0;

        if(remaining.empty()) {
            pile.emplace_back(item.transformedShape());
            auto chull = sl::convexHull(pile);
            pile.pop_back();
            strategies::EdgeCache<PolygonImpl> ec(chull);

            double circ = ec.circumference() / norm;
            double bcirc = 2.0*(fullbb.width() + fullbb.height()) / norm;
            score = 0.5*circ + 0.5*bcirc;

        } else {
            // Prepare a variable for the alignment score.
            // This will indicate: how well is the candidate item aligned with
            // its neighbors. We will check the aligment with all neighbors and
            // return the score for the best alignment. So it is enough for the
            // candidate to be aligned with only one item.
            auto alignment_score = 1.0;

            density = (fullbb.width()*fullbb.height()) / (norm*norm);
            auto& trsh = item.transformedShape();
            auto querybb = item.boundingBox();

            // Query the spatial index for the neigbours
            std::vector<SpatElement> result;
            result.reserve(spatindex.size());
            spatindex.query(bgi::intersects(querybb),
                            std::back_inserter(result));

            for(auto& e : result) { // now get the score for the best alignment
                auto idx = e.second;
                auto& p = pile[idx];
                auto parea = areacache[idx];
                if(std::abs(1.0 - parea/item.area()) < 1e-6) {
                    auto bb = sl::boundingBox(sl::Shapes<PolygonImpl>{p, trsh});
                    auto bbarea = bb.area();
                    auto ascore = 1.0 - (item.area() + parea)/bbarea;

                    if(ascore < alignment_score) alignment_score = ascore;
                }
            }

            // The final mix of the score is the balance between the distance
            // from the full pile center, the pack density and the
            // alignment with the neigbours
            if(result.empty())
                score = 0.5 * dist + 0.5 * density;
            else
                score = 0.45 * dist + 0.45 * density + 0.1 * alignment_score;
        }
    } else if( !isBig(item.area()) && spatindex.empty()) {
        auto bindist = pl::distance(ibb.center(), bincenter) / norm;

        // Bindist is surprisingly enough...
        score = bindist;
    } else {
        // Here there are the small items that should be placed around the
        // already processed bigger items.
        // No need to play around with the anchor points, the center will be
        // just fine for small items
        score = pl::distance(ibb.center(), bigbb.center()) / norm;
    }

    return std::make_tuple(score, fullbb);
}

template<class PConf>
void fillConfig(PConf& pcfg) {

    // Align the arranged pile into the center of the bin
    pcfg.alignment = PConf::Alignment::CENTER;

    // Start placing the items from the center of the print bed
    pcfg.starting_point = PConf::Alignment::CENTER;

    // TODO cannot use rotations until multiple objects of same geometry can
    // handle different rotations
    // arranger.useMinimumBoundigBoxRotation();
    pcfg.rotations = { 0.0 };

    // The accuracy of optimization.
    // Goes from 0.0 to 1.0 and scales performance as well
    pcfg.accuracy = 0.65f;
}

template<class TBin>
class AutoArranger {};

template<class TBin>
class _ArrBase {
protected:
    using Placer = strategies::_NofitPolyPlacer<PolygonImpl, TBin>;
    using Selector = FirstFitSelection;
    using Packer = Nester<Placer, Selector>;
    using PConfig = typename Packer::PlacementConfig;
    using Distance = TCoord<PointImpl>;
    using Pile = sl::Shapes<PolygonImpl>;

    Packer pck_;
    PConfig pconf_; // Placement configuration
    double bin_area_;
    std::vector<double> areacache_;
    SpatIndex rtree_;
    double norm_;
public:

    _ArrBase(const TBin& bin, Distance dist,
             std::function<void(unsigned)> progressind):
       pck_(bin, dist), bin_area_(sl::area(bin)),
       norm_(std::sqrt(sl::area(bin)))
    {
        fillConfig(pconf_);
        pck_.progressIndicator(progressind);
    }

    template<class...Args> inline IndexedPackGroup operator()(Args&&...args) {
        areacache_.clear();
        rtree_.clear();
        return pck_.executeIndexed(std::forward<Args>(args)...);
    }
};

template<>
class AutoArranger<Box>: public _ArrBase<Box> {
public:

    AutoArranger(const Box& bin, Distance dist,
                 std::function<void(unsigned)> progressind):
        _ArrBase<Box>(bin, dist, progressind)
    {
        pconf_.object_function = [this, bin] (
                    Pile& pile,
                    const Item &item,
                    const ItemGroup& rem) {

            auto result = objfunc(bin.center(), bin_area_, pile,
                                  item, norm_, areacache_, rtree_, rem);
            double score = std::get<0>(result);
            auto& fullbb = std::get<1>(result);

            auto wdiff = fullbb.width() - bin.width();
            auto hdiff = fullbb.height() - bin.height();
            if(wdiff > 0) score += std::pow(wdiff, 2) / norm_;
            if(hdiff > 0) score += std::pow(hdiff, 2) / norm_;

            return score;
        };

        pck_.configure(pconf_);
    }
};

using lnCircle = libnest2d::_Circle<libnest2d::PointImpl>;

template<>
class AutoArranger<lnCircle>: public _ArrBase<lnCircle> {
public:

    AutoArranger(const lnCircle& bin, Distance dist,
                 std::function<void(unsigned)> progressind):
        _ArrBase<lnCircle>(bin, dist, progressind) {

        pconf_.object_function = [this, &bin] (
                    Pile& pile,
                    const Item &item,
                    const ItemGroup& rem) {

            auto result = objfunc(bin.center(), bin_area_, pile, item, norm_,
                                  areacache_, rtree_, rem);
            double score = std::get<0>(result);
            auto& fullbb = std::get<1>(result);

            auto d = pl::distance(fullbb.minCorner(),
                                         fullbb.maxCorner());
            auto diff = d - 2*bin.radius();

            if(diff > 0) {
                if( item.area() > 0.01*bin_area_ && item.vertexCount() < 30) {
                    pile.emplace_back(item.transformedShape());
                    auto chull = sl::convexHull(pile);
                    pile.pop_back();

                    auto C = strategies::boundingCircle(chull);
                    auto rdiff = C.radius() - bin.radius();

                    if(rdiff > 0) {
                        score += std::pow(rdiff, 3) / norm_;
                    }
                }
            }

            return score;
        };

        pck_.configure(pconf_);
    }
};

template<>
class AutoArranger<PolygonImpl>: public _ArrBase<PolygonImpl> {
public:
    AutoArranger(const PolygonImpl& bin, Distance dist,
                 std::function<void(unsigned)> progressind):
        _ArrBase<PolygonImpl>(bin, dist, progressind)
    {
        pconf_.object_function = [this, &bin] (
                    Pile& pile,
                    const Item &item,
                    const ItemGroup& rem) {

            auto binbb = sl::boundingBox(bin);
            auto result = objfunc(binbb.center(), bin_area_, pile, item, norm_,
                                  areacache_, rtree_, rem);
            double score = std::get<0>(result);

            return score;
        };

        pck_.configure(pconf_);
    }
};

template<> // Specialization with no bin
class AutoArranger<bool>: public _ArrBase<Box> {
public:

    AutoArranger(Distance dist, std::function<void(unsigned)> progressind):
        _ArrBase<Box>(Box(0, 0), dist, progressind)
    {
        this->pconf_.object_function = [this] (
                    Pile& pile,
                    const Item &item,
                    const ItemGroup& rem) {

            auto result = objfunc({0, 0}, 0, pile, item, norm_,
                                  areacache_, rtree_, rem);
            return std::get<0>(result);
        };

        this->pck_.configure(pconf_);
    }
};

// A container which stores a pointer to the 3D object and its projected
// 2D shape from top view.
using ShapeData2D =
    std::vector<std::pair<Slic3r::ModelInstance*, Item>>;

ShapeData2D projectModelFromTop(const Slic3r::Model &model) {
    ShapeData2D ret;

    auto s = std::accumulate(model.objects.begin(), model.objects.end(), 0,
                    [](size_t s, ModelObject* o){
        return s + o->instances.size();
    });

    ret.reserve(s);

    for(auto objptr : model.objects) {
        if(objptr) {

            auto rmesh = objptr->raw_mesh();

            for(auto objinst : objptr->instances) {
                if(objinst) {
                    Slic3r::TriangleMesh tmpmesh = rmesh;
                    ClipperLib::PolygonImpl pn;

                    tmpmesh.scale(objinst->scaling_factor);

                    // TODO export the exact 2D projection
                    auto p = tmpmesh.convex_hull();

                    p.make_clockwise();
                    p.append(p.first_point());
                    pn.Contour = Slic3rMultiPoint_to_ClipperPath( p );

                    // Efficient conversion to item.
                    Item item(std::move(pn));

                    // Invalid geometries would throw exceptions when arranging
                    if(item.vertexCount() > 3) {
                        item.rotation(objinst->rotation);
                        item.translation( {
                            ClipperLib::cInt(objinst->offset.x/SCALING_FACTOR),
                            ClipperLib::cInt(objinst->offset.y/SCALING_FACTOR)
                        });
                        ret.emplace_back(objinst, item);
                    }
                }
            }
        }
    }

    return ret;
}

class Circle {
    Point center_;
    double radius_;
public:

    inline Circle(): center_(0, 0), radius_(std::nan("")) {}
    inline Circle(const Point& c, double r): center_(c), radius_(r) {}

    inline double radius() const { return radius_; }
    inline const Point& center() const { return center_; }
    inline operator bool() { return !std::isnan(radius_); }
};

enum class BedShapeType {
    BOX,
    CIRCLE,
    IRREGULAR,
    WHO_KNOWS
};

struct BedShapeHint {
    BedShapeType type;
    /*union*/ struct {  // I know but who cares...
        Circle circ;
        BoundingBox box;
        Polyline polygon;
    } shape;
};

BedShapeHint bedShape(const Polyline& bed) {
    static const double E = 10/SCALING_FACTOR;

    BedShapeHint ret;

    auto width = [](const BoundingBox& box) {
        return box.max.x - box.min.x;
    };

    auto height = [](const BoundingBox& box) {
        return box.max.y - box.min.y;
    };

    auto area = [&width, &height](const BoundingBox& box) {
        double w = width(box);
        double h = height(box);
        return w*h;
    };

    auto poly_area = [](Polyline p) {
        Polygon pp; pp.points.reserve(p.points.size() + 1);
        pp.points = std::move(p.points);
        pp.points.emplace_back(pp.points.front());
        return std::abs(pp.area());
    };


    auto bb = bed.bounding_box();

    auto isCircle = [bb](const Polyline& polygon) {
        auto center = bb.center();
        std::vector<double> vertex_distances;
        double avg_dist = 0;
        for (auto pt: polygon.points)
        {
            double distance = center.distance_to(pt);
            vertex_distances.push_back(distance);
            avg_dist += distance;
        }

        avg_dist /= vertex_distances.size();

        Circle ret(center, avg_dist);
        for (auto el: vertex_distances)
        {
            if (abs(el - avg_dist) > 10 * SCALED_EPSILON)
                ret = Circle();
            break;
        }

        return ret;
    };

    auto parea = poly_area(bed);

    if( (1.0 - parea/area(bb)) < 1e-3 ) {
        ret.type = BedShapeType::BOX;
        ret.shape.box = bb;
    }
    else if(auto c = isCircle(bed)) {
        ret.type = BedShapeType::CIRCLE;
        ret.shape.circ = c;
    } else {
        ret.type = BedShapeType::IRREGULAR;
        ret.shape.polygon = bed;
    }

    // Determine the bed shape by hand
    return ret;
}

void applyResult(
        IndexedPackGroup::value_type& group,
        Coord batch_offset,
        ShapeData2D& shapemap)
{
    for(auto& r : group) {
        auto idx = r.first;     // get the original item index
        Item& item = r.second;  // get the item itself

        // Get the model instance from the shapemap using the index
        ModelInstance *inst_ptr = shapemap[idx].first;

        // Get the tranformation data from the item object and scale it
        // appropriately
        auto off = item.translation();
        Radians rot = item.rotation();
        Pointf foff(off.X*SCALING_FACTOR + batch_offset,
                    off.Y*SCALING_FACTOR);

        // write the tranformation data into the model instance
        inst_ptr->rotation = rot;
        inst_ptr->offset = foff;
    }
}


/**
 * \brief Arranges the model objects on the screen.
 *
 * The arrangement considers multiple bins (aka. print beds) for placing all
 * the items provided in the model argument. If the items don't fit on one
 * print bed, the remaining will be placed onto newly created print beds.
 * The first_bin_only parameter, if set to true, disables this behaviour and
 * makes sure that only one print bed is filled and the remaining items will be
 * untouched. When set to false, the items which could not fit onto the
 * print bed will be placed next to the print bed so the user should see a
 * pile of items on the print bed and some other piles outside the print
 * area that can be dragged later onto the print bed as a group.
 *
 * \param model The model object with the 3D content.
 * \param dist The minimum distance which is allowed for any pair of items
 * on the print bed  in any direction.
 * \param bb The bounding box of the print bed. It corresponds to the 'bin'
 * for bin packing.
 * \param first_bin_only This parameter controls whether to place the
 * remaining items which do not fit onto the print area next to the print
 * bed or leave them untouched (let the user arrange them by hand or remove
 * them).
 */
bool arrange(Model &model, coordf_t min_obj_distance,
             const Slic3r::Polyline& bed,
             BedShapeHint bedhint,
             bool first_bin_only,
             std::function<void(unsigned)> progressind)
{
    using ArrangeResult = _IndexedPackGroup<PolygonImpl>;

    bool ret = true;

    // Get the 2D projected shapes with their 3D model instance pointers
    auto shapemap = arr::projectModelFromTop(model);

    // Copy the references for the shapes only as the arranger expects a
    // sequence of objects convertible to Item or ClipperPolygon
    std::vector<std::reference_wrapper<Item>> shapes;
    shapes.reserve(shapemap.size());
    std::for_each(shapemap.begin(), shapemap.end(),
                  [&shapes] (ShapeData2D::value_type& it)
    {
        shapes.push_back(std::ref(it.second));
    });

    IndexedPackGroup result;

    if(bedhint.type == BedShapeType::WHO_KNOWS) bedhint = bedShape(bed);

    BoundingBox bbb(bed);

    auto binbb = Box({
                         static_cast<libnest2d::Coord>(bbb.min.x),
                         static_cast<libnest2d::Coord>(bbb.min.y)
                     },
                     {
                         static_cast<libnest2d::Coord>(bbb.max.x),
                         static_cast<libnest2d::Coord>(bbb.max.y)
                     });

    switch(bedhint.type) {
    case BedShapeType::BOX: {

        // Create the arranger for the box shaped bed
        AutoArranger<Box> arrange(binbb, min_obj_distance, progressind);

        // Arrange and return the items with their respective indices within the
        // input sequence.
        result = arrange(shapes.begin(), shapes.end());
        break;
    }
    case BedShapeType::CIRCLE: {

        auto c = bedhint.shape.circ;
        auto cc = lnCircle({c.center().x, c.center().y} , c.radius());

        AutoArranger<lnCircle> arrange(cc, min_obj_distance, progressind);
        result = arrange(shapes.begin(), shapes.end());
        break;
    }
    case BedShapeType::IRREGULAR:
    case BedShapeType::WHO_KNOWS: {

        using P = libnest2d::PolygonImpl;

        auto ctour = Slic3rMultiPoint_to_ClipperPath(bed);
        P irrbed = sl::create<PolygonImpl>(std::move(ctour));

        AutoArranger<P> arrange(irrbed, min_obj_distance, progressind);

        // Arrange and return the items with their respective indices within the
        // input sequence.
        result = arrange(shapes.begin(), shapes.end());
        break;
    }
    };

    if(result.empty()) return false;

    if(first_bin_only) {
        applyResult(result.front(), 0, shapemap);
    } else {

        const auto STRIDE_PADDING = 1.2;

        Coord stride = static_cast<Coord>(STRIDE_PADDING*
                                          binbb.width()*SCALING_FACTOR);
        Coord batch_offset = 0;

        for(auto& group : result) {
            applyResult(group, batch_offset, shapemap);

            // Only the first pack group can be placed onto the print bed. The
            // other objects which could not fit will be placed next to the
            // print bed
            batch_offset += stride;
        }
    }

    for(auto objptr : model.objects) objptr->invalidate_bounding_box();

    return ret && result.size() == 1;
}

}
}
#endif // MODELARRANGE_HPP
