use Test::More;
use strict;
use warnings;

plan tests => 6;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use Slic3r;

{
    my $polygon = Slic3r::Polygon->new([
        [5,0], [10,0], [15,0], [20,0], [20,10], [20,30], [0,0],
    ]);
    
    $polygon->merge_continuous_lines;
    is scalar(@$polygon), 3, 'merge_continuous_lines';
}

{
    my $polyline = Slic3r::Polyline->new([
        [0,0],[1,0],[2,0],[2,1],[2,2],[1,2],[0,2],[0,1],[0,0],
    ]);
    $polyline->simplify(1);
    is_deeply $polyline, [ [0, 0], [2, 0], [2, 2], [0, 2], [0, 0] ], 'Douglas-Peucker';
}

{
    my $polyline = Slic3r::Polyline->new([
        [0,0],[0.5,0.5],[1,0],[1.25,-0.25],[1.5,.5],
    ]);
    $polyline->scale(100);
    $polyline->simplify(25);
    is_deeply $polyline, [ [0, 0], [50, 50], [125, -25], [150, 50] ], 'Douglas-Peucker';
}

{
    my $gear = [
        [144.9694,317.1543], [145.4181,301.5633], [146.3466,296.921], [131.8436,294.1643], [131.7467,294.1464], 
        [121.7238,291.5082], [117.1631,290.2776], [107.9198,308.2068], [100.1735,304.5101], [104.9896,290.3672], 
        [106.6511,286.2133], [93.453,279.2327], [81.0065,271.4171], [67.7886,286.5055], [60.7927,280.1127], 
        [69.3928,268.2566], [72.7271,264.9224], [61.8152,253.9959], [52.2273,242.8494], [47.5799,245.7224], 
        [34.6577,252.6559], [30.3369,245.2236], [42.1712,236.3251], [46.1122,233.9605], [43.2099,228.4876], 
        [35.0862,211.5672], [33.1441,207.0856], [13.3923,212.1895], [10.6572,203.3273], [6.0707,204.8561], 
        [7.2775,204.4259], [29.6713,196.3631], [25.9815,172.1277], [25.4589,167.2745], [19.8337,167.0129], 
        [5.0625,166.3346], [5.0625,156.9425], [5.3701,156.9282], [21.8636,156.1628], [25.3713,156.4613], 
        [25.4243,155.9976], [29.3432,155.8157], [30.3838,149.3549], [26.3596,147.8137], [27.1085,141.2604], 
        [29.8466,126.8337], [24.5841,124.9201], [10.6664,119.8989], [13.4454,110.9264], [33.1886,116.0691], 
        [38.817,103.1819], [45.8311,89.8133], [30.4286,76.81], [35.7686,70.0812], [48.0879,77.6873], 
        [51.564,81.1635], [61.9006,69.1791], [72.3019,58.7916], [60.5509,42.5416], [68.3369,37.1532], 
        [77.9524,48.1338], [80.405,52.2215], [92.5632,44.5992], [93.0123,44.3223], [106.3561,37.2056], 
        [100.8631,17.4679], [108.759,14.3778], [107.3148,11.1283], [117.0002,32.8627], [140.9109,27.3974], 
        [145.7004,26.4994], [145.1346,6.1011], [154.502,5.4063], [156.9398,25.6501], [171.0557,26.2017], 
        [181.3139,27.323], [186.2377,27.8532], [191.6031,8.5474], [200.6724,11.2756], [197.2362,30.2334], 
        [220.0789,39.1906], [224.3261,41.031], [236.3506,24.4291], [243.6897,28.6723], [234.2956,46.7747], 
        [245.6562,55.1643], [257.2523,65.0901], [261.4374,61.5679], [273.1709,52.8031], [278.555,59.5164], 
        [268.4334,69.8001], [264.1615,72.3633], [268.2763,77.9442], [278.8488,93.5305], [281.4596,97.6332], 
        [286.4487,95.5191], [300.2821,90.5903], [303.4456,98.5849], [286.4523,107.7253], [293.7063,131.1779], 
        [294.9748,135.8787], [314.918,133.8172], [315.6941,143.2589], [300.9234,146.1746], [296.6419,147.0309], 
        [297.1839,161.7052], [296.6136,176.3942], [302.1147,177.4857], [316.603,180.3608], [317.1658,176.7341], 
        [315.215,189.6589], [315.1749,189.6548], [294.9411,187.5222], [291.13,201.7233], [286.2615,215.5916], 
        [291.1944,218.2545], [303.9158,225.1271], [299.2384,233.3694], [285.7165,227.6001], [281.7091,225.1956], 
        [273.8981,237.6457], [268.3486,245.2248], [267.4538,246.4414], [264.8496,250.0221], [268.6392,253.896], 
        [278.5017,265.2131], [272.721,271.4403], [257.2776,258.3579], [234.4345,276.5687], [242.6222,294.8315], 
        [234.9061,298.5798], [227.0321,286.2841], [225.2505,281.8301], [211.5387,287.8187], [202.3025,291.0935], 
        [197.307,292.831], [199.808,313.1906], [191.5298,315.0787], [187.3082,299.8172], [186.4201,295.3766], 
        [180.595,296.0487], [161.7854,297.4248], [156.8058,297.6214], [154.3395,317.8592],
    ];
    my $polygon = Slic3r::Polygon->new($gear);
    $polygon->merge_continuous_lines;
    note sprintf "original points: %d\nnew points: %d", scalar(@$gear), scalar(@$polygon);
    ok @$polygon < @$gear, 'gear was simplified using merge_continuous_lines';

    my $num_points = scalar @$polygon;
    $polygon->simplify;
    note sprintf "original points: %d\nnew points: %d", $num_points, scalar(@$polygon);
    ok @$polygon < $num_points, 'gear was further simplified using Douglas-Peucker';
}

{
    my $circle = [
        [3744.8,8045.8],[3788.1,8061.4],[3940.6,8116.3],[3984.8,8129.2],[4140.6,8174.4],[4185.5,8184.4],[4343.8,8219.9],
        [4389.2,8227.1],[4549.4,8252.4],[4595.2,8256.7],[4756.6,8272],[4802.6,8273.4],[4964.7,8278.5],[5010.7,8277.1],
        [5172.8,8272],[5218.6,8267.7],[5380,8252.4],[5425.5,8245.2],[5585.6,8219.9],[5630.5,8209.8],[5788.8,8174.4],
        [5833,8161.6],[5988.8,8116.3],[6032,8100.7],[6184.6,8045.8],[6226.8,8027.5],[6375.6,7963.2],[6416.6,7942.3],
        [6561.1,7868.6],[6600.7,7845.2],[6740.3,7762.7],[6778.4,7736.8],[6912.5,7645.7],[6948.8,7617.5],[7077,7518],
        [7111.5,7487.6],[7233.2,7380.4],[7347.9,7265.7],[7380.4,7233.2],[7410.8,7198.7],[7518,7077],[7546.2,7040.6],
        [7645.7,6912.5],[7671.5,6874.4],[7762.7,6740.3],[7786.1,6700.7],[7868.6,6561.1],[7889.5,6520.2],[7963.2,6375.6],
        [7981.4,6333.4],[8045.8,6184.6],[8061.4,6141.3],[8116.3,5988.8],[8129.2,5944.6],[8174.4,5788.8],[8184.4,5743.9],
        [8219.9,5585.6],[8227.1,5540.2],[8252.4,5380],[8256.7,5334.2],[8272,5172.8],[8273.4,5126.8],[8278.5,4964.7],
        [8277.1,4918.7],[8272,4756.6],[8267.7,4710.8],[8252.4,4549.4],[8245.2,4503.9],[8219.9,4343.8],[8209.8,4298.9],
        [8174.4,4140.6],[8161.6,4096.4],[8116.3,3940.6],[8100.7,3897.4],[8045.8,3744.8],[8027.5,3702.6],[7963.2,3553.8],
        [7942.3,3512.8],[7868.6,3368.3],[7845.2,3328.7],[7762.7,3189.1],[7736.8,3151],[7645.7,3016.9],[7617.5,2980.6],
        [7518,2852.4],[7487.6,2817.9],[7380.4,2696.2],[7347.9,2663.7],[7233.2,2549],[7198.7,2518.6],[7077,2411.4],
        [7040.6,2383.2],[6912.5,2283.7],[6874.4,2257.9],[6740.3,2166.7],[6700.7,2143.3],[6561.1,2060.8],[6520.2,2039.9],
        [6375.6,1966.2],[6333.4,1948],[6184.6,1883.6],[6141.3,1868],[5988.8,1813.1],[5944.6,1800.2],[5788.8,1755],
        [5743.9,1745],[5585.6,1709.5],[5540.2,1702.3],[5380,1677],[5334.2,1672.7],[5172.8,1657.4],[5126.8,1656],
        [4964.7,1650.9],[4918.7,1652.3],[4756.6,1657.4],[4710.8,1661.7],[4549.4,1677],[4503.9,1684.2],[4343.8,1709.5],
        [4298.9,1719.6],[4140.6,1755],[4096.4,1767.8],[3940.6,1813.1],[3897.4,1828.7],[3744.8,1883.6],[3702.6,1901.9],
        [3553.8,1966.2],[3512.8,1987.1],[3368.3,2060.8],[3328.7,2084.2],[3189.1,2166.7],[3151,2192.6],[3016.9,2283.7],
        [2980.6,2311.9],[2852.4,2411.4],[2817.9,2441.8],[2696.2,2549],[2581.5,2663.7],[2549,2696.2],[2518.6,2730.7],
        [2411.4,2852.4],[2383.2,2888.8],[2283.7,3016.9],[2257.9,3055],[2166.7,3189.1],[2143.3,3228.7],[2060.8,3368.3],
        [2039.9,3409.2],[1966.2,3553.8],[1948,3596],[1883.6,3744.8],[1868,3788.1],[1813.1,3940.6],[1800.2,3984.8],
        [1755,4140.6],[1745,4185.5],[1709.5,4343.8],[1702.3,4389.2],[1677,4549.4],[1672.7,4595.2],[1657.4,4756.6],
        [1656,4802.6],[1650.9,4964.7],[1652.3,5010.7],[1657.4,5172.8],[1661.7,5218.6],[1677,5380],[1684.2,5425.5],
        [1709.5,5585.6],[1719.6,5630.5],[1755,5788.8],[1767.8,5833],[1813.1,5988.8],[1828.7,6032],[1883.6,6184.6],
        [1901.9,6226.8],[1966.2,6375.6],[1987.1,6416.6],[2060.8,6561.1],[2084.2,6600.7],[2166.7,6740.3],[2192.6,6778.4],
        [2283.7,6912.5],[2311.9,6948.8],[2411.4,7077],[2441.8,7111.5],[2549,7233.2],[2581.5,7265.7],[2696.2,7380.4],
        [2730.7,7410.8],[2852.4,7518],[2888.8,7546.2],[3016.9,7645.7],[3055,7671.5],[3189.1,7762.7],[3228.7,7786.1],
        [3368.3,7868.6],[3409.2,7889.5],[3553.8,7963.2],[3596,7981.4],
    ];
    
    my $polygon = Slic3r::Polygon->new($circle);
    $polygon->merge_continuous_lines;
    note sprintf "original points: %d\nnew points: %d", scalar(@$circle), scalar(@$polygon);
    ok @$polygon >= @$circle/3, 'circle was simplified using merge_continuous_lines';
}
