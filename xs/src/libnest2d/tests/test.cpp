#include <gtest/gtest.h>
#include <fstream>

#include <libnest2d.h>
#include "printer_parts.h"
#include <libnest2d/geometries_io.hpp>
#include <libnest2d/geometries_nfp.hpp>

std::vector<libnest2d::Item>& prusaParts() {
    static std::vector<libnest2d::Item> ret;

    if(ret.empty()) {
        ret.reserve(PRINTER_PART_POLYGONS.size());
        for(auto& inp : PRINTER_PART_POLYGONS) ret.emplace_back(inp);
    }

    return ret;
}

TEST(BasicFunctionality, Angles)
{

    using namespace libnest2d;

    Degrees deg(180);
    Radians rad(deg);
    Degrees deg2(rad);

    ASSERT_DOUBLE_EQ(rad, Pi);
    ASSERT_DOUBLE_EQ(deg, 180);
    ASSERT_DOUBLE_EQ(deg2, 180);
    ASSERT_DOUBLE_EQ(rad, (Radians) deg);
    ASSERT_DOUBLE_EQ( (Degrees) rad, deg);

    ASSERT_TRUE(rad == deg);

    Segment seg = {{0, 0}, {12, -10}};

    ASSERT_TRUE(Degrees(seg.angleToXaxis()) > 270 &&
                Degrees(seg.angleToXaxis()) < 360);

    seg = {{0, 0}, {12, 10}};

    ASSERT_TRUE(Degrees(seg.angleToXaxis()) > 0 &&
                Degrees(seg.angleToXaxis()) < 90);

    seg = {{0, 0}, {-12, 10}};

    ASSERT_TRUE(Degrees(seg.angleToXaxis()) > 90 &&
                Degrees(seg.angleToXaxis()) < 180);

    seg = {{0, 0}, {-12, -10}};

    ASSERT_TRUE(Degrees(seg.angleToXaxis()) > 180 &&
                Degrees(seg.angleToXaxis()) < 270);

    seg = {{0, 0}, {1, 0}};

    ASSERT_DOUBLE_EQ(Degrees(seg.angleToXaxis()), 0);

    seg = {{0, 0}, {0, 1}};

    ASSERT_DOUBLE_EQ(Degrees(seg.angleToXaxis()), 90);


    seg = {{0, 0}, {-1, 0}};

    ASSERT_DOUBLE_EQ(Degrees(seg.angleToXaxis()), 180);


    seg = {{0, 0}, {0, -1}};

    ASSERT_DOUBLE_EQ(Degrees(seg.angleToXaxis()), 270);

}

// Simple test, does not use gmock
TEST(BasicFunctionality, creationAndDestruction)
{
    using namespace libnest2d;

    Item sh = { {0, 0}, {1, 0}, {1, 1}, {0, 1} };

    ASSERT_EQ(sh.vertexCount(), 4u);

    Item sh2 ({ {0, 0}, {1, 0}, {1, 1}, {0, 1} });

    ASSERT_EQ(sh2.vertexCount(), 4u);

    // copy
    Item sh3 = sh2;

    ASSERT_EQ(sh3.vertexCount(), 4u);

    sh2 = {};

    ASSERT_EQ(sh2.vertexCount(), 0u);
    ASSERT_EQ(sh3.vertexCount(), 4u);

}

TEST(GeometryAlgorithms, Distance) {
    using namespace libnest2d;

    Point p1 = {0, 0};

    Point p2 = {10, 0};
    Point p3 = {10, 10};

    ASSERT_DOUBLE_EQ(PointLike::distance(p1, p2), 10);
    ASSERT_DOUBLE_EQ(PointLike::distance(p1, p3), sqrt(200));

    Segment seg(p1, p3);

    ASSERT_DOUBLE_EQ(PointLike::distance(p2, seg), 7.0710678118654755);

    auto result = PointLike::horizontalDistance(p2, seg);

    auto check = [](Coord val, Coord expected) {
        if(std::is_floating_point<Coord>::value)
            ASSERT_DOUBLE_EQ(static_cast<double>(val),
                             static_cast<double>(expected));
        else
            ASSERT_EQ(val, expected);
    };

    ASSERT_TRUE(result.second);
    check(result.first, 10);

    result = PointLike::verticalDistance(p2, seg);
    ASSERT_TRUE(result.second);
    check(result.first, -10);

    result = PointLike::verticalDistance(Point{10, 20}, seg);
    ASSERT_TRUE(result.second);
    check(result.first, 10);


    Point p4 = {80, 0};
    Segment seg2 = { {0, 0}, {0, 40} };

    result = PointLike::horizontalDistance(p4, seg2);

    ASSERT_TRUE(result.second);
    check(result.first, 80);

    result = PointLike::verticalDistance(p4, seg2);
    // Point should not be related to the segment
    ASSERT_FALSE(result.second);

}

TEST(GeometryAlgorithms, Area) {
    using namespace libnest2d;

    Rectangle rect(10, 10);

    ASSERT_EQ(rect.area(), 100);

    Rectangle rect2 = {100, 100};

    ASSERT_EQ(rect2.area(), 10000);

    Item item = {
        {61, 97},
        {70, 151},
        {176, 151},
        {189, 138},
        {189, 59},
        {70, 59},
        {61, 77},
        {61, 97}
    };

    ASSERT_TRUE(ShapeLike::area(item.transformedShape()) > 0 );
}

TEST(GeometryAlgorithms, IsPointInsidePolygon) {
    using namespace libnest2d;

    Rectangle rect(10, 10);

    Point p = {1, 1};

    ASSERT_TRUE(rect.isPointInside(p));

    p = {11, 11};

    ASSERT_FALSE(rect.isPointInside(p));


    p = {11, 12};

    ASSERT_FALSE(rect.isPointInside(p));


    p = {3, 3};

    ASSERT_TRUE(rect.isPointInside(p));

}

//TEST(GeometryAlgorithms, Intersections) {
//    using namespace binpack2d;

//    Rectangle rect(70, 30);

//    rect.translate({80, 60});

//    Rectangle rect2(80, 60);
//    rect2.translate({80, 0});

////    ASSERT_FALSE(Item::intersects(rect, rect2));

//    Segment s1({0, 0}, {10, 10});
//    Segment s2({1, 1}, {11, 11});
//    ASSERT_FALSE(ShapeLike::intersects(s1, s1));
//    ASSERT_FALSE(ShapeLike::intersects(s1, s2));
//}

// Simple test, does not use gmock
TEST(GeometryAlgorithms, LeftAndDownPolygon)
{
    using namespace libnest2d;
    using namespace libnest2d;

    Box bin(100, 100);
    BottomLeftPlacer placer(bin);

    Item item = {{70, 75}, {88, 60}, {65, 50}, {60, 30}, {80, 20}, {42, 20},
                 {35, 35}, {35, 55}, {40, 75}, {70, 75}};

    Item leftControl = { {40, 75},
                         {35, 55},
                         {35, 35},
                         {42, 20},
                         {0,  20},
                         {0,  75},
                         {40, 75}};

    Item downControl = {{88, 60},
                        {88, 0},
                        {35, 0},
                        {35, 35},
                        {42, 20},
                        {80, 20},
                        {60, 30},
                        {65, 50},
                        {88, 60}};

    Item leftp(placer.leftPoly(item));

    ASSERT_TRUE(ShapeLike::isValid(leftp.rawShape()).first);
    ASSERT_EQ(leftp.vertexCount(), leftControl.vertexCount());

    for(unsigned long i = 0; i < leftControl.vertexCount(); i++) {
        ASSERT_EQ(getX(leftp.vertex(i)), getX(leftControl.vertex(i)));
        ASSERT_EQ(getY(leftp.vertex(i)), getY(leftControl.vertex(i)));
    }

    Item downp(placer.downPoly(item));

    ASSERT_TRUE(ShapeLike::isValid(downp.rawShape()).first);
    ASSERT_EQ(downp.vertexCount(), downControl.vertexCount());

    for(unsigned long i = 0; i < downControl.vertexCount(); i++) {
        ASSERT_EQ(getX(downp.vertex(i)), getX(downControl.vertex(i)));
        ASSERT_EQ(getY(downp.vertex(i)), getY(downControl.vertex(i)));
    }
}

// Simple test, does not use gmock
TEST(GeometryAlgorithms, ArrangeRectanglesTight)
{
    using namespace libnest2d;

    std::vector<Rectangle> rects = {
        {80, 80},
        {60, 90},
        {70, 30},
        {80, 60},
        {60, 60},
        {60, 40},
        {40, 40},
        {10, 10},
        {10, 10},
        {10, 10},
        {10, 10},
        {10, 10},
        {5, 5},
        {5, 5},
        {5, 5},
        {5, 5},
        {5, 5},
        {5, 5},
        {5, 5},
        {20, 20} };


    Arranger<BottomLeftPlacer, DJDHeuristic> arrange(Box(210, 250));

    auto groups = arrange(rects.begin(), rects.end());

    ASSERT_EQ(groups.size(), 1u);
    ASSERT_EQ(groups[0].size(), rects.size());

    // check for no intersections, no containment:

    for(auto result : groups) {
        bool valid = true;
        for(Item& r1 : result) {
            for(Item& r2 : result) {
                if(&r1 != &r2 ) {
                    valid = !Item::intersects(r1, r2) || Item::touches(r1, r2);
                    valid = (valid && !r1.isInside(r2) && !r2.isInside(r1));
                    ASSERT_TRUE(valid);
                }
            }
        }
    }

}

TEST(GeometryAlgorithms, ArrangeRectanglesLoose)
{
    using namespace libnest2d;

//    std::vector<Rectangle> rects = { {40, 40}, {10, 10}, {20, 20} };
    std::vector<Rectangle> rects = {
        {80, 80},
        {60, 90},
        {70, 30},
        {80, 60},
        {60, 60},
        {60, 40},
        {40, 40},
        {10, 10},
        {10, 10},
        {10, 10},
        {10, 10},
        {10, 10},
        {5, 5},
        {5, 5},
        {5, 5},
        {5, 5},
        {5, 5},
        {5, 5},
        {5, 5},
        {20, 20} };

    Coord min_obj_distance = 5;

    Arranger<BottomLeftPlacer, DJDHeuristic> arrange(Box(210, 250),
                                                     min_obj_distance);

    auto groups = arrange(rects.begin(), rects.end());

    ASSERT_EQ(groups.size(), 1u);
    ASSERT_EQ(groups[0].size(), rects.size());

    // check for no intersections, no containment:
    auto result = groups[0];
    bool valid = true;
    for(Item& r1 : result) {
        for(Item& r2 : result) {
            if(&r1 != &r2 ) {
                valid = !Item::intersects(r1, r2);
                valid = (valid && !r1.isInside(r2) && !r2.isInside(r1));
                ASSERT_TRUE(valid);
            }
        }
    }

}

namespace {
using namespace libnest2d;

template<unsigned long SCALE = 1, class Bin>
void exportSVG(std::vector<std::reference_wrapper<Item>>& result, const Bin& bin, int idx = 0) {


    std::string loc = "out";

    static std::string svg_header =
R"raw(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<!DOCTYPE svg PUBLIC "-//W3C//DTD SVG 1.0//EN" "http://www.w3.org/TR/2001/REC-SVG-20010904/DTD/svg10.dtd">
<svg height="500" width="500" xmlns="http://www.w3.org/2000/svg" xmlns:svg="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink">
)raw";

    int i = idx;
    auto r = result;
//    for(auto r : result) {
        std::fstream out(loc + std::to_string(i) + ".svg", std::fstream::out);
        if(out.is_open()) {
            out << svg_header;
            Item rbin( Rectangle(bin.width(), bin.height()) );
            for(unsigned i = 0; i < rbin.vertexCount(); i++) {
                auto v = rbin.vertex(i);
                setY(v, -getY(v)/SCALE + 500 );
                setX(v, getX(v)/SCALE);
                rbin.setVertex(i, v);
            }
            out << ShapeLike::serialize<Formats::SVG>(rbin.rawShape()) << std::endl;
            for(Item& sh : r) {
                Item tsh(sh.transformedShape());
                for(unsigned i = 0; i < tsh.vertexCount(); i++) {
                    auto v = tsh.vertex(i);
                    setY(v, -getY(v)/SCALE + 500);
                    setX(v, getX(v)/SCALE);
                    tsh.setVertex(i, v);
                }
                out << ShapeLike::serialize<Formats::SVG>(tsh.rawShape()) << std::endl;
            }
            out << "\n</svg>" << std::endl;
        }
        out.close();

//        i++;
//    }
}
}

TEST(GeometryAlgorithms, BottomLeftStressTest) {
    using namespace libnest2d;

    auto& input = prusaParts();

    Box bin(210, 250);
    BottomLeftPlacer placer(bin);

    auto it = input.begin();
    auto next = it;
    int i = 0;
    while(it != input.end() && ++next != input.end()) {
        placer.pack(*it);
        placer.pack(*next);

        auto result = placer.getItems();
        bool valid = true;

        if(result.size() == 2) {
        Item& r1 = result[0];
        Item& r2 = result[1];
            valid = !Item::intersects(r1, r2) || Item::touches(r1, r2);
            valid = (valid && !r1.isInside(r2) && !r2.isInside(r1));
            if(!valid) {
                std::cout << "error index: " << i << std::endl;
                exportSVG(result, bin, i);
            }
//                    ASSERT_TRUE(valid);
        } else {
            std::cout << "something went terribly wrong!" << std::endl;
        }


        placer.clearItems();
        it++;
        i++;
    }
}

namespace {

struct ItemPair {
    Item orbiter;
    Item stationary;
};

std::vector<ItemPair> nfp_testdata = {
    {
        {
            {80, 50},
            {100, 70},
            {120, 50},
            {80, 50}
        },
        {
            {10, 10},
            {10, 40},
            {40, 40},
            {40, 10},
            {10, 10}
        }
    },
    {
        {
            {80, 50},
            {60, 70},
            {80, 90},
            {120, 90},
            {140, 70},
            {120, 50},
            {80, 50}
        },
        {
            {10, 10},
            {10, 40},
            {40, 40},
            {40, 10},
            {10, 10}
        }
    },
    {
        {
            {40, 10},
            {30, 10},
            {20, 20},
            {20, 30},
            {30, 40},
            {40, 40},
            {50, 30},
            {50, 20},
            {40, 10}
        },
        {
            {80, 0},
            {80, 30},
            {110, 30},
            {110, 0},
            {80, 0}
        }
    },
    {
        {
            {117, 107},
            {118, 109},
            {120, 112},
            {122, 113},
            {128, 113},
            {130, 112},
            {132, 109},
            {133, 107},
            {133, 103},
            {132, 101},
            {130, 98},
            {128, 97},
            {122, 97},
            {120, 98},
            {118, 101},
            {117, 103},
            {117, 107}
        },
        {
            {102, 116},
            {111, 126},
            {114, 126},
            {144, 106},
            {148, 100},
            {148, 85},
            {147, 84},
            {102, 84},
            {102, 116},
        }
    },
    {
        {
            {99, 122},
            {108, 140},
            {110, 142},
            {139, 142},
            {151, 122},
            {151, 102},
            {142, 70},
            {139, 68},
            {111, 68},
            {108, 70},
            {99, 102},
            {99, 122},
        },
        {
            {107, 124},
            {128, 125},
            {133, 125},
            {136, 124},
            {140, 121},
            {142, 119},
            {143, 116},
            {143, 109},
            {141, 93},
            {139, 89},
            {136, 86},
            {134, 85},
            {108, 85},
            {107, 86},
            {107, 124},
        }
    },
    {
        {
            {91, 100},
            {94, 144},
            {117, 153},
            {118, 153},
            {159, 112},
            {159, 110},
            {156, 66},
            {133, 57},
            {132, 57},
            {91, 98},
            {91, 100},
        },
        {
            {101, 90},
            {103, 98},
            {107, 113},
            {114, 125},
            {115, 126},
            {135, 126},
            {136, 125},
            {144, 114},
            {149, 90},
            {149, 89},
            {148, 87},
            {145, 84},
            {105, 84},
            {102, 87},
            {101, 89},
            {101, 90},
        }
    }
};

}

TEST(GeometryAlgorithms, nfpConvexConvex) {
    using namespace libnest2d;

    const Coord SCALE = 1000000;

    Box bin(210*SCALE, 250*SCALE);

    int testcase = 0;

    auto& exportfun = exportSVG<1, Box>;

    auto onetest = [&](Item& orbiter, Item& stationary){
        testcase++;

        orbiter.translate({210*SCALE, 0});

        auto&& nfp = Nfp::noFitPolygon(stationary.rawShape(),
                                       orbiter.transformedShape());

        auto v = ShapeLike::isValid(nfp);

        if(!v.first) {
            std::cout << v.second << std::endl;
        }

        ASSERT_TRUE(v.first);

        Item infp(nfp);

        int i = 0;
        auto rorbiter = orbiter.transformedShape();
        auto vo = Nfp::referenceVertex(rorbiter);

        ASSERT_TRUE(stationary.isInside(infp));

        for(auto v : infp) {
            auto dx = getX(v) - getX(vo);
            auto dy = getY(v) - getY(vo);

            Item tmp = orbiter;

            tmp.translate({dx, dy});

            bool notinside = !tmp.isInside(stationary);
            bool notintersecting = !Item::intersects(tmp, stationary) ||
                                    Item::touches(tmp, stationary);

            if(!(notinside && notintersecting)) {
                std::vector<std::reference_wrapper<Item>> inp = {
                    std::ref(stationary), std::ref(tmp), std::ref(infp)
                };

                exportfun(inp, bin, testcase*i++);
            }

            ASSERT_TRUE(notintersecting);
            ASSERT_TRUE(notinside);
        }
    };

    for(auto& td : nfp_testdata) {
        auto orbiter = td.orbiter;
        auto stationary = td.stationary;
        onetest(orbiter, stationary);
    }

    for(auto& td : nfp_testdata) {
        auto orbiter = td.stationary;
        auto stationary = td.orbiter;
        onetest(orbiter, stationary);
    }
}

TEST(GeometryAlgorithms, pointOnPolygonContour) {
    using namespace libnest2d;

    Rectangle input(10, 10);

    strategies::EdgeCache<PolygonImpl> ecache(input);

    auto first = *input.begin();
    ASSERT_TRUE(getX(first) == getX(ecache.coords(0)));
    ASSERT_TRUE(getY(first) == getY(ecache.coords(0)));

    auto last = *std::prev(input.end());
    ASSERT_TRUE(getX(last) == getX(ecache.coords(1.0)));
    ASSERT_TRUE(getY(last) == getY(ecache.coords(1.0)));

    for(int i = 0; i <= 100; i++) {
        auto v = ecache.coords(i*(0.01));
        ASSERT_TRUE(ShapeLike::touches(v, input.transformedShape()));
    }
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
