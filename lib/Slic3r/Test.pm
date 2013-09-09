package Slic3r::Test;
use strict;
use warnings;

require Exporter;
our @ISA = qw(Exporter);
our @EXPORT_OK = qw(_eq);

use IO::Scalar;
use List::Util qw(first);
use Slic3r::Geometry qw(epsilon X Y Z);

my %cuboids = (
    '20mm_cube' => [20,20,20],
    '2x20x10'   => [2, 20,10],
);

sub model {
    my ($model_name, %params) = @_;
    
    my ($vertices, $facets);
    if ($cuboids{$model_name}) {
        my ($x, $y, $z) = @{ $cuboids{$model_name} };
        $vertices = [
            [$x,$y,0], [$x,0,0], [0,0,0], [0,$y,0], [$x,$y,$z], [0,$y,$z], [0,0,$z], [$x,0,$z],
        ];
        $facets = [
            [0,1,2], [0,2,3], [4,5,6], [4,6,7], [0,4,7], [0,7,1], [1,7,6], [1,6,2], [2,6,5], [2,5,3], [4,0,3], [4,3,5],
        ],
    } elsif ($model_name eq 'cube_with_hole') {
        $vertices = [
            [0,0,0],[0,0,10],[0,20,0],[0,20,10],[20,0,0],[20,0,10],[5,5,0],[15,5,0],[5,15,0],[20,20,0],[15,15,0],[20,20,10],[5,5,10],[5,15,10],[15,5,10],[15,15,10]
        ];
        $facets = [
            [0,1,2],[2,1,3],[1,0,4],[5,1,4],[6,7,4],[8,2,9],[0,2,8],[10,8,9],[0,8,6],[0,6,4],[4,7,9],[7,10,9],[2,3,9],[9,3,11],[12,1,5],[13,3,12],[14,12,5],[3,1,12],[11,3,13],[11,15,5],[11,13,15],[15,14,5],[5,4,9],[11,5,9],[8,13,12],[6,8,12],[10,15,13],[8,10,13],[15,10,14],[14,10,7],[14,7,12],[12,7,6]
        ],
    } elsif ($model_name eq 'V') {
        $vertices = [
            [-14,0,20],[-14,15,20],[0,0,0],[0,15,0],[-4,0,20],[-4,15,20],[5,0,7.14286],[10,0,0],[24,0,20],[14,0,20],[10,15,0],[5,15,7.14286],[14,15,20],[24,15,20]
        ];
        $facets = [
            [0,1,2],[2,1,3],[1,0,4],[5,1,4],[4,0,2],[6,4,2],[7,6,2],[8,9,7],[9,6,7],[2,3,7],[7,3,10],[1,5,3],[3,5,11],[11,12,13],[11,13,3],[3,13,10],[5,4,6],[11,5,6],[6,9,11],[11,9,12],[12,9,8],[13,12,8],[8,7,10],[13,8,10]
        ],
    } elsif ($model_name eq 'L') {
        $vertices = [
            [0,10,0],[0,10,10],[0,20,0],[0,20,10],[10,10,0],[10,10,10],[20,20,0],[20,0,0],[10,0,0],[20,20,10],[10,0,10],[20,0,10]
        ];
        $facets = [
            [0,1,2],[2,1,3],[4,5,1],[0,4,1],[0,2,4],[4,2,6],[4,6,7],[4,7,8],[2,3,6],[6,3,9],[3,1,5],[9,3,5],[10,11,5],[11,9,5],[5,4,10],[10,4,8],[10,8,7],[11,10,7],[11,7,6],[9,11,6]
        ],
    } elsif ($model_name eq 'overhang') {
        $vertices = [
            [1364.68505859375,614.398010253906,20.002498626709],[1389.68505859375,614.398010253906,20.002498626709],[1377.18505859375,589.398986816406,20.002498626709],[1389.68505859375,589.398986816406,20.002498626709],[1389.68505859375,564.398986816406,20.0014991760254],[1364.68505859375,589.398986816406,20.002498626709],[1364.68505859375,564.398986816406,20.0014991760254],[1360.93505859375,589.398986816406,17.0014991760254],[1360.93505859375,585.64697265625,17.0014991760254],[1357.18505859375,564.398986816406,17.0014991760254],[1364.68505859375,589.398986816406,17.0014991760254],[1364.68505859375,571.899963378906,17.0014991760254],[1364.68505859375,564.398986816406,17.0014991760254],[1348.43603515625,564.398986816406,17.0014991760254],[1352.80908203125,589.398986816406,17.0014991760254],[1357.18408203125,589.398986816406,17.0014991760254],[1357.18310546875,614.398010253906,17.0014991760254],[1364.68505859375,606.89599609375,17.0014991760254],[1364.68505859375,614.398010253906,17.0014991760254],[1352.18603515625,564.398986816406,20.0014991760254],[1363.65405273438,589.398986816406,23.3004989624023],[1359.46704101562,589.398986816406,23.3004989624023],[1358.37109375,564.398986816406,23.3004989624023],[1385.56103515625,564.398986816406,23.3004989624023],[1373.06311035156,589.398986816406,23.3004989624023],[1368.80810546875,564.398986816406,23.3004989624023],[1387.623046875,589.398986816406,23.3004989624023],[1387.623046875,585.276000976562,23.3004989624023],[1389.68505859375,589.398986816406,23.3004989624023],[1389.68505859375,572.64599609375,23.3004989624023],[1389.68505859375,564.398986816406,23.3004989624023],[1367.77709960938,589.398986816406,23.3004989624023],[1366.7470703125,564.398986816406,23.3004989624023],[1354.31201171875,589.398986816406,23.3004989624023],[1352.18603515625,564.398986816406,23.3004989624023],[1389.68505859375,614.398010253906,23.3004989624023],[1377.31701660156,614.398010253906,23.3004989624023],[1381.43908691406,589.398986816406,23.3004989624023],[1368.80700683594,614.398010253906,23.3004989624023],[1368.80810546875,589.398986816406,23.3004989624023],[1356.43908691406,614.398010253906,23.3004989624023],[1357.40502929688,589.398986816406,23.3004989624023],[1360.56201171875,614.398010253906,23.3004989624023],[1348.705078125,614.398010253906,23.3004989624023],[1350.44506835938,589.398986816406,23.3004989624023],[1389.68505859375,606.153015136719,23.3004989624023],[1347.35205078125,589.398986816406,23.3004989624023],[1346.56005859375,589.398986816406,23.3004989624023],[1346.56005859375,594.159912109375,17.0014991760254],[1346.56005859375,589.398986816406,17.0014991760254],[1346.56005859375,605.250427246094,23.3004989624023],[1346.56005859375,614.398010253906,23.3004989624023],[1346.56005859375,614.398010253906,20.8258285522461],[1346.56005859375,614.398010253906,17.0014991760254],[1346.56005859375,564.398986816406,19.10133934021],[1346.56005859375,567.548583984375,23.3004989624023],[1346.56005859375,564.398986816406,17.0020332336426],[1346.56005859375,564.398986816406,23.0018501281738],[1346.56005859375,564.398986816406,23.3004989624023],[1346.56005859375,575.118957519531,17.0014991760254],[1346.56005859375,574.754028320312,23.3004989624023]
        ];
        $facets = [
            [0,1,2],[2,3,4],[2,5,0],[4,6,2],[2,6,5],[2,1,3],[7,8,9],[10,9,8],[11,9,10],[12,9,11],[9,13,14],[7,15,16],[10,17,0],[10,0,5],[12,11,6],[18,16,0],[6,19,13],[6,13,9],[9,12,6],[17,18,0],[11,10,5],[11,5,6],[14,16,15],[17,7,18],[16,18,7],[14,15,9],[7,9,15],[7,17,8],[10,8,17],[20,21,22],[23,24,25],[26,23,27],[28,27,23],[29,28,23],[30,29,23],[25,31,32],[22,33,34],[35,36,37],[24,38,39],[21,40,41],[38,42,20],[33,43,44],[6,4,23],[6,23,25],[36,35,1],[1,0,38],[1,38,36],[29,30,4],[25,32,6],[40,42,0],[35,45,1],[4,3,28],[4,28,29],[3,1,45],[3,45,28],[22,34,19],[19,6,32],[19,32,22],[42,38,0],[30,23,4],[0,16,43],[0,43,40],[24,37,36],[38,24,36],[24,23,37],[37,23,26],[22,32,20],[20,32,31],[33,41,40],[43,33,40],[45,35,26],[37,26,35],[33,44,34],[44,43,46],[20,42,21],[40,21,42],[31,39,38],[20,31,38],[33,22,41],[21,41,22],[31,25,39],[24,39,25],[26,27,45],[28,45,27],[47,48,49],[47,50,48],[51,48,50],[52,48,51],[53,48,52],[54,55,56],[57,55,54],[58,55,57],[49,59,47],[60,56,55],[59,56,60],[60,47,59],[48,53,16],[56,13,19],[54,56,19],[56,59,13],[59,49,14],[59,14,13],[49,48,16],[49,16,14],[44,46,60],[44,60,55],[51,50,43],[19,34,58],[19,58,57],[53,52,16],[43,16,52],[43,52,51],[57,54,19],[47,60,46],[55,58,34],[55,34,44],[50,47,46],[50,46,43]
        ],
    } elsif ($model_name eq '40x10') {
        $vertices = [
            [12.8680295944214,29.5799007415771,12],[11.7364797592163,29.8480796813965,12],[11.1571502685547,29.5300102233887,12],[10.5814504623413,29.9830799102783,12],[10,29.6000003814697,12],[9.41855144500732,29.9830799102783,12],[8.84284687042236,29.5300102233887,12],[8.26351833343506,29.8480796813965,12],[7.70256900787354,29.3210391998291,12],[7.13196802139282,29.5799007415771,12],[6.59579277038574,28.9761600494385,12],[6.03920221328735,29.1821594238281,12],[5.53865718841553,28.5003795623779,12],[5,28.6602592468262,12],[4.54657793045044,27.9006500244141,12],[4.02841377258301,28.0212306976318,12],[3.63402199745178,27.1856994628906,12],[3.13758301734924,27.2737407684326,12],[2.81429696083069,26.3659801483154,12],[2.33955597877502,26.4278793334961,12],[2.0993549823761,25.4534206390381,12],[1.64512205123901,25.4950904846191,12],[1.49962198734283,24.4613399505615,12],[1.0636739730835,24.4879894256592,12],[1.02384400367737,23.4042091369629,12],[0.603073298931122,23.4202003479004,12],[0.678958415985107,22.2974300384521,12],[0.269550800323486,22.3061599731445,12],[0.469994693994522,21.1571502685547,12],[0.067615881562233,21.1609306335449,12],[0.399999290704727,20,12],[0,20,12],[0.399999290704727,5,12],[0,5,12],[0.456633001565933,4.2804012298584,12],[0.0615576282143593,4.21782684326172,12],[0.625140011310577,3.5785219669342,12],[0.244717106223106,3.45491504669189,12],[0.901369392871857,2.91164398193359,12],[0.544967114925385,2.73004698753357,12],[1.27852201461792,2.29618692398071,12],[0.954914808273315,2.06107401847839,12],[1.74730801582336,1.74730801582336,12],[1.46446597576141,1.46446597576141,12],[2.29618692398071,1.27852201461792,12],[2.06107401847839,0.954914808273315,12],[2.91164398193359,0.901369392871857,12],[2.73004698753357,0.544967114925385,12],[3.5785219669342,0.625140011310577,12],[3.45491504669189,0.244717106223106,12],[4.2804012298584,0.456633001565933,12],[4.21782684326172,0.0615576282143593,12],[5,0.399999290704727,12],[5,0,12],[19.6000003814697,0.399999290704727,12],[20,0,12],[19.6000003814697,20,12],[20,20,12],[19.5300102233887,21.1571502685547,12],[19.9323806762695,21.1609306335449,12],[19.3210391998291,22.2974300384521,12],[19.7304496765137,22.3061599731445,12],[18.9761600494385,23.4042091369629,12],[19.3969306945801,23.4202003479004,12],[18.5003795623779,24.4613399505615,12],[18.9363307952881,24.4879894256592,12],[17.9006500244141,25.4534206390381,12],[18.3548793792725,25.4950904846191,12],[17.1856994628906,26.3659801483154,12],[17.6604404449463,26.4278793334961,12],[16.3659801483154,27.1856994628906,12],[16.862419128418,27.2737407684326,12],[15.4534196853638,27.9006500244141,12],[15.9715900421143,28.0212306976318,12],[14.4613399505615,28.5003795623779,12],[15,28.6602592468262,12],[13.4042100906372,28.9761600494385,12],[13.9608001708984,29.1821594238281,12],[12.2974300384521,29.3210391998291,12],[7.13196802139282,29.5799007415771,0],[8.26351833343506,29.8480796813965,0],[8.84284687042236,29.5300102233887,0],[9.41855144500732,29.9830799102783,0],[10,29.6000003814697,0],[10.5814504623413,29.9830799102783,0],[11.1571502685547,29.5300102233887,0],[11.7364797592163,29.8480796813965,0],[12.2974300384521,29.3210391998291,0],[12.8680295944214,29.5799007415771,0],[13.4042100906372,28.9761600494385,0],[13.9608001708984,29.1821594238281,0],[14.4613399505615,28.5003795623779,0],[15,28.6602592468262,0],[15.4534196853638,27.9006500244141,0],[15.9715900421143,28.0212306976318,0],[16.3659801483154,27.1856994628906,0],[16.862419128418,27.2737407684326,0],[17.1856994628906,26.3659801483154,0],[17.6604404449463,26.4278793334961,0],[17.9006500244141,25.4534206390381,0],[18.3548793792725,25.4950904846191,0],[18.5003795623779,24.4613399505615,0],[18.9363307952881,24.4879894256592,0],[18.9761600494385,23.4042091369629,0],[19.3969306945801,23.4202003479004,0],[19.3210391998291,22.2974300384521,0],[19.7304496765137,22.3061599731445,0],[19.5300102233887,21.1571502685547,0],[19.9323806762695,21.1609306335449,0],[19.6000003814697,20,0],[20,20,0],[19.6000003814697,0.399999290704727,0],[20,0,0],[5,0.399999290704727,0],[5,0,0],[4.2804012298584,0.456633001565933,0],[4.21782684326172,0.0615576282143593,0],[3.5785219669342,0.625140011310577,0],[3.45491504669189,0.244717106223106,0],[2.91164398193359,0.901369392871857,0],[2.73004698753357,0.544967114925385,0],[2.29618692398071,1.27852201461792,0],[2.06107401847839,0.954914808273315,0],[1.74730801582336,1.74730801582336,0],[1.46446597576141,1.46446597576141,0],[1.27852201461792,2.29618692398071,0],[0.954914808273315,2.06107401847839,0],[0.901369392871857,2.91164398193359,0],[0.544967114925385,2.73004698753357,0],[0.625140011310577,3.5785219669342,0],[0.244717106223106,3.45491504669189,0],[0.456633001565933,4.2804012298584,0],[0.0615576282143593,4.21782684326172,0],[0.399999290704727,5,0],[0,5,0],[0.399999290704727,20,0],[0,20,0],[0.469994693994522,21.1571502685547,0],[0.067615881562233,21.1609306335449,0],[0.678958415985107,22.2974300384521,0],[0.269550800323486,22.3061599731445,0],[1.02384400367737,23.4042091369629,0],[0.603073298931122,23.4202003479004,0],[1.49962198734283,24.4613399505615,0],[1.0636739730835,24.4879894256592,0],[2.0993549823761,25.4534206390381,0],[1.64512205123901,25.4950904846191,0],[2.81429696083069,26.3659801483154,0],[2.33955597877502,26.4278793334961,0],[3.63402199745178,27.1856994628906,0],[3.13758301734924,27.2737407684326,0],[4.54657793045044,27.9006500244141,0],[4.02841377258301,28.0212306976318,0],[5.53865718841553,28.5003795623779,0],[5,28.6602592468262,0],[6.59579277038574,28.9761600494385,0],[6.03920221328735,29.1821594238281,0],[7.70256900787354,29.3210391998291,0]
            ];
        $facets = [
            [0,1,2],[2,1,3],[2,3,4],[4,3,5],[4,5,6],[6,5,7],[6,7,8],[8,7,9],[8,9,10],[10,9,11],[10,11,12],[12,11,13],[12,13,14],[14,13,15],[14,15,16],[16,15,17],[16,17,18],[18,17,19],[18,19,20],[20,19,21],[20,21,22],[22,21,23],[22,23,24],[24,23,25],[24,25,26],[26,25,27],[26,27,28],[28,27,29],[28,29,30],[30,29,31],[30,31,32],[32,31,33],[32,33,34],[34,33,35],[34,35,36],[36,35,37],[36,37,38],[38,37,39],[38,39,40],[40,39,41],[40,41,42],[42,41,43],[42,43,44],[44,43,45],[44,45,46],[46,45,47],[46,47,48],[48,47,49],[48,49,50],[50,49,51],[50,51,52],[52,51,53],[52,53,54],[54,53,55],[54,55,56],[56,55,57],[56,57,58],[58,57,59],[58,59,60],[60,59,61],[60,61,62],[62,61,63],[62,63,64],[64,63,65],[64,65,66],[66,65,67],[66,67,68],[68,67,69],[68,69,70],[70,69,71],[70,71,72],[72,71,73],[72,73,74],[74,73,75],[74,75,76],[76,75,77],[76,77,78],[78,77,0],[78,0,2],[79,80,81],[81,80,82],[81,82,83],[83,82,84],[83,84,85],[85,84,86],[85,86,87],[87,86,88],[87,88,89],[89,88,90],[89,90,91],[91,90,92],[91,92,93],[93,92,94],[93,94,95],[95,94,96],[95,96,97],[97,96,98],[97,98,99],[99,98,100],[99,100,101],[101,100,102],[101,102,103],[103,102,104],[103,104,105],[105,104,106],[105,106,107],[107,106,108],[107,108,109],[109,108,110],[109,110,111],[111,110,112],[111,112,113],[113,112,114],[113,114,115],[115,114,116],[115,116,117],[117,116,118],[117,118,119],[119,118,120],[119,120,121],[121,120,122],[121,122,123],[123,122,124],[123,124,125],[125,124,126],[125,126,127],[127,126,128],[127,128,129],[129,128,130],[129,130,131],[131,130,132],[131,132,133],[133,132,134],[133,134,135],[135,134,136],[135,136,137],[137,136,138],[137,138,139],[139,138,140],[139,140,141],[141,140,142],[141,142,143],[143,142,144],[143,144,145],[145,144,146],[145,146,147],[147,146,148],[147,148,149],[149,148,150],[149,150,151],[151,150,152],[151,152,153],[153,152,154],[153,154,155],[155,154,156],[155,156,157],[157,156,79],[157,79,81],[57,110,108],[57,108,59],[59,108,106],[59,106,61],[61,106,104],[61,104,63],[63,104,102],[63,102,65],[65,102,100],[65,100,67],[67,100,98],[67,98,69],[69,98,96],[69,96,71],[71,96,94],[71,94,73],[73,94,92],[73,92,75],[75,92,90],[75,90,77],[77,90,88],[77,88,0],[0,88,86],[0,86,1],[1,86,84],[1,84,3],[3,84,82],[3,82,5],[5,82,80],[5,80,7],[7,80,79],[7,79,9],[9,79,156],[9,156,11],[11,156,154],[11,154,13],[13,154,152],[13,152,15],[15,152,150],[15,150,17],[17,150,148],[17,148,19],[19,148,146],[19,146,21],[21,146,144],[21,144,23],[23,144,142],[23,142,25],[25,142,140],[25,140,27],[27,140,138],[27,138,29],[29,138,136],[29,136,31],[33,31,134],[134,31,136],[33,134,132],[33,132,35],[35,132,130],[35,130,37],[37,130,128],[37,128,39],[39,128,126],[39,126,41],[41,126,124],[41,124,43],[43,124,122],[43,122,45],[45,122,120],[45,120,47],[47,120,118],[47,118,49],[49,118,116],[49,116,51],[51,116,114],[51,114,53],[55,53,112],[112,53,114],[57,55,110],[110,55,112],[30,135,137],[30,137,28],[28,137,139],[28,139,26],[26,139,141],[26,141,24],[24,141,143],[24,143,22],[22,143,145],[22,145,20],[20,145,147],[20,147,18],[18,147,149],[18,149,16],[16,149,151],[16,151,14],[14,151,153],[14,153,12],[12,153,155],[12,155,10],[10,155,157],[10,157,8],[8,157,81],[8,81,6],[6,81,83],[6,83,4],[4,83,85],[4,85,2],[2,85,87],[2,87,78],[78,87,89],[78,89,76],[76,89,91],[76,91,74],[74,91,93],[74,93,72],[72,93,95],[72,95,70],[70,95,97],[70,97,68],[68,97,99],[68,99,66],[66,99,101],[66,101,64],[64,101,103],[64,103,62],[62,103,105],[62,105,60],[60,105,107],[60,107,58],[58,107,109],[58,109,56],[30,32,135],[135,32,133],[52,113,115],[52,115,50],[50,115,117],[50,117,48],[48,117,119],[48,119,46],[46,119,121],[46,121,44],[44,121,123],[44,123,42],[42,123,125],[42,125,40],[40,125,127],[40,127,38],[38,127,129],[38,129,36],[36,129,131],[36,131,34],[34,131,133],[34,133,32],[52,54,113],[113,54,111],[54,56,111],[111,56,109]
        ],
    } else {
        return undef;
    }
    
    my $mesh = Slic3r::TriangleMesh->new;
    $mesh->ReadFromperl($vertices, $facets);
    $mesh->scale_xyz($params{scale_xyz}) if $params{scale_xyz};
    $mesh->scale($params{scale}) if $params{scale};
    
    my $model = Slic3r::Model->new;
    my $object = $model->add_object(vertices => $mesh->vertices);
    $object->add_volume(facets => $mesh->facets);
    $object->add_instance(
        offset      => [0,0],
        rotation    => $params{rotation} // 0,
    );
    return $model;
}

sub init_print {
    my ($model_name, %params) = @_;
    
    my $config = Slic3r::Config->new_from_defaults;
    $config->apply($params{config}) if $params{config};
    $config->set('gcode_comments', 1) if $ENV{SLIC3R_TESTS_GCODE};
    
    my $print = Slic3r::Print->new(config => $config);
    
    $model_name = [$model_name] if ref($model_name) ne 'ARRAY';
    $print->add_model(model($_, %params)) for @$model_name;
    $print->validate;
    
    return $print;
}

sub gcode {
    my ($print) = @_;
    
    my $fh = IO::Scalar->new(\my $gcode);
    $print->export_gcode(output_fh => $fh, quiet => 1);
    $fh->close;
    
    return $gcode;
}

sub _eq {
    my ($a, $b) = @_;
    return abs($a - $b) < epsilon;
}

sub add_facet {
    my ($facet, $vertices, $facets) = @_;
    
    push @$facets, [];
    for my $i (0..2) {
        my $v = first { $vertices->[$_][X] == $facet->[$i][X] && $vertices->[$_][Y] == $facet->[$i][Y] && $vertices->[$_][Z] == $facet->[$i][Z] } 0..$#$vertices;
        if (!defined $v) {
            push @$vertices, [ @{$facet->[$i]}[X,Y,Z] ];
            $v = $#$vertices;
        }
        $facets->[-1][$i] = $v;
    }
}

1;
