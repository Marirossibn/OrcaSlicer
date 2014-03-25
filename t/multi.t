use Test::More tests => 6;
use strict;
use warnings;

BEGIN {
    use FindBin;
    use lib "$FindBin::Bin/../lib";
}

use List::Util qw(first);
use Slic3r;
use Slic3r::Geometry qw(scale convex_hull);
use Slic3r::Test;

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('raft_layers', 2);
    $config->set('infill_extruder', 2);
    $config->set('support_material_extruder', 3);
    $config->set('ooze_prevention', 1);
    $config->set('extruder_offset', [ [0,0], [20,0], [0,20] ]);
    $config->set('temperature', [200, 180, 170]);
    $config->set('first_layer_temperature', [206, 186, 166]);
    $config->set('toolchange_gcode', ';toolchange');  # test that it doesn't crash when this is supplied
    
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    
    my $tool = undef;
    my @tool_temp = (0,0,0);
    my @toolchange_points = ();
    my @extrusion_points = ();
    Slic3r::GCode::Reader->new->parse(Slic3r::Test::gcode($print), sub {
        my ($self, $cmd, $args, $info) = @_;
        
        if ($cmd =~ /^T(\d+)/) {
            if (defined $tool) {
                my $expected_temp = $self->Z == ($config->get_value('first_layer_height') + $config->z_offset)
                    ? $config->first_layer_temperature->[$tool]
                    : $config->temperature->[$tool];
                die 'standby temperature was not set before toolchange'
                    if $tool_temp[$tool] != $expected_temp + $config->standby_temperature_delta;
            }
            $tool = $1;
            push @toolchange_points, Slic3r::Point->new_scale($self->X, $self->Y);
        } elsif ($cmd eq 'M104' || $cmd eq 'M109') {
            my $t = $args->{T} // $tool;
            if ($tool_temp[$t] == 0) {
                fail 'initial temperature is not equal to first layer temperature + standby delta'
                    unless $args->{S} == $config->first_layer_temperature->[$t] + $config->standby_temperature_delta;
            }
            $tool_temp[$t] = $args->{S};
        } elsif ($cmd eq 'G1' && $info->{extruding} && $info->{dist_XY} > 0) {
            push @extrusion_points, my $point = Slic3r::Point->new_scale($args->{X}, $args->{Y});
            $point->translate(map scale($_), @{ $config->extruder_offset->[$tool] });
        }
    });
    my $convex_hull = convex_hull(\@extrusion_points);
    ok !(first { $convex_hull->contains_point($_) } @toolchange_points), 'all toolchanges happen outside skirt';
}

{
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('support_material_extruder', 3);
    
    my $print = Slic3r::Test::init_print('20mm_cube', config => $config);
    ok Slic3r::Test::gcode($print), 'no errors when using non-consecutive extruders';
}

{
    my $model = Slic3r::Model->new;
    my $object = $model->add_object;
    my $lower_config = $model->set_material('lower')->config;
    my $upper_config = $model->set_material('upper')->config;
    $object->add_volume(mesh => Slic3r::Test::mesh('20mm_cube'), material_id => 'lower');
    $object->add_volume(mesh => Slic3r::Test::mesh('20mm_cube', translate => [0,0,20]), material_id => 'upper');
    $object->add_instance(offset => [0,0]);
    
    $lower_config->set('extruder', 1);
    $lower_config->set('bottom_solid_layers', 0);
    $lower_config->set('top_solid_layers', 1);
    $upper_config->set('extruder', 2);
    $upper_config->set('bottom_solid_layers', 1);
    $upper_config->set('top_solid_layers', 0);
    my $config = Slic3r::Config->new_from_defaults;
    $config->set('fill_density', 0);
    $config->set('solid_infill_speed', 99);
    $config->set('top_solid_infill_speed', 99);
    $config->set('cooling', 0);                 # for preventing speeds from being altered
    $config->set('first_layer_speed', '100%');  # for preventing speeds from being altered
    
    my $test = sub {
        my $print = Slic3r::Test::init_print($model, config => $config);
        my $tool = undef;
        my %T0_shells = my %T1_shells = ();  # Z => 1
        Slic3r::GCode::Reader->new->parse(my $gcode = Slic3r::Test::gcode($print), sub {
            my ($self, $cmd, $args, $info) = @_;
        
            if ($cmd =~ /^T(\d+)/) {
                $tool = $1;
            } elsif ($cmd eq 'G1' && $info->{extruding} && $info->{dist_XY} > 0) {
                if (($args->{F} // $self->F) == $config->solid_infill_speed*60) {
                    if ($tool == 0) {
                        $T0_shells{$self->Z} = 1;
                    } elsif ($tool == 1) {
                        $T1_shells{$self->Z} = 1;
                    }
                }
            }
        });
        return [ sort keys %T0_shells ], [ sort keys %T1_shells ];
    };
    
    {
        my ($t0, $t1) = $test->();
        is scalar(@$t0), 0, 'no interface shells';
        is scalar(@$t1), 0, 'no interface shells';
    }
    {
        $config->set('interface_shells', 1);
        my ($t0, $t1) = $test->();
        is scalar(@$t0), $lower_config->top_solid_layers,    'top interface shells';
        is scalar(@$t1), $upper_config->bottom_solid_layers, 'bottom interface shells';
    }
}

__END__
