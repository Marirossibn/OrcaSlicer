package Slic3r::Layer;
use strict;
use warnings;

use List::Util qw(first);
use Slic3r::Geometry qw(scale chained_path);
use Slic3r::Geometry::Clipper qw(union_ex intersection_ex);

# the following two were previously generated by Moo
sub print {
    my $self = shift;
    return $self->object->print;
}

sub config {
    my $self = shift;
    return $self->object->config;
}

# the purpose of this method is to be overridden for ::Support layers
sub islands {
    my $self = shift;
    return $self->slices;
}

sub region {
    my $self = shift;
    my ($region_id) = @_;
    
    while ($self->region_count <= $region_id) {
        $self->add_region($self->object->print->get_region($self->region_count));
    }
    
    return $self->get_region($region_id);
}

sub regions {
    my ($self) = @_;
    return [ map $self->get_region($_), 0..($self->region_count-1) ];
}

sub merge_slices {
    my ($self) = @_;
    $_->merge_slices for @{$self->regions};
}

sub make_perimeters {
    my $self = shift;
    Slic3r::debugf "Making perimeters for layer %d\n", $self->id;
    
    # keep track of regions whose perimeters we have already generated
    my %done = ();  # region_id => 1
    
    for my $region_id (0..$#{$self->regions}) {
        next if $done{$region_id};
        my $layerm = $self->regions->[$region_id];
        $done{$region_id} = 1;
        
        # find compatible regions
        my @layerms = ($layerm);
        for my $i (($region_id+1)..$#{$self->regions}) {
            my $config = $self->regions->[$i]->config;
            my $layerm_config = $layerm->config;
            
            if ($config->perimeter_extruder == $layerm_config->perimeter_extruder
                && $config->perimeters == $layerm_config->perimeters
                && $config->perimeter_speed == $layerm_config->perimeter_speed
                && $config->gap_fill_speed == $layerm_config->gap_fill_speed
                && $config->overhangs == $layerm_config->overhangs
                && $config->perimeter_extrusion_width == $layerm_config->perimeter_extrusion_width
                && $config->thin_walls == $layerm_config->thin_walls
                && $config->external_perimeters_first == $layerm_config->external_perimeters_first) {
                push @layerms, $self->regions->[$i];
                $done{$i} = 1;
            }
        }
        
        if (@layerms == 1) {  # optimization
            $layerm->fill_surfaces->clear;
            $layerm->make_perimeters($layerm->slices, $layerm->fill_surfaces);
        } else {
            # group slices (surfaces) according to number of extra perimeters
            my %slices = ();  # extra_perimeters => [ surface, surface... ]
            foreach my $surface (map @{$_->slices}, @layerms) {
                my $extra = $surface->extra_perimeters;
                $slices{$extra} ||= [];
                push @{$slices{$extra}}, $surface;
            }
            
            # merge the surfaces assigned to each group
            my $new_slices = Slic3r::Surface::Collection->new;
            foreach my $surfaces (values %slices) {
                $new_slices->append(Slic3r::Surface->new(
                    surface_type        => $surfaces->[0]->surface_type,
                    extra_perimeters    => $surfaces->[0]->extra_perimeters,
                    expolygon           => $_,
                )) for @{union_ex([ map $_->p, @$surfaces ], 1)};
            }
            
            # make perimeters
            my $fill_surfaces = Slic3r::Surface::Collection->new;
            $layerm->make_perimeters($new_slices, $fill_surfaces);
            
            # assign fill_surfaces to each layer
            if ($fill_surfaces->count > 0) {
                foreach my $lm (@layerms) {
                    my $expolygons = intersection_ex(
                        [ map $_->p, @$fill_surfaces ],
                        [ map $_->p, @{$lm->slices} ],
                    );
                    $lm->fill_surfaces->clear;
                    $lm->fill_surfaces->append(Slic3r::Surface->new(
                        surface_type        => $fill_surfaces->[0]->surface_type,
                        extra_perimeters    => $fill_surfaces->[0]->extra_perimeters,
                        expolygon           => $_,
                    )) for @$expolygons;
                }
            }
        }
    }
}

package Slic3r::Layer::Support;

our @ISA = qw(Slic3r::Layer);

sub islands {
    my $self = shift;
    return [ @{$self->slices}, @{$self->support_islands} ];
}

1;
