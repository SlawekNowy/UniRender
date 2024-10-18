/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/.
*
* Copyright (c) 2023 Silverlan
*/

module;

#include "definitions.hpp"
#include <sharedutils/util_hash.hpp>
#include <mathutil/color.h>
#include <optional>
#include <functional>

export module pragma.scenekit:shader_nodes;

import :data_value;

export namespace pragma::scenekit {
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
		pragma::scenekit::GroupNodeDesc *GetCommonGroupNode(const Socket &other) const;
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
			constexpr inline auto *IN_TYPE = "type";
			constexpr inline auto *IN_USE_CLAMP = "use_clamp";
			constexpr inline auto *IN_VALUE1 = "value1";
			constexpr inline auto *IN_VALUE2 = "value2";
			constexpr inline auto *IN_VALUE3 = "value3";

			constexpr inline auto *OUT_VALUE = "value";

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
			constexpr inline auto *IN_HUE = "hue";
			constexpr inline auto *IN_SATURATION = "saturation";
			constexpr inline auto *IN_VALUE = "value";
			constexpr inline auto *IN_FAC = "fac";
			constexpr inline auto *IN_COLOR = "color";

			constexpr inline auto *OUT_COLOR = "color";
		};
		namespace separate_xyz {
			constexpr inline auto *IN_VECTOR = "vector";

			constexpr inline auto *OUT_X = "x";
			constexpr inline auto *OUT_Y = "y";
			constexpr inline auto *OUT_Z = "z";
		};
		namespace combine_xyz {
			constexpr inline auto *IN_X = "x";
			constexpr inline auto *IN_Y = "Y";
			constexpr inline auto *IN_Z = "Z";

			constexpr inline auto *OUT_VECTOR = "vector";
		};
		namespace separate_rgb {
			constexpr inline auto *IN_COLOR = "color";

			constexpr inline auto *OUT_R = "r";
			constexpr inline auto *OUT_G = "g";
			constexpr inline auto *OUT_B = "b";
		};
		namespace combine_rgb {
			constexpr inline auto *IN_R = "r";
			constexpr inline auto *IN_G = "g";
			constexpr inline auto *IN_B = "b";

			constexpr inline auto *OUT_IMAGE = "image";
		};
		namespace geometry {
			constexpr inline auto *OUT_POSITION = "position";
			constexpr inline auto *OUT_NORMAL = "normal";
			constexpr inline auto *OUT_TANGENT = "tangent";
			constexpr inline auto *OUT_TRUE_NORMAL = "true_normal";
			constexpr inline auto *OUT_INCOMING = "incoming";
			constexpr inline auto *OUT_PARAMETRIC = "parametric";
			constexpr inline auto *OUT_BACKFACING = "backfacing";
			constexpr inline auto *OUT_POINTINESS = "pointiness";
			constexpr inline auto *OUT_RANDOM_PER_ISLAND = "random_per_island";
		};
		namespace camera_info {
			constexpr inline auto *OUT_VIEW_VECTOR = "view_vector";
			constexpr inline auto *OUT_VIEW_Z_DEPTH = "view_z_depth";
			constexpr inline auto *OUT_VIEW_DISTANCE = "view_distance";
		};
		namespace image_texture {
			// Note: These have to match ccl::u_colorspace_raw and ccl::u_colorspace_srgb
			constexpr inline auto *COLOR_SPACE_RAW = "__builtin_raw";
			constexpr inline auto *COLOR_SPACE_SRGB = "__builtin_srgb";

			constexpr inline auto *IN_FILENAME = "filename";
			constexpr inline auto *IN_COLORSPACE = "colorspace";
			constexpr inline auto *IN_ALPHA_TYPE = "alpha_type";
			constexpr inline auto *IN_INTERPOLATION = "interpolation";
			constexpr inline auto *IN_EXTENSION = "extension";
			constexpr inline auto *IN_PROJECTION = "projection";
			constexpr inline auto *IN_PROJECTION_BLEND = "projection_blend";
			constexpr inline auto *IN_VECTOR = "vector";

			constexpr inline auto *OUT_COLOR = "color";
			constexpr inline auto *OUT_ALPHA = "alpha";

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
			constexpr inline auto *IN_FILENAME = "filename";
			constexpr inline auto *IN_STRENGTH = "strength";

			constexpr inline auto *OUT_NORMAL = "normal";
		};
		namespace environment_texture {
			constexpr inline auto *IN_FILENAME = "filename";
			constexpr inline auto *IN_COLORSPACE = "colorspace";
			constexpr inline auto *IN_ALPHA_TYPE = "alpha_type";
			constexpr inline auto *IN_INTERPOLATION = "interpolation";
			constexpr inline auto *IN_PROJECTION = "projection";
			constexpr inline auto *IN_VECTOR = "vector";

			constexpr inline auto *OUT_COLOR = "color";
			constexpr inline auto *OUT_ALPHA = "alpha";
		};
		namespace noise_texture {
			constexpr inline auto *IN_VECTOR = "vector";
			constexpr inline auto *IN_W = "w";
			constexpr inline auto *IN_SCALE = "scale";
			constexpr inline auto *IN_DETAIL = "detail";
			constexpr inline auto *IN_ROUGHNESS = "roughness";
			constexpr inline auto *IN_DISTORTION = "distortion";

			constexpr inline auto *OUT_FAC = "fac";
			constexpr inline auto *OUT_COLOR = "color";
		};
		namespace mix_closure {
			constexpr inline auto *IN_FAC = "fac";
			constexpr inline auto *IN_CLOSURE1 = "closure1";
			constexpr inline auto *IN_CLOSURE2 = "closure2";

			constexpr inline auto *OUT_CLOSURE = "closure";
		};
		namespace add_closure {
			constexpr inline auto *IN_CLOSURE1 = "closure1";
			constexpr inline auto *IN_CLOSURE2 = "closure2";

			constexpr inline auto *OUT_CLOSURE = "closure";
		};
		namespace background_shader {
			constexpr inline auto *IN_COLOR = "color";
			constexpr inline auto *IN_STRENGTH = "strength";
			constexpr inline auto *IN_SURFACE_MIX_WEIGHT = "surface_mix_weight";

			constexpr inline auto *OUT_BACKGROUND = "background";
		};
		namespace texture_coordinate {
			constexpr inline auto *IN_FROM_DUPLI = "from_dupli";
			constexpr inline auto *IN_USE_TRANSFORM = "use_transform";
			constexpr inline auto *IN_OB_TFM = "ob_tfm";

			constexpr inline auto *OUT_GENERATED = "generated";
			constexpr inline auto *OUT_NORMAL = "normal";
			constexpr inline auto *OUT_UV = "UV";
			constexpr inline auto *OUT_OBJECT = "object";
			constexpr inline auto *OUT_CAMERA = "camera";
			constexpr inline auto *OUT_WINDOW = "window";
			constexpr inline auto *OUT_REFLECTION = "reflection";
		};
		namespace uvmap {
			constexpr inline auto *OUT_UV = "UV";
		};
		namespace mapping {
			constexpr inline auto *IN_TYPE = "type";
			constexpr inline auto *IN_VECTOR = "vector";
			constexpr inline auto *IN_LOCATION = "location";
			constexpr inline auto *IN_ROTATION = "rotation";
			constexpr inline auto *IN_SCALE = "scale";

			constexpr inline auto *OUT_VECTOR = "vector";

			enum class Type : uint32_t {
				Point = 0,
				Texture,
				Vector,
				Normal,

				Count
			};
		};
		namespace scatter_volume {
			constexpr inline auto *IN_COLOR = "color";
			constexpr inline auto *IN_DENSITY = "density";
			constexpr inline auto *IN_ANISOTROPY = "anisotropy";
			constexpr inline auto *IN_VOLUME_MIX_WEIGHT = "volume_mix_weight";

			constexpr inline auto *OUT_VOLUME = "volume";
		};
		namespace emission {
			constexpr inline auto *IN_COLOR = "color";
			constexpr inline auto *IN_STRENGTH = "strength";
			constexpr inline auto *IN_SURFACE_MIX_WEIGHT = "surface_mix_weight";

			constexpr inline auto *OUT_EMISSION = "emission";
		};
		namespace color {
			constexpr inline auto *IN_VALUE = "value";

			constexpr inline auto *OUT_COLOR = "color";
		};
		namespace attribute {
			constexpr inline auto *IN_ATTRIBUTE = "attribute";

			constexpr inline auto *OUT_COLOR = "color";
			constexpr inline auto *OUT_VECTOR = "vector";
			constexpr inline auto *OUT_FAC = "fac";
		};
		namespace light_path {
			constexpr inline auto *OUT_IS_CAMERA_RAY = "is_camera_ray";
			constexpr inline auto *OUT_IS_SHADOW_RAY = "is_shadow_ray";
			constexpr inline auto *OUT_IS_DIFFUSE_RAY = "is_diffuse_ray";
			constexpr inline auto *OUT_IS_GLOSSY_RAY = "is_glossy_ray";
			constexpr inline auto *OUT_IS_SINGULAR_RAY = "is_singular_ray";
			constexpr inline auto *OUT_IS_REFLECTION_RAY = "is_reflection_ray";
			constexpr inline auto *OUT_IS_TRANSMISSION_RAY = "is_transmission_ray";
			constexpr inline auto *OUT_IS_VOLUME_SCATTER_RAY = "is_volume_scatter_ray";

			constexpr inline auto *OUT_RAY_LENGTH = "ray_length";
			constexpr inline auto *OUT_RAY_DEPTH = "ray_depth";
			constexpr inline auto *OUT_DIFFUSE_DEPTH = "diffuse_depth";
			constexpr inline auto *OUT_GLOSSY_DEPTH = "glossy_depth";
			constexpr inline auto *OUT_TRANSPARENT_DEPTH = "transparent_depth";
			constexpr inline auto *OUT_TRANSMISSION_DEPTH = "transmission_depth";
		};
		namespace transparent_bsdf {
			constexpr inline auto *IN_COLOR = "color";
			constexpr inline auto *IN_SURFACE_MIX_WEIGHT = "surface_mix_weight";

			constexpr inline auto *OUT_BSDF = "BSDF";
		};
		namespace translucent_bsdf {
			constexpr inline auto *IN_COLOR = "color";
			constexpr inline auto *IN_NORMAL = "normal";
			constexpr inline auto *IN_SURFACE_MIX_WEIGHT = "surface_mix_weight";

			constexpr inline auto *OUT_BSDF = "BSDF";
		};
		namespace diffuse_bsdf {
			constexpr inline auto *IN_COLOR = "color";
			constexpr inline auto *IN_NORMAL = "normal";
			constexpr inline auto *IN_SURFACE_MIX_WEIGHT = "surface_mix_weight";
			constexpr inline auto *IN_ROUGHNESS = "roughness";

			constexpr inline auto *OUT_BSDF = "BSDF";
		};
		namespace normal_map {
			constexpr inline auto *IN_SPACE = "space";
			constexpr inline auto *IN_ATTRIBUTE = "attribute";
			constexpr inline auto *IN_STRENGTH = "strength";
			constexpr inline auto *IN_COLOR = "color";

			constexpr inline auto *OUT_NORMAL = "normal";

			enum class Space : uint32_t {
				Tangent = 0,
				Object,
				World,

				Count
			};
		};
		namespace principled_bsdf {
			constexpr inline auto *IN_DISTRIBUTION = "distribution";
			constexpr inline auto *IN_SUBSURFACE_METHOD = "subsurface_method";
			constexpr inline auto *IN_BASE_COLOR = "base_color";
			constexpr inline auto *IN_SUBSURFACE_COLOR = "subsurface_color";
			constexpr inline auto *IN_METALLIC = "metallic";
			constexpr inline auto *IN_SUBSURFACE = "subsurface";
			constexpr inline auto *IN_SUBSURFACE_RADIUS = "subsurface_radius";
			constexpr inline auto *IN_SPECULAR = "specular";
			constexpr inline auto *IN_ROUGHNESS = "roughness";
			constexpr inline auto *IN_SPECULAR_TINT = "specular_tint";
			constexpr inline auto *IN_ANISOTROPIC = "anisotropic";
			constexpr inline auto *IN_SHEEN = "sheen";
			constexpr inline auto *IN_SHEEN_TINT = "sheen_tint";
			constexpr inline auto *IN_CLEARCOAT = "clearcoat";
			constexpr inline auto *IN_CLEARCOAT_ROUGHNESS = "clearcoat_roughness";
			constexpr inline auto *IN_IOR = "ior";
			constexpr inline auto *IN_TRANSMISSION = "transmission";
			constexpr inline auto *IN_TRANSMISSION_ROUGHNESS = "transmission_roughness";
			constexpr inline auto *IN_ANISOTROPIC_ROTATION = "anisotropic_rotation";
			constexpr inline auto *IN_EMISSION = "emission";
			constexpr inline auto *IN_ALPHA = "alpha";
			constexpr inline auto *IN_NORMAL = "normal";
			constexpr inline auto *IN_CLEARCOAT_NORMAL = "clearcoat_normal";
			constexpr inline auto *IN_TANGENT = "tangent";
			constexpr inline auto *IN_SURFACE_MIX_WEIGHT = "surface_mix_weight";

			constexpr inline auto *OUT_BSDF = "BSDF";
		};
		namespace principled_volume {
			constexpr inline auto *IN_COLOR = "color";
			constexpr inline auto *IN_DENSITY = "density";
			constexpr inline auto *IN_ANISOTROPY = "anisotropy";
			constexpr inline auto *IN_ABSORPTION_COLOR = "absorption_color";
			constexpr inline auto *IN_EMISSION_STRENGTH = "emission_strength";
			constexpr inline auto *IN_EMISSION_COLOR = "emission_color";
			constexpr inline auto *IN_BLACKBODY_INTENSITY = "blackbody_intensity";
			constexpr inline auto *IN_BLACKBODY_TINT = "blackbody_tint";
			constexpr inline auto *IN_TEMPERATURE = "temperature";
			constexpr inline auto *IN_VOLUME_MIX_WEIGHT = "volume_mix_weight";

			constexpr inline auto *OUT_VOLUME = "volume";
		};
		namespace toon_bsdf {
			constexpr inline auto *IN_COMPONENT = "component";
			constexpr inline auto *IN_COLOR = "color";
			constexpr inline auto *IN_NORMAL = "normal";
			constexpr inline auto *IN_SURFACE_MIX_WEIGHT = "surface_mix_weight";
			constexpr inline auto *IN_SIZE = "size";
			constexpr inline auto *IN_SMOOTH = "smooth";

			constexpr inline auto *OUT_BSDF = "BSDF";
		};
		namespace glossy_bsdf {
			constexpr inline auto *IN_COLOR = "color";
			constexpr inline auto *IN_ALPHA = "alpha";
			constexpr inline auto *IN_NORMAL = "normal";
			constexpr inline auto *IN_SURFACE_MIX_WEIGHT = "surface_mix_weight";
			constexpr inline auto *IN_DISTRIBUTION = "distribution";
			constexpr inline auto *IN_ROUGHNESS = "roughness";

			constexpr inline auto *OUT_BSDF = "BSDF";
		};
		namespace glass_bsdf {
			constexpr inline auto *IN_DISTRIBUTION = "distribution";
			constexpr inline auto *IN_COLOR = "color";
			constexpr inline auto *IN_NORMAL = "normal";
			constexpr inline auto *IN_SURFACE_MIX_WEIGHT = "surface_mix_weight";
			constexpr inline auto *IN_ROUGHNESS = "roughness";
			constexpr inline auto *IN_IOR = "IOR";

			constexpr inline auto *OUT_BSDF = "BSDF";
		};
		namespace volume_clear {
			constexpr inline auto *IN_PRIORITY = "priority";
			constexpr inline auto *IN_IOR = "IOR";
			constexpr inline auto *IN_ABSORPTION = "absorption";
			constexpr inline auto *IN_EMISSION = "emission";

			constexpr inline auto *IN_DEFAULT_WORLD_VOLUME = "default_world_volume";

			constexpr inline auto *OUT_VOLUME = "volume";
		};
		namespace volume_homogeneous {
			constexpr inline auto *IN_PRIORITY = "priority";
			constexpr inline auto *IN_IOR = "IOR";
			constexpr inline auto *IN_ABSORPTION = "absorption";
			constexpr inline auto *IN_EMISSION = "emission";

			constexpr inline auto *IN_SCATTERING = "scattering";
			constexpr inline auto *IN_ASYMMETRY = "asymmetry";
			constexpr inline auto *IN_MULTI_SCATTERING = "multiscattering";

			constexpr inline auto *IN_ABSORPTION_DEPTH = "absorption_depth";
			constexpr inline auto *IN_DEFAULT_WORLD_VOLUME = "default_world_volume";

			constexpr inline auto *OUT_VOLUME = "homogeneous";
		};
		namespace volume_heterogeneous {
			constexpr inline auto *IN_PRIORITY = "priority";
			constexpr inline auto *IN_IOR = "IOR";
			constexpr inline auto *IN_ABSORPTION = "absorption";
			constexpr inline auto *IN_EMISSION = "emission";

			constexpr inline auto *IN_SCATTERING = "scattering";
			constexpr inline auto *IN_ASYMMETRY = "asymmetry";
			constexpr inline auto *IN_MULTI_SCATTERING = "multiscattering";

			constexpr inline auto *IN_STEP_SIZE = "step_size";
			constexpr inline auto *IN_STEP_MAX_COUNT = "step_max_count";

			constexpr inline auto *IN_DEFAULT_WORLD_VOLUME = "default_world_volume";

			constexpr inline auto *OUT_VOLUME = "heterogeneous";
		};
		namespace output {
			constexpr inline auto *IN_SURFACE = "surface";
			constexpr inline auto *IN_VOLUME = "volume";
			constexpr inline auto *IN_DISPLACEMENT = "displacement";
			constexpr inline auto *IN_NORMAL = "normal";
		};
		namespace vector_math {
			constexpr inline auto *IN_TYPE = "type";
			constexpr inline auto *IN_VECTOR1 = "vector1";
			constexpr inline auto *IN_VECTOR2 = "vector2";
			constexpr inline auto *IN_SCALE = "scale";

			constexpr inline auto *OUT_VALUE = "value";
			constexpr inline auto *OUT_VECTOR = "vector";

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
			constexpr inline auto *IN_TYPE = "type";
			constexpr inline auto *IN_USE_CLAMP = "use_clamp";
			constexpr inline auto *IN_FAC = "fac";
			constexpr inline auto *IN_COLOR1 = "color1";
			constexpr inline auto *IN_COLOR2 = "color2";

			constexpr inline auto *OUT_COLOR = "color";

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
				Exclusion,
				Clamp,

				Count
			};
		};
		namespace rgb_to_bw {
			constexpr inline auto *IN_COLOR = "color";

			constexpr inline auto *OUT_VAL = "val";
		};
		namespace invert {
			constexpr inline auto *IN_COLOR = "color";
			constexpr inline auto *IN_FAC = "fac";

			constexpr inline auto *OUT_COLOR = "color";
		};
		namespace vector_transform {
			constexpr inline auto *IN_TYPE = "type";
			constexpr inline auto *IN_CONVERT_FROM = "convert_from";
			constexpr inline auto *IN_CONVERT_TO = "convert_to";
			constexpr inline auto *IN_VECTOR = "vector";

			constexpr inline auto *OUT_VECTOR = "vector";

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
			constexpr inline auto *IN_RAMP = "ramp";
			constexpr inline auto *IN_RAMP_ALPHA = "ramp_alpha";
			constexpr inline auto *IN_INTERPOLATE = "interpolate";
			constexpr inline auto *IN_FAC = "fac";

			constexpr inline auto *OUT_COLOR = "color";
			constexpr inline auto *OUT_ALPHA = "alpha";
		};
		namespace layer_weight {
			constexpr inline auto *IN_NORMAL = "normal";
			constexpr inline auto *IN_BLEND = "blend";

			constexpr inline auto *OUT_FRESNEL = "fresnel";
			constexpr inline auto *OUT_FACING = "facing";
		};
		namespace ambient_occlusion {
			constexpr inline auto *IN_SAMPLES = "samples";
			constexpr inline auto *IN_COLOR = "color";
			constexpr inline auto *IN_DISTANCE = "distance";
			constexpr inline auto *IN_NORMAL = "normal";
			constexpr inline auto *IN_INSIDE = "inside";
			constexpr inline auto *IN_ONLY_LOCAL = "only_local";

			constexpr inline auto *OUT_COLOR = "color";
			constexpr inline auto *OUT_AO = "ao";
		};
	};
	constexpr uint32_t NODE_COUNT = 44;
};

export DLLRTUTIL std::ostream &operator<<(std::ostream &os, const pragma::scenekit::Socket &socket);

export namespace std {
	template<>
	struct hash<pragma::scenekit::Socket> {
		std::size_t operator()(const pragma::scenekit::Socket &k) const
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
