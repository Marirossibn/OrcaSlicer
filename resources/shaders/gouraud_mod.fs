#version 110

#define INTENSITY_CORRECTION 0.6

// normalized values for (-0.6/1.31, 0.6/1.31, 1./1.31)
const vec3 LIGHT_TOP_DIR = vec3(-0.4574957, 0.4574957, 0.7624929);
#define LIGHT_TOP_DIFFUSE    (0.8 * INTENSITY_CORRECTION)
#define LIGHT_TOP_SPECULAR   (0.125 * INTENSITY_CORRECTION)
#define LIGHT_TOP_SHININESS  20.0

// normalized values for (1./1.43, 0.2/1.43, 1./1.43)
const vec3 LIGHT_FRONT_DIR = vec3(0.6985074, 0.1397015, 0.6985074);
#define LIGHT_FRONT_DIFFUSE  (0.3 * INTENSITY_CORRECTION)

#define INTENSITY_AMBIENT    0.3

const vec3 ZERO = vec3(0.0, 0.0, 0.0);
const vec3 GREEN = vec3(0.0, 0.7, 0.0);
const vec3 YELLOW = vec3(0.5, 0.7, 0.0);
const vec3 RED = vec3(0.7, 0.0, 0.0);
const vec3 WHITE = vec3(1.0, 1.0, 1.0);
const float EPSILON = 0.0001;
const float BANDS_WIDTH = 10.0;

struct PrintVolumeDetection
{
	// 0 = rectangle, 1 = circle, 2 = custom, 3 = invalid
	int type;
    // type = 0 (rectangle):
    // x = min.x, y = min.y, z = max.x, w = max.y
    // type = 1 (circle):
    // x = center.x, y = center.y, z = radius
	vec4 xy_data;
    // x = min z, y = max z
	vec2 z_data;
};

struct SlopeDetection
{
    bool actived;
	float normal_z;
    mat3 volume_world_normal_matrix;
};

uniform vec4 uniform_color;
uniform SlopeDetection slope;

#ifdef ENABLE_ENVIRONMENT_MAP
    uniform sampler2D environment_tex;
    uniform bool use_environment_tex;
#endif // ENABLE_ENVIRONMENT_MAP

varying vec3 clipping_planes_dots;

// x = diffuse, y = specular;
varying vec2 intensity;

uniform PrintVolumeDetection print_volume;

varying vec4 model_pos;
varying vec4 world_pos;
varying float world_normal_z;
varying vec3 eye_normal;

uniform bool compute_triangle_normals_in_fs;

void main()
{
    if (any(lessThan(clipping_planes_dots, ZERO)))
        discard;
    vec3  color = uniform_color.rgb;
    float alpha = uniform_color.a;

    vec2  intensity_fs      = intensity;
    vec3  eye_normal_fs     = eye_normal;
    float world_normal_z_fs = world_normal_z;
    if (compute_triangle_normals_in_fs) {
        vec3 triangle_normal = normalize(cross(dFdx(model_pos.xyz), dFdy(model_pos.xyz)));
#ifdef FLIP_TRIANGLE_NORMALS
        triangle_normal = -triangle_normal;
#endif

        // First transform the normal into camera space and normalize the result.
        eye_normal_fs = normalize(gl_NormalMatrix * triangle_normal);

        // Compute the cos of the angle between the normal and lights direction. The light is directional so the direction is constant for every vertex.
        // Since these two are normalized the cosine is the dot product. We also need to clamp the result to the [0,1] range.
        float NdotL = max(dot(eye_normal_fs, LIGHT_TOP_DIR), 0.0);

        intensity_fs = vec2(0.0, 0.0);
        intensity_fs.x = INTENSITY_AMBIENT + NdotL * LIGHT_TOP_DIFFUSE;
        vec3 position = (gl_ModelViewMatrix * model_pos).xyz;
        intensity_fs.y = LIGHT_TOP_SPECULAR * pow(max(dot(-normalize(position), reflect(-LIGHT_TOP_DIR, eye_normal_fs)), 0.0), LIGHT_TOP_SHININESS);

        // Perform the same lighting calculation for the 2nd light source (no specular applied).
        NdotL = max(dot(eye_normal_fs, LIGHT_FRONT_DIR), 0.0);
        intensity_fs.x += NdotL * LIGHT_FRONT_DIFFUSE;

        // z component of normal vector in world coordinate used for slope shading
        world_normal_z_fs = slope.actived ? (normalize(slope.volume_world_normal_matrix * triangle_normal)).z : 0.0;
    }

    if (slope.actived && world_normal_z_fs < slope.normal_z - EPSILON) {
        color = vec3(0.7, 0.7, 1.0);
        alpha = 1.0;
    }
	
    // if the fragment is outside the print volume -> use darker color
	vec3 pv_check_min = ZERO;
	vec3 pv_check_max = ZERO;
    if (print_volume.type == 0) {
		// rectangle
		pv_check_min = world_pos.xyz - vec3(print_volume.xy_data.x, print_volume.xy_data.y, print_volume.z_data.x);
		pv_check_max = world_pos.xyz - vec3(print_volume.xy_data.z, print_volume.xy_data.w, print_volume.z_data.y);
	}
	else if (print_volume.type == 1) {
		// circle
		float delta_radius = print_volume.xy_data.z - distance(world_pos.xy, print_volume.xy_data.xy);
		pv_check_min = vec3(delta_radius, 0.0, world_pos.z - print_volume.z_data.x);
		pv_check_max = vec3(0.0, 0.0, world_pos.z - print_volume.z_data.y);
	}	
	color = (any(lessThan(pv_check_min, ZERO)) || any(greaterThan(pv_check_max, ZERO))) ? mix(color, ZERO, 0.3333) : color;
	
#ifdef ENABLE_ENVIRONMENT_MAP
    if (use_environment_tex)
        gl_FragColor = vec4(0.45 * texture2D(environment_tex, normalize(eye_normal_fs).xy * 0.5 + 0.5).xyz + 0.8 * color * intensity_fs.x, alpha);
    else
#endif
        gl_FragColor = vec4(vec3(intensity_fs.y) + color * intensity_fs.x, alpha);
}
