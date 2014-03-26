use Test::More tests => 3;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use List::Util qw(first);
use Slic3r;
use Slic3r::Geometry qw(epsilon unscale X Y);
use Slic3r::Test;

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('print_center', [100,100]);
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    my @extrusion_points = ();
    Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
        my ($self, $cmd, $args, $info) = @_;
        
        if ($cmd eq 'G1' && $info->{extruding} && $info->{dist_XY} > 0) {
            push @extrusion_points, my $point = Slic3r::Point->new_scale($args->{X}, $args->{Y});
        }
    });
    my $bb = Slic3r::Geometry::BoundingBox->new_from_points(\@extrusion_points);
    my $center = $bb->center;
    ok abs(unscale($center->[X]) - $config->print_center->[X]) < epsilon, 'print is centered around print_center (X)';
    ok abs(unscale($center->[Y]) - $config->print_center->[Y]) < epsilon, 'print is centered around print_center (Y)';
}

{
    # this represents the aggregate config from presets
    my $config = Slic3r::Config->new_from_defaults;
    
    # user adds one object to the plater
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    
    # user sets a per-object option
    $print->objects->[0]->config->set('fill_density', 100);
    $print->reload_object(0);
    
    # user exports G-code, thus the default config is reapplied
    $print->apply_config($config);
    
    is $print->objects->[0]->config->fill_density, 100, 'apply_config() does not override per-object settings';
}

__END__
