/*! Computes a point on the near clipping plane for a camera defined using a
	homogeneous transform.
	\param ray_tex_coord The screen-space location of the point in a coordinate
		frame where the left top of the viewport is (0, 0), the right top is
		(1, 0) and the right bottom (1, 1).
	\param proj_to_world_space The projection to world space transform of
		the camera (to be multiplied from the left).
	\return The ray origin on the near clipping plane in world space.*/
vec3 get_camera_ray_origin(vec2 ray_tex_coord, mat4 proj_to_world_space) {
	vec4 pos_0_proj = vec4(2.0 * ray_tex_coord - vec2(1.0), 0.0, 1.0);
	vec4 pos_0_world = proj_to_world_space * pos_0_proj;
	return pos_0_world.xyz / pos_0_world.w;
}


/*! Computes a ray direction for a camera defined using a homogeneous
	transform.
	\param ray_tex_coord The screen-space location of the point in a coordinate
		frame where the left top of the viewport is (0, 0), the right top is
		(1, 0) and the right bottom (1, 1).
	\param world_to_proj_space The world to projection space transform of the
		camera (to be multiplied from the left).
	\return The normalized world-space ray direction for the camera ray.*/
vec3 get_camera_ray_direction(vec2 ray_tex_coord, mat4 world_to_proj_space) {
	vec2 dir_proj = 2.0 * ray_tex_coord - vec2(1.0);
	vec3 ray_dir;
	// This implementation is a bit fancy. The code is automatically generated
	// but the derivation goes as follows: Construct Pluecker coordinates of
	// the ray in projection space, transform those to world space, then take
	// the intersection with the plane at infinity. Of course, the parts that
	// only operate on world_to_proj_space could be pre-computed, but 24 fused
	// multiply-adds is not so bad and I like how broadly applicable this
	// implementation is.
	ray_dir.x =  (world_to_proj_space[1][1] * world_to_proj_space[2][3] - world_to_proj_space[1][3] * world_to_proj_space[2][1]) * dir_proj.x;
	ray_dir.x += (world_to_proj_space[1][3] * world_to_proj_space[2][0] - world_to_proj_space[1][0] * world_to_proj_space[2][3]) * dir_proj.y;
	ray_dir.x +=  world_to_proj_space[1][0] * world_to_proj_space[2][1] - world_to_proj_space[1][1] * world_to_proj_space[2][0];
	ray_dir.y =  (world_to_proj_space[0][3] * world_to_proj_space[2][1] - world_to_proj_space[0][1] * world_to_proj_space[2][3]) * dir_proj.x;
	ray_dir.y += (world_to_proj_space[0][0] * world_to_proj_space[2][3] - world_to_proj_space[0][3] * world_to_proj_space[2][0]) * dir_proj.y;
	ray_dir.y +=  world_to_proj_space[0][1] * world_to_proj_space[2][0] - world_to_proj_space[0][0] * world_to_proj_space[2][1];
	ray_dir.z =  (world_to_proj_space[0][1] * world_to_proj_space[1][3] - world_to_proj_space[0][3] * world_to_proj_space[1][1]) * dir_proj.x;
	ray_dir.z += (world_to_proj_space[0][3] * world_to_proj_space[1][0] - world_to_proj_space[0][0] * world_to_proj_space[1][3]) * dir_proj.y;
	ray_dir.z +=  world_to_proj_space[0][0] * world_to_proj_space[1][1] - world_to_proj_space[0][1] * world_to_proj_space[1][0];
	return normalize(ray_dir);
}