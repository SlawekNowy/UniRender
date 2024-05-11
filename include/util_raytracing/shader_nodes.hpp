/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2023 Silverlan
*/

#ifndef __PR_CYCLES_NODES_HPP__
#define __PR_CYCLES_NODES_HPP__

#include "definitions.hpp"
#include "data_value.hpp"
#include "exception.hpp"
#include <sharedutils/util_hash.hpp>
#include <mathutil/color.h>
#include <optional>
#include <functional>

namespace unirender {
	enum class ColorSpace : uint8_t {
		Srgb = 0,
		Raw,
		Auto,

		Count
	};

	enum class EnvironmentProjection : uint8_t {
		Equirectangular = 0,
		MirrorBall,

		Count
	};

	enum class ClosureType : uint32_t {
		None = 0,
		BsdfMicroFacetMultiGgxGlass,
		BsdfDiffuseToon,
		BsdfMicroFacetGgxGlass,

		Count
	};

	class Shader;
	class NodeDesc;
	class GroupNodeDesc;
	struct MathNode;
	namespace nodes::math {
		enum class MathType : uint32_t;
	};
	namespace nodes::vector_math {
		enum class MathType : uint32_t;
	};
	struct DLLRTUTIL Socket {
		Socket() = default;
		Socket(const DataValue &value);
		Socket(float value);
		Socket(const Vector3 &value);
		Socket(NodeDesc &node, const std::string &socketName, bool output);
		Socket(const Socket &other) = default;
		Socket &operator=(const Socket &other) = default;
		Socket &operator=(Socket &&other) = default;

		bool operator==(const Socket &other) const;
		bool operator!=(const Socket &other) const;

		std::string ToString() const;
		void Link(const Socket &other);

		bool IsValid() const;
		bool IsConcreteValue() const;
		bool IsNodeSocket() const;
		bool IsOutputSocket() const;
		SocketType GetType() const;

		NodeDesc *GetNode(std::string &outSocketName) const;
		NodeDesc *GetNode() const;
		std::optional<DataValue> GetValue() const;

		void Serialize(DataStream &dsOut, const std::unordered_map<const NodeDesc *, uint64_t> &nodeIndexTable) const;
		void Deserialize(GroupNodeDesc &parentGroupNode, DataStream &dsIn, const std::vector<const NodeDesc *> &nodeIndexTable);

		Socket operator-() const;
		Socket operator+(float f) const;
		Socket operator-(float f) const;
		Socket operator*(float f) const;
		Socket operator/(float f) const;
		Socket operator%(float f) const;
		Socket operator^(float f) const;
		Socket operator<(float f) const;
		Socket operator<=(float f) const;
		Socket operator>(float f) const;
		Socket operator>=(float f) const;
		Socket operator+(const Socket &socket) const;
		Socket operator-(const Socket &socket) const;
		Socket operator*(const Socket &socket) const;
		Socket operator/(const Socket &socket) const;
		Socket operator%(const Socket &socket) const;
		Socket operator^(const Socket &socket) const;
		Socket operator<(const Socket &socket) const;
		Socket operator<=(const Socket &socket) const;
		Socket operator>(const Socket &socket) const;
		Socket operator>=(const Socket &socket) const;
	  private:
		Socket ApplyOperator(const Socket &other, nodes::math::MathType opType, std::optional<nodes::vector_math::MathType> opTypeVec, float (*applyValue)(float, float)) const;
		Socket ApplyComparisonOperator(const Socket &other, bool (*op)(float, float), Socket (*opNode)(GroupNodeDesc &, const Socket &, const Socket &)) const;
		unirender::GroupNodeDesc *GetCommonGroupNode(const Socket &other) const;
		// Socket can either be a concrete value (e.g. float), OR an input or output of a node
		std::optional<DataValue> m_value {};
		struct {
			std::weak_ptr<NodeDesc> node {};
			std::string socketName;
			bool output = false;
		} m_nodeSocketRef;
	};

	struct DLLRTUTIL SocketHasher {
		std::size_t operator()(const Socket &k) const;
	};

	namespace nodes {
		namespace math {
			constexpr auto *IN_TYPE = "type";
			constexpr auto *IN_USE_CLAMP = "use_clamp";
			constexpr auto *IN_VALUE1 = "value1";
			constexpr auto *IN_VALUE2 = "value2";
			constexpr auto *IN_VALUE3 = "value3";

			constexpr auto *OUT_VALUE = "value";

			enum class MathType : uint32_t {
				Add = 0u,
				Subtract,
				Multiply,
				Divide,
				Sine,
				Cosine,
				Tangent,
				ArcSine,
				ArcCosine,
				ArcTangent,
				Power,
				Logarithm,
				Minimum,
				Maximum,
				Round,
				LessThan,
				GreaterThan,
				Modulo,
				Absolute,
				ArcTan2,
				Floor,
				Ceil,
				Fraction,
				Sqrt,
				InvSqrt,
				Sign,
				Exponent,
				Radians,
				Degrees,
				SinH,
				CosH,
				TanH,
				Trunc,
				Snap,
				Wrap,
				Compare,
				MultiplyAdd,
				PingPong,
				SmoothMin,
				SmoothMax,

				Count
			};
		};
		namespace hsv {
			constexpr auto *IN_HUE = "hue";
			constexpr auto *IN_SATURATION = "saturation";
			constexpr auto *IN_VALUE = "value";
			constexpr auto *IN_FAC = "fac";
			constexpr auto *IN_COLOR = "color";

			constexpr auto *OUT_COLOR = "color";
		};
		namespace separate_xyz {
			constexpr auto *IN_VECTOR = "vector";

			constexpr auto *OUT_X = "x";
			constexpr auto *OUT_Y = "y";
			constexpr auto *OUT_Z = "z";
		};
		namespace combine_xyz {
			constexpr auto *IN_X = "x";
			constexpr auto *IN_Y = "Y";
			constexpr auto *IN_Z = "Z";

			constexpr auto *OUT_VECTOR = "vector";
		};
		namespace separate_rgb {
			constexpr auto *IN_COLOR = "color";

			constexpr auto *OUT_R = "r";
			constexpr auto *OUT_G = "g";
			constexpr auto *OUT_B = "b";
		};
		namespace combine_rgb {
			constexpr auto *IN_R = "r";
			constexpr auto *IN_G = "g";
			constexpr auto *IN_B = "b";

			constexpr auto *OUT_IMAGE = "image";
		};
		namespace geometry {
			constexpr auto *OUT_POSITION = "position";
			constexpr auto *OUT_NORMAL = "normal";
			constexpr auto *OUT_TANGENT = "tangent";
			constexpr auto *OUT_TRUE_NORMAL = "true_normal";
			constexpr auto *OUT_INCOMING = "incoming";
			constexpr auto *OUT_PARAMETRIC = "parametric";
			constexpr auto *OUT_BACKFACING = "backfacing";
			constexpr auto *OUT_POINTINESS = "pointiness";
			constexpr auto *OUT_RANDOM_PER_ISLAND = "random_per_island";
		};
		namespace camera_info {
			constexpr auto *OUT_VIEW_VECTOR = "view_vector";
			constexpr auto *OUT_VIEW_Z_DEPTH = "view_z_depth";
			constexpr auto *OUT_VIEW_DISTANCE = "view_distance";
		};
		namespace image_texture {
			// Note: These have to match ccl::u_colorspace_raw and ccl::u_colorspace_srgb
			constexpr auto *COLOR_SPACE_RAW = "__builtin_raw";
			constexpr auto *COLOR_SPACE_SRGB = "__builtin_srgb";

			constexpr auto *IN_FILENAME = "filename";
			constexpr auto *IN_COLORSPACE = "colorspace";
			constexpr auto *IN_ALPHA_TYPE = "alpha_type";
			constexpr auto *IN_INTERPOLATION = "interpolation";
			constexpr auto *IN_EXTENSION = "extension";
			constexpr auto *IN_PROJECTION = "projection";
			constexpr auto *IN_PROJECTION_BLEND = "projection_blend";
			constexpr auto *IN_VECTOR = "vector";

			constexpr auto *OUT_COLOR = "color";
			constexpr auto *OUT_ALPHA = "alpha";

			enum class AlphaType : uint32_t {
				Unassociated = 0,
				Associated,
				ChannelPacked,
				Ignore,
				Auto,

				Count
			};

			enum class InterpolationType : uint32_t {
				Linear = 0,
				Closest,
				Cubic,
				Smart,

				Count
			};

			enum class ExtensionType : uint32_t {
				Repeat = 0,
				Extend,
				Clip,

				Count
			};

			enum class Projection : uint32_t {
				Flat = 0,
				Box,
				Sphere,
				Tube,

				Count
			};
		};
		namespace normal_texture {
			constexpr auto *IN_FILENAME = "filename";
			constexpr auto *IN_STRENGTH = "strength";

			constexpr auto *OUT_NORMAL = "normal";
		};
		namespace environment_texture {
			constexpr auto *IN_FILENAME = "filename";
			constexpr auto *IN_COLORSPACE = "colorspace";
			constexpr auto *IN_ALPHA_TYPE = "alpha_type";
			constexpr auto *IN_INTERPOLATION = "interpolation";
			constexpr auto *IN_PROJECTION = "projection";
			constexpr auto *IN_VECTOR = "vector";

			constexpr auto *OUT_COLOR = "color";
			constexpr auto *OUT_ALPHA = "alpha";
		};
		namespace noise_texture {
			constexpr auto *IN_VECTOR = "vector";
			constexpr auto *IN_W = "w";
			constexpr auto *IN_SCALE = "scale";
			constexpr auto *IN_DETAIL = "detail";
			constexpr auto *IN_ROUGHNESS = "roughness";
			constexpr auto *IN_DISTORTION = "distortion";

			constexpr auto *OUT_FAC = "fac";
			constexpr auto *OUT_COLOR = "color";
		};
		namespace mix_closure {
			constexpr auto *IN_FAC = "fac";
			constexpr auto *IN_CLOSURE1 = "closure1";
			constexpr auto *IN_CLOSURE2 = "closure2";

			constexpr auto *OUT_CLOSURE = "closure";
		};
		namespace add_closure {
			constexpr auto *IN_CLOSURE1 = "closure1";
			constexpr auto *IN_CLOSURE2 = "closure2";

			constexpr auto *OUT_CLOSURE = "closure";
		};
		namespace background_shader {
			constexpr auto *IN_COLOR = "color";
			constexpr auto *IN_STRENGTH = "strength";
			constexpr auto *IN_SURFACE_MIX_WEIGHT = "surface_mix_weight";

			constexpr auto *OUT_BACKGROUND = "background";
		};
		namespace texture_coordinate {
			constexpr auto *IN_FROM_DUPLI = "from_dupli";
			constexpr auto *IN_USE_TRANSFORM = "use_transform";
			constexpr auto *IN_OB_TFM = "ob_tfm";

			constexpr auto *OUT_GENERATED = "generated";
			constexpr auto *OUT_NORMAL = "normal";
			constexpr auto *OUT_UV = "UV";
			constexpr auto *OUT_OBJECT = "object";
			constexpr auto *OUT_CAMERA = "camera";
			constexpr auto *OUT_WINDOW = "window";
			constexpr auto *OUT_REFLECTION = "reflection";
		};
		namespace uvmap {
			constexpr auto *OUT_UV = "UV";
		};
		namespace mapping {
			constexpr auto *IN_TYPE = "type";
			constexpr auto *IN_VECTOR = "vector";
			constexpr auto *IN_LOCATION = "location";
			constexpr auto *IN_ROTATION = "rotation";
			constexpr auto *IN_SCALE = "scale";

			constexpr auto *OUT_VECTOR = "vector";

			enum class Type : uint32_t {
				Point = 0,
				Texture,
				Vector,
				Normal,

				Count
			};
		};
		namespace scatter_volume {
			constexpr auto *IN_COLOR = "color";
			constexpr auto *IN_DENSITY = "density";
			constexpr auto *IN_ANISOTROPY = "anisotropy";
			constexpr auto *IN_VOLUME_MIX_WEIGHT = "volume_mix_weight";

			constexpr auto *OUT_VOLUME = "volume";
		};
		namespace emission {
			constexpr auto *IN_COLOR = "color";
			constexpr auto *IN_STRENGTH = "strength";
			constexpr auto *IN_SURFACE_MIX_WEIGHT = "surface_mix_weight";

			constexpr auto *OUT_EMISSION = "emission";
		};
		namespace color {
			constexpr auto *IN_VALUE = "value";

			constexpr auto *OUT_COLOR = "color";
		};
		namespace attribute {
			constexpr auto *IN_ATTRIBUTE = "attribute";

			constexpr auto *OUT_COLOR = "color";
			constexpr auto *OUT_VECTOR = "vector";
			constexpr auto *OUT_FAC = "fac";
		};
		namespace light_path {
			constexpr auto *OUT_IS_CAMERA_RAY = "is_camera_ray";
			constexpr auto *OUT_IS_SHADOW_RAY = "is_shadow_ray";
			constexpr auto *OUT_IS_DIFFUSE_RAY = "is_diffuse_ray";
			constexpr auto *OUT_IS_GLOSSY_RAY = "is_glossy_ray";
			constexpr auto *OUT_IS_SINGULAR_RAY = "is_singular_ray";
			constexpr auto *OUT_IS_REFLECTION_RAY = "is_reflection_ray";
			constexpr auto *OUT_IS_TRANSMISSION_RAY = "is_transmission_ray";
			constexpr auto *OUT_IS_VOLUME_SCATTER_RAY = "is_volume_scatter_ray";

			constexpr auto *OUT_RAY_LENGTH = "ray_length";
			constexpr auto *OUT_RAY_DEPTH = "ray_depth";
			constexpr auto *OUT_DIFFUSE_DEPTH = "diffuse_depth";
			constexpr auto *OUT_GLOSSY_DEPTH = "glossy_depth";
			constexpr auto *OUT_TRANSPARENT_DEPTH = "transparent_depth";
			constexpr auto *OUT_TRANSMISSION_DEPTH = "transmission_depth";
		};
		namespace transparent_bsdf {
			constexpr auto *IN_COLOR = "color";
			constexpr auto *IN_SURFACE_MIX_WEIGHT = "surface_mix_weight";

			constexpr auto *OUT_BSDF = "BSDF";
		};
		namespace translucent_bsdf {
			constexpr auto *IN_COLOR = "color";
			constexpr auto *IN_NORMAL = "normal";
			constexpr auto *IN_SURFACE_MIX_WEIGHT = "surface_mix_weight";

			constexpr auto *OUT_BSDF = "BSDF";
		};
		namespace diffuse_bsdf {
			constexpr auto *IN_COLOR = "color";
			constexpr auto *IN_NORMAL = "normal";
			constexpr auto *IN_SURFACE_MIX_WEIGHT = "surface_mix_weight";
			constexpr auto *IN_ROUGHNESS = "roughness";

			constexpr auto *OUT_BSDF = "BSDF";
		};
		namespace normal_map {
			constexpr auto *IN_SPACE = "space";
			constexpr auto *IN_ATTRIBUTE = "attribute";
			constexpr auto *IN_STRENGTH = "strength";
			constexpr auto *IN_COLOR = "color";

			constexpr auto *OUT_NORMAL = "normal";

			enum class Space : uint32_t {
				Tangent = 0,
				Object,
				World,

				Count
			};
		};
		namespace principled_bsdf {
			constexpr auto *IN_DISTRIBUTION = "distribution";
			constexpr auto *IN_SUBSURFACE_METHOD = "subsurface_method";
			constexpr auto *IN_BASE_COLOR = "base_color";
			constexpr auto *IN_SUBSURFACE_COLOR = "subsurface_color";
			constexpr auto *IN_METALLIC = "metallic";
			constexpr auto *IN_SUBSURFACE = "subsurface";
			constexpr auto *IN_SUBSURFACE_RADIUS = "subsurface_radius";
			constexpr auto *IN_SPECULAR = "specular";
			constexpr auto *IN_ROUGHNESS = "roughness";
			constexpr auto *IN_SPECULAR_TINT = "specular_tint";
			constexpr auto *IN_ANISOTROPIC = "anisotropic";
			constexpr auto *IN_SHEEN = "sheen";
			constexpr auto *IN_SHEEN_TINT = "sheen_tint";
			constexpr auto *IN_CLEARCOAT = "clearcoat";
			constexpr auto *IN_CLEARCOAT_ROUGHNESS = "clearcoat_roughness";
			constexpr auto *IN_IOR = "ior";
			constexpr auto *IN_TRANSMISSION = "transmission";
			constexpr auto *IN_TRANSMISSION_ROUGHNESS = "transmission_roughness";
			constexpr auto *IN_ANISOTROPIC_ROTATION = "anisotropic_rotation";
			constexpr auto *IN_EMISSION = "emission";
			constexpr auto *IN_ALPHA = "alpha";
			constexpr auto *IN_NORMAL = "normal";
			constexpr auto *IN_CLEARCOAT_NORMAL = "clearcoat_normal";
			constexpr auto *IN_TANGENT = "tangent";
			constexpr auto *IN_SURFACE_MIX_WEIGHT = "surface_mix_weight";

			constexpr auto *OUT_BSDF = "BSDF";
		};
		namespace principled_volume {
			constexpr auto *IN_COLOR = "color";
			constexpr auto *IN_DENSITY = "density";
			constexpr auto *IN_ANISOTROPY = "anisotropy";
			constexpr auto *IN_ABSORPTION_COLOR = "absorption_color";
			constexpr auto *IN_EMISSION_STRENGTH = "emission_strength";
			constexpr auto *IN_EMISSION_COLOR = "emission_color";
			constexpr auto *IN_BLACKBODY_INTENSITY = "blackbody_intensity";
			constexpr auto *IN_BLACKBODY_TINT = "blackbody_tint";
			constexpr auto *IN_TEMPERATURE = "temperature";
			constexpr auto *IN_VOLUME_MIX_WEIGHT = "volume_mix_weight";

			constexpr auto *OUT_VOLUME = "volume";
		};
		namespace toon_bsdf {
			constexpr auto *IN_COMPONENT = "component";
			constexpr auto *IN_COLOR = "color";
			constexpr auto *IN_NORMAL = "normal";
			constexpr auto *IN_SURFACE_MIX_WEIGHT = "surface_mix_weight";
			constexpr auto *IN_SIZE = "size";
			constexpr auto *IN_SMOOTH = "smooth";

			constexpr auto *OUT_BSDF = "BSDF";
		};
		namespace glossy_bsdf {
			constexpr auto *IN_COLOR = "color";
			constexpr auto *IN_ALPHA = "alpha";
			constexpr auto *IN_NORMAL = "normal";
			constexpr auto *IN_SURFACE_MIX_WEIGHT = "surface_mix_weight";
			constexpr auto *IN_DISTRIBUTION = "distribution";
			constexpr auto *IN_ROUGHNESS = "roughness";

			constexpr auto *OUT_BSDF = "BSDF";
		};
		namespace glass_bsdf {
			constexpr auto *IN_DISTRIBUTION = "distribution";
			constexpr auto *IN_COLOR = "color";
			constexpr auto *IN_NORMAL = "normal";
			constexpr auto *IN_SURFACE_MIX_WEIGHT = "surface_mix_weight";
			constexpr auto *IN_ROUGHNESS = "roughness";
			constexpr auto *IN_IOR = "IOR";

			constexpr auto *OUT_BSDF = "BSDF";
		};
		namespace volume_clear {
			constexpr auto *IN_PRIORITY = "priority";
			constexpr auto *IN_IOR = "IOR";
			constexpr auto *IN_ABSORPTION = "absorption";
			constexpr auto *IN_EMISSION = "emission";

			constexpr auto *IN_DEFAULT_WORLD_VOLUME = "default_world_volume";

			constexpr auto *OUT_VOLUME = "volume";
		};
		namespace volume_homogeneous {
			constexpr auto *IN_PRIORITY = "priority";
			constexpr auto *IN_IOR = "IOR";
			constexpr auto *IN_ABSORPTION = "absorption";
			constexpr auto *IN_EMISSION = "emission";

			constexpr auto *IN_SCATTERING = "scattering";
			constexpr auto *IN_ASYMMETRY = "asymmetry";
			constexpr auto *IN_MULTI_SCATTERING = "multiscattering";

			constexpr auto *IN_ABSORPTION_DEPTH = "absorption_depth";
			constexpr auto *IN_DEFAULT_WORLD_VOLUME = "default_world_volume";

			constexpr auto *OUT_VOLUME = "homogeneous";
		};
		namespace volume_heterogeneous {
			constexpr auto *IN_PRIORITY = "priority";
			constexpr auto *IN_IOR = "IOR";
			constexpr auto *IN_ABSORPTION = "absorption";
			constexpr auto *IN_EMISSION = "emission";

			constexpr auto *IN_SCATTERING = "scattering";
			constexpr auto *IN_ASYMMETRY = "asymmetry";
			constexpr auto *IN_MULTI_SCATTERING = "multiscattering";

			constexpr auto *IN_STEP_SIZE = "step_size";
			constexpr auto *IN_STEP_MAX_COUNT = "step_max_count";

			constexpr auto *IN_DEFAULT_WORLD_VOLUME = "default_world_volume";

			constexpr auto *OUT_VOLUME = "heterogeneous";
		};
		namespace output {
			constexpr auto *IN_SURFACE = "surface";
			constexpr auto *IN_VOLUME = "volume";
			constexpr auto *IN_DISPLACEMENT = "displacement";
			constexpr auto *IN_NORMAL = "normal";
		};
		namespace vector_math {
			constexpr auto *IN_TYPE = "type";
			constexpr auto *IN_VECTOR1 = "vector1";
			constexpr auto *IN_VECTOR2 = "vector2";
			constexpr auto *IN_SCALE = "scale";

			constexpr auto *OUT_VALUE = "value";
			constexpr auto *OUT_VECTOR = "vector";

			enum class MathType : uint32_t {
				Add = 0u,
				Subtract,
				Multiply,
				Divide,

				CrossProduct,
				Project,
				Reflect,
				DotProduct,

				Distance,
				Length,
				Scale,
				Normalize,

				Snap,
				Floor,
				Ceil,
				Modulo,
				Fraction,
				Absolute,
				Minimum,
				Maximum,

				Count
			};
		};
		namespace mix {
			constexpr auto *IN_TYPE = "type";
			constexpr auto *IN_USE_CLAMP = "use_clamp";
			constexpr auto *IN_FAC = "fac";
			constexpr auto *IN_COLOR1 = "color1";
			constexpr auto *IN_COLOR2 = "color2";

			constexpr auto *OUT_COLOR = "color";

			enum class Mix : uint32_t {
				Blend = 0,
				Add,
				Mul,
				Sub,
				Screen,
				Div,
				Diff,
				Dark,
				Light,
				Overlay,
				Dodge,
				Burn,
				Hue,
				Sat,
				Val,
				Color,
				Soft,
				Linear,
				Clamp,

				Count
			};
		};
		namespace rgb_to_bw {
			constexpr auto *IN_COLOR = "color";

			constexpr auto *OUT_VAL = "val";
		};
		namespace invert {
			constexpr auto *IN_COLOR = "color";
			constexpr auto *IN_FAC = "fac";

			constexpr auto *OUT_COLOR = "color";
		};
		namespace vector_transform {
			constexpr auto *IN_TYPE = "type";
			constexpr auto *IN_CONVERT_FROM = "convert_from";
			constexpr auto *IN_CONVERT_TO = "convert_to";
			constexpr auto *IN_VECTOR = "vector";

			constexpr auto *OUT_VECTOR = "vector";

			enum class Type : uint32_t {
				None = 0,
				Vector,
				Point,
				Normal,

				Count
			};

			enum class ConvertSpace : uint32_t {
				World = 0,
				Object,
				Camera,

				Count
			};
		};
		namespace rgb_ramp {
			constexpr auto *IN_RAMP = "ramp";
			constexpr auto *IN_RAMP_ALPHA = "ramp_alpha";
			constexpr auto *IN_INTERPOLATE = "interpolate";
			constexpr auto *IN_FAC = "fac";

			constexpr auto *OUT_COLOR = "color";
			constexpr auto *OUT_ALPHA = "alpha";
		};
		namespace layer_weight {
			constexpr auto *IN_NORMAL = "normal";
			constexpr auto *IN_BLEND = "blend";

			constexpr auto *OUT_FRESNEL = "fresnel";
			constexpr auto *OUT_FACING = "facing";
		};
		namespace ambient_occlusion {
			constexpr auto *IN_SAMPLES = "samples";
			constexpr auto *IN_COLOR = "color";
			constexpr auto *IN_DISTANCE = "distance";
			constexpr auto *IN_NORMAL = "normal";
			constexpr auto *IN_INSIDE = "inside";
			constexpr auto *IN_ONLY_LOCAL = "only_local";

			constexpr auto *OUT_COLOR = "color";
			constexpr auto *OUT_AO = "ao";
		};
	};
	constexpr uint32_t NODE_COUNT = 44;
};

DLLRTUTIL std::ostream &operator<<(std::ostream &os, const unirender::Socket &socket);

namespace std {
	template<>
	struct hash<unirender::Socket> {
		std::size_t operator()(const unirender::Socket &k) const
		{
			using std::hash;
			using std::size_t;
			using std::string;

			std::string socketName;
			auto *node = k.GetNode(socketName);
			assert(node);
			return util::hash_combine(util::hash_combine(0, node), socketName);
		}
	};
}

#endif
