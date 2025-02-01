// #include "shading_data.glsl"

#define M_PI 3.141592653589793238462643


//! The Fresnel-Schlick approximation for the Fresnel term
vec3 fresnel_schlick(vec3 f_0, vec3 f_90, float lambert) {
	float flip_1 = 1.0 - lambert;
	float flip_2 = flip_1 * flip_1;
	float flip_5 = flip_2 * flip_1 * flip_2;
	return flip_5 * (f_90 - f_0) + f_0;
}


// /*! Evaluates the Frostbite BRDF as described at the links below for the given
// 	shading point and normalized incoming light direction:
// 	https://dl.acm.org/doi/abs/10.1145/2614028.2615431
// 	https://seblagarde.files.wordpress.com/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf */
// vec3 frostbite_brdf(shading_data_t s, vec3 in_dir) {
// 	// The BRDF is zero in the lower hemisphere
// 	float lambert_in = dot(s.normal, in_dir);
// 	if (min(lambert_in, s.lambert_out) < 0.0)
// 		return vec3(0.0);
// 	// It uses the half-vector between incoming and outgoing light direction
// 	vec3 half_dir = normalize(in_dir + s.out_dir);
// 	float half_dot_out = dot(half_dir, s.out_dir);
// 	// This is the Disney diffuse BRDF
// 	float f_90 = (half_dot_out * half_dot_out) * (2.0 * s.roughness) + 0.5;
// 	float fresnel_diffuse =
// 		fresnel_schlick(vec3(1.0), vec3(f_90), s.lambert_out).x *
// 		fresnel_schlick(vec3(1.0), vec3(f_90), lambert_in).x;
// 	vec3 brdf = fresnel_diffuse * s.diffuse_albedo;
// 	// The Frostbite specular BRDF uses the GGX normal distribution function...
// 	float half_dot_normal = dot(half_dir, s.normal);
// 	float roughness_2 = s.roughness * s.roughness;
// 	float ggx = (roughness_2 * half_dot_normal - half_dot_normal) * half_dot_normal + 1.0;
// 	ggx = roughness_2 / (ggx * ggx);
// 	// ... Smith masking-shadowing...
// 	float masking = lambert_in * sqrt((s.lambert_out - roughness_2 * s.lambert_out) * s.lambert_out + roughness_2);
// 	float shadowing = s.lambert_out * sqrt((lambert_in - roughness_2 * lambert_in) * lambert_in + roughness_2);
// 	float smith = 0.5 / (masking + shadowing);
// 	// ... and the Fresnel-Schlick approximation
// 	vec3 fresnel = fresnel_schlick(s.fresnel_0, vec3(1.0), max(0.0, half_dot_out));
// 	brdf += ggx * smith * fresnel;
// 	return brdf * (1.0 / M_PI);
// }


/*! Samples the distribution of visible normals in the GGX normal distribution
	function for the given outgoing direction. All vectors must be in a
	coordinate frame where the shading normal is (0.0, 0.0, 1.0).
	\param out_dir Direction towards the camera along the path. Need not be
		normalized.
	\param roughness The roughness parameter of GGX for the local x- and
		y-axis.
	\param randoms A point distributed uniformly in [0,1)^2.
	\return The normalized half vector (*not* the incoming direction).*/
vec3 sample_ggx_vndf(vec3 out_dir, vec2 roughness, vec2 randoms) {
	// This implementation is based on:
	// https://gist.github.com/jdupuy/4c6e782b62c92b9cb3d13fbb0a5bd7a0
	// It is described in detail here:
	// https://doi.org/10.1111/cgf.14867

	// Warp to the hemisphere configuration
	vec3 out_dir_std = normalize(vec3(out_dir.xy * roughness, out_dir.z));
	// Sample a spherical cap in (-out_dir_std.z, 1]
	float azimuth = (2.0 * M_PI) * randoms[0] - M_PI;
	float z = 1.0 - randoms[1] * (1.0 + out_dir_std.z);
	float sine = sqrt(max(0.0, 1.0 - z * z));
	vec3 cap = vec3(sine * cos(azimuth), sine * sin(azimuth), z);
	// Compute the half vector in the hemisphere configuration
	vec3 half_dir_std = cap + out_dir_std;
	// Warp back to the ellipsoid configuration
	return normalize(vec3(half_dir_std.xy * roughness, half_dir_std.z));
}


/*! Computes the density that is sampled by sample_ggx_vndf().
	\param lambert_out Dot product between the shading normal and the outgoing
		light direction.
	\param half_dot_normal Dot product between the sampled half vector and
		the shading normal.
	\param half_dot_out Dot product between the sampled half vector and the
		outgoing light direction.
	\param roughness The GGX roughness parameter (along both x- and y-axis).
	\return The density w.r.t. solid angle for the sampled half vector (*not*
		for the incoming direction).*/
float get_ggx_vndf_density(float lambert_out, float half_dot_normal, float half_dot_out, float roughness) {
	// Based on Equation 2 in this paper: https://doi.org/10.1111/cgf.14867
	// A few factors have been cancelled to optimize evaluation.
	if (half_dot_normal < 0.0)
		return 0.0;
	float roughness_2 = roughness * roughness;
	float flip_roughness_2 = 1.0 - roughness * roughness;
	float length_M_inv_out_2 = roughness_2 + flip_roughness_2 * lambert_out * lambert_out;
	float D_vis_std = max(0.0, half_dot_out) * (2.0 / M_PI) / (lambert_out + sqrt(length_M_inv_out_2));
	float length_M_half_2 = 1.0 - flip_roughness_2 * half_dot_normal * half_dot_normal;
	return D_vis_std * roughness_2 / (length_M_half_2 * length_M_half_2);
}


//! Forwards to sample_ggx_vndf() and constructs a local reflection vector from
//! the result which can be used as incoming light direction.
vec3 sample_ggx_in_dir(vec3 out_dir, float roughness, vec2 randoms) {
	vec3 half_dir = sample_ggx_vndf(out_dir, vec2(roughness), randoms);
	return -reflect(out_dir, half_dir);
}


//! Returns the density w.r.t. solid angle that is sampled by
//! sample_ggx_in_dir(). Most parameters come from shading_data_t.
float get_ggx_in_dir_density(float lambert_out, vec3 out_dir, vec3 in_dir, vec3 normal, float roughness) {
	vec3 half_dir = normalize(in_dir + out_dir);
	float half_dot_out = dot(half_dir, out_dir);
	float half_dot_normal = dot(half_dir, normal);
	// Compute the density of the half vector
	float density = get_ggx_vndf_density(lambert_out, half_dot_normal, half_dot_out, roughness);
	// Account for the mirroring in the density
	density /= 4.0 * half_dot_out;
	return density;
}


//! Constructs a special orthogonal matrix where the given normalized normal
//! vector is the third column
mat3 get_shading_space(vec3 n) {
	// This implementation is from: https://www.jcgt.org/published/0006/01/01/
	float s = (n.z > 0.0) ? 1.0 : -1.0;
	float a = -1.0 / (s + n.z);
	float b = n.x * n.y * a;
	vec3 b1 = vec3(1.0 + s * n.x * n.x * a, s * b, -s * n.x);
	vec3 b2 = vec3(b, s + n.y * n.y * a, -n.y);
	return mat3(b1, b2, n);
}


//! Produces a sample distributed uniformly with respect to projected solid
//! angle (PSA) in the upper hemisphere (positive z).
vec3 sample_hemisphere_psa(vec2 randoms) {
	// Sample a disk uniformly
	float azimuth = (2.0 * M_PI) * randoms[0] - M_PI;
	float radius = sqrt(randoms[1]);
	// Project to the hemisphere
	float z = sqrt(1.0 - radius * radius);
	return vec3(radius * cos(azimuth), radius * sin(azimuth), z);
}


//! Returns the density w.r.t. solid angle sampled by sample_hemisphere_psa().
//! It only needs the z-coordinate of the sampled direction (in shading space).
float get_hemisphere_psa_density(float sampled_dir_z) {
	return (1.0 / M_PI) * max(0.0, sampled_dir_z);
}


// //! Heuristically computes a probability with which projected solid angle
// //! sampling should be used when sampling the BRDF for the given shading point.
// float get_diffuse_sampling_probability(shading_data_t s) {
// 	// In principle we use the luminance of the diffuse albedo. But in specular
// 	// highlights, the specular component is much more important than the
// 	// diffuse component. Thus, we always sample it at least 50% of the time in
// 	// the spirit of defensive sampling.
// 	return min(0.5, dot(s.diffuse_albedo, vec3(0.2126, 0.7152, 0.0722)));
// }


// /*! Samples an incoming light direction using sampling strategies that provide
// 	good importance sampling for the Frostbite BRDF times cosine (namely
// 	projected solid angle sampling and GGX VNDF sampling with probabilities as
// 	indicated by get_diffuse_sampling_probability()).
// 	\param s Complete shading data.
// 	\param randoms A uniformly distributed point in [0,1)^2.
// 	\return A normalized direction vector for the sampled direction.*/
// vec3 sample_frostbite_brdf(shading_data_t s, vec2 randoms) {
// 	mat3 shading_to_world_space = get_shading_space(s.normal);
// 	// Decide stochastically whether to sample the specular or the diffuse
// 	// component
// 	float diffuse_prob = get_diffuse_sampling_probability(s);
// 	bool diffuse = randoms[0] < diffuse_prob;
// 	// Sample the direction and determine the density according to a
// 	// single-sample MIS estimate with the balance heuristic
// 	float density;
// 	vec3 sampled_dir;
// 	if (diffuse) {
// 		// Reuse the random number
// 		randoms[0] /= diffuse_prob;
// 		// Sample the projected solid angle uniformly
// 		sampled_dir = shading_to_world_space * sample_hemisphere_psa(randoms);
// 	}
// 	else {
// 		// Reuse the random number
// 		randoms[0] = (randoms[0] - diffuse_prob) / (1.0 - diffuse_prob);
// 		// Sample the half-vector from the GGX VNDF
// 		vec3 local_out_dir = transpose(shading_to_world_space) * s.out_dir;
// 		vec3 local_in_dir = sample_ggx_in_dir(local_out_dir, s.roughness, randoms);
// 		sampled_dir = shading_to_world_space * local_in_dir;
// 	}
// 	return sampled_dir;
// }


// //! Returns the density w.r.t. solid angle sampled by sample_frostbite_brdf().
// float get_frostbite_brdf_density(shading_data_t s, vec3 sampled_dir) {
// 	float diffuse_prob = get_diffuse_sampling_probability(s);
// 	float specular_density = get_ggx_in_dir_density(s.lambert_out, s.out_dir, sampled_dir, s.normal, s.roughness);
// 	float diffuse_density = get_hemisphere_psa_density(dot(s.normal, sampled_dir));
// 	return mix(specular_density, diffuse_density, diffuse_prob);
// }